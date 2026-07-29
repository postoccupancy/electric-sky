#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <time.h>

#include "Dashboard.h"
#include "SampleRing.h"
#include "SensorManager.h"

#ifndef FW_GIT_SHA
#define FW_GIT_SHA "unknown"
#endif
#ifndef FW_GIT_DIRTY
#define FW_GIT_DIRTY 1
#endif
#ifndef FW_BUILD_UTC
#define FW_BUILD_UTC "unknown"
#endif

struct WifiNetwork {
  const char* ssid;
  const char* password;
};

const WifiNetwork WIFI_NETWORKS[] = {
  {"Resident Frequency LAN", "fastfourier"},
  {"Knight-MacDonald_EXT", "409Jasper!"},
  {"Knight-MacDonald", "409Jasper!"}
};
constexpr size_t WIFI_NETWORK_COUNT = sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]);
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000;
constexpr uint32_t WIFI_PRIORITY_SCAN_INTERVAL_MS = 300000;
constexpr uint32_t WIFI_OFFLINE_RESTART_MS = 600000;

constexpr uint32_t BME_INTERVAL_MS = 10;
constexpr uint32_t TRANSPORT_INTERVAL_MS = 63;
constexpr size_t MAX_BME_PER_PACKET = 8;
constexpr size_t MAX_POWER_PER_PACKET = 63;
constexpr size_t MAX_AUDIO_PER_PACKET = 16;
constexpr size_t TRANSPORT_PACKET_BYTES = 3072;
constexpr size_t TRANSPORT_QUEUE_DEPTH = 120;
constexpr uint16_t INA219_CONVERSION_US = 1064;
constexpr uint16_t OSC_ROUTER_PORT = 5005;
constexpr uint16_t PCM_ROUTER_PORT = 5007;
constexpr uint32_t PCM_SAMPLE_RATE = 16000;
constexpr size_t PCM_SAMPLES_PER_PACKET = 640;
constexpr size_t PCM_QUEUE_DEPTH = 24;
// UDP can briefly report send failures while Wi-Fi remains usable. Requiring a
// sustained run preserves the safety cutoff without disabling audio after a
// few congested packets.
constexpr uint8_t PCM_FAILURE_LIMIT = 50;
constexpr size_t OSC_PACKET_BYTES = 1472;
IPAddress oscRouterIp(192, 168, 0, 41);

struct BmeSample {
  uint32_t sequence;
  uint64_t timeUs;
  float temperature;
  float humidity;
  float pressure;
} __attribute__((packed));

struct PowerSample {
  uint32_t sequence;
  uint64_t timeUs;
  float busVoltage;
  float currentMa;
  float powerMw;
  float solarBusVoltage;
  float solarCurrentMa;
  float solarPowerMw;
} __attribute__((packed));

struct AudioSample {
  uint32_t sequence;
  uint64_t timeUs;
  float rmsDb;
} __attribute__((packed));

struct PacketHeader {
  char magic[4];
  uint8_t version;
  uint8_t flags;
  uint16_t headerBytes;
  uint32_t packetSequence;
  uint64_t sendTimeUs;
  uint16_t bmeCount;
  uint16_t powerCount;
  uint16_t audioCount;
  uint16_t reserved;
  uint32_t bmeOverruns;
  uint32_t powerOverruns;
  uint32_t audioOverruns;
  uint16_t bmeHzX10;
  uint16_t powerHzX10;
  uint16_t audioHzX10;
  uint16_t bmeQueue;
  uint16_t powerQueue;
  uint16_t audioQueue;
  uint16_t transportQueued;
  uint32_t transportDrops;
  uint16_t scheduledHzX10;
  uint32_t uptimeMs;
  uint16_t powerIntervalAvgUs;
  uint16_t powerIntervalMaxUs;
  uint16_t powerDuplicatePermille;
  uint16_t powerConversionUs;
  int16_t wifiRssiDbm;
  uint16_t wifiReconnects;
  uint32_t oscSendFailures;
  uint32_t oscSendAvgUs;
  uint32_t oscSendMaxUs;
  uint32_t oscSendStalls;
} __attribute__((packed));

struct TransportPacket {
  uint16_t length;
  uint8_t data[TRANSPORT_PACKET_BYTES];
};

struct PcmPacketHeader {
  char magic[4];
  uint8_t version;
  uint8_t channels;
  uint8_t bitsPerSample;
  uint8_t flags;
  uint32_t packetSequence;
  uint64_t firstSampleTimeUs;
  uint32_t sampleRate;
  uint16_t sampleCount;
  uint16_t headerBytes;
  uint32_t queueDrops;
} __attribute__((packed));

struct PcmPacket {
  PcmPacketHeader header;
  int16_t samples[PCM_SAMPLES_PER_PACKET];
} __attribute__((packed));

struct EncodedPcmPacket {
  PcmPacketHeader header;
  int16_t predictor;
  uint8_t index;
  uint8_t reserved;
  uint8_t data[PCM_SAMPLES_PER_PACKET / 2];
} __attribute__((packed));

struct OscWriter {
  uint8_t* data;
  size_t capacity;
  size_t length = 0;
  bool ok = true;

  OscWriter(uint8_t* output, size_t outputCapacity)
    : data(output), capacity(outputCapacity) {}

  void bytes(const void* source, size_t count) {
    if (!ok || length + count > capacity) { ok = false; return; }
    memcpy(data + length, source, count);
    length += count;
  }
  void u32(uint32_t value) {
    uint8_t encoded[4] = {
      static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16),
      static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)
    };
    bytes(encoded, sizeof(encoded));
  }
  void i32(int32_t value) { u32(static_cast<uint32_t>(value)); }
  void f32(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    u32(bits);
  }
  void f64(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    uint8_t encoded[8];
    for (int i = 0; i < 8; i++) encoded[i] = static_cast<uint8_t>(bits >> (56 - i * 8));
    bytes(encoded, sizeof(encoded));
  }
  void string(const char* value) {
    size_t count = strlen(value) + 1;
    size_t padded = (count + 3) & ~static_cast<size_t>(3);
    if (!ok || length + padded > capacity) { ok = false; return; }
    memset(data + length, 0, padded);
    memcpy(data + length, value, count - 1);
    length += padded;
  }
  void patchU32(size_t position, uint32_t value) {
    if (position + 4 > capacity) { ok = false; return; }
    data[position] = static_cast<uint8_t>(value >> 24);
    data[position + 1] = static_cast<uint8_t>(value >> 16);
    data[position + 2] = static_cast<uint8_t>(value >> 8);
    data[position + 3] = static_cast<uint8_t>(value);
  }
};

static_assert(sizeof(BmeSample) == 24, "BME wire format changed");
static_assert(sizeof(PowerSample) == 36, "power wire format changed");
static_assert(sizeof(AudioSample) == 16, "audio wire format changed");
static_assert(sizeof(PacketHeader) == 92, "packet header changed");
static_assert(sizeof(PcmPacketHeader) == 32, "PCM header changed");
static_assert(sizeof(PcmPacket) == 1312, "PCM packet changed");
static_assert(sizeof(EncodedPcmPacket) == 356, "encoded PCM packet changed");
static_assert(sizeof(PacketHeader) + MAX_BME_PER_PACKET * sizeof(BmeSample) +
  MAX_POWER_PER_PACKET * sizeof(PowerSample) + MAX_AUDIO_PER_PACKET * sizeof(AudioSample)
  <= TRANSPORT_PACKET_BYTES, "transport packet buffer too small");

WebServer server(80);
WebSocketsServer webSocket(81);
WiFiUDP oscUdp;
WiFiUDP pcmUdp;
SensorManager sensors;
static volatile uint8_t webSocketClients = 0;

SampleRing<BmeSample, 256> bmeRing;
SampleRing<PowerSample, 1024> powerRing;
SampleRing<AudioSample, 512> audioRing;

volatile bool otaInProgress = false;
volatile bool otaAudioStopped = false;
volatile uint32_t bmeProduced = 0;
volatile uint32_t powerProduced = 0;
volatile uint32_t audioProduced = 0;
volatile float bmeActualHz = 0;
volatile float powerActualHz = 0;
volatile float audioActualHz = 0;
volatile uint32_t powerDuplicates = 0;
volatile uint32_t powerIntervalMaxUs = 0;
volatile uint16_t powerIntervalAvgUs = 0;
volatile uint16_t powerDuplicatePermille = 0;
volatile uint32_t cameraCaptures = 0;
volatile uint32_t cameraFailures = 0;
volatile uint32_t cameraLastBytes = 0;
volatile uint32_t cameraLastCaptureMs = 0;
volatile uint16_t cameraLastWidth = 0;
volatile uint16_t cameraLastHeight = 0;

static SemaphoreHandle_t latestMutex;
static QueueHandle_t transportQueue;
static TaskHandle_t transportTaskHandle = nullptr;
static TaskHandle_t audioTaskHandle = nullptr;
static TaskHandle_t pcmTaskHandle = nullptr;
static StaticQueue_t transportQueueControl;
static uint8_t* transportQueueStorage = nullptr;
static volatile uint32_t transportDrops = 0;
static volatile uint32_t oscPacketsSent = 0;
static volatile uint32_t oscSendFailures = 0;
static volatile uint16_t oscLargestPacket = 0;
static volatile uint32_t oscSendAvgUs = 0;
static volatile uint32_t oscSendMaxUs = 0;
static volatile uint32_t oscSendStalls = 0;
static volatile uint16_t wifiReconnects = 0;
static QueueHandle_t pcmQueue;
static StaticQueue_t pcmQueueControl;
static uint8_t* pcmQueueStorage = nullptr;
static volatile bool pcmStreamEnabled = false;
static volatile bool pcmAutoDisabled = false;
static volatile uint8_t pcmConsecutiveFailures = 0;
static volatile uint32_t pcmPacketsQueued = 0;
static volatile uint32_t pcmPacketsSent = 0;
static volatile uint32_t pcmQueueDrops = 0;
static volatile uint32_t pcmSendFailures = 0;
static uint8_t oscPacketBuffer[OSC_PACKET_BYTES];
static TransportPacket transportWorkPacket;
static TransportPacket transportStalePacket;
static EncodedPcmPacket encodedPcmWork;
static BmeSample transportBmeBatch[MAX_BME_PER_PACKET];
static PowerSample transportPowerBatch[MAX_POWER_PER_PACKET];
static AudioSample transportAudioBatch[MAX_AUDIO_PER_PACKET];
static BmeSample latestBme = {};
static PowerSample latestPower = {};
static AudioSample latestAudio = {};
static bool hasBme = false;
static bool hasPower = false;
static bool hasSolarPower = false;
static bool hasAudio = false;
RTC_DATA_ATTR uint32_t bootCount = 0;
static esp_reset_reason_t resetReason = ESP_RST_UNKNOWN;

static const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    default: return "unknown";
  }
}

static bool connectPreferredWifi(int attemptsPerNetwork) {
  WiFi.mode(WIFI_STA);
  for (size_t network = 0; network < WIFI_NETWORK_COUNT; network++) {
    // Leave the station radio enabled while moving between candidates.
    // disconnect(true) powers Wi-Fi down and can leave subsequent begin()
    // attempts unable to associate during boot/recovery.
    WiFi.disconnect(false, false);
    delay(250);
    Serial.printf("WiFi: trying %s\n", WIFI_NETWORKS[network].ssid);
    WiFi.begin(WIFI_NETWORKS[network].ssid, WIFI_NETWORKS[network].password);
    for (int attempt = 0;
         attempt < attemptsPerNetwork && WiFi.status() != WL_CONNECTED;
         attempt++) {
      delay(500);
    }
    if (WiFi.status() == WL_CONNECTED) return true;
  }
  // Reset the station state after a complete failed cycle so a driver left
  // stale by a vanished access point cannot remain wedged indefinitely.
  WiFi.disconnect(true, false);
  delay(100);
  WiFi.mode(WIFI_STA);
  return false;
}

static int configuredWifiIndex(const String& ssid) {
  for (size_t network = 0; network < WIFI_NETWORK_COUNT; network++) {
    if (ssid == WIFI_NETWORKS[network].ssid) return static_cast<int>(network);
  }
  return -1;
}

static bool higherPriorityWifiVisible() {
  int current = configuredWifiIndex(WiFi.SSID());
  if (current <= 0) return false;
  int found = WiFi.scanNetworks(false, true);
  bool higherPriority = false;
  for (int result = 0; result < found && !higherPriority; result++) {
    for (int network = 0; network < current; network++) {
      if (WiFi.SSID(result) == WIFI_NETWORKS[network].ssid) {
        higherPriority = true;
        break;
      }
    }
  }
  WiFi.scanDelete();
  return higherPriority;
}

static void resolveRouterIp() {
  IPAddress resolved = MDNS.queryHost("adrian-pi");
  if (!resolved) WiFi.hostByName("adrian-pi.local", resolved);
  if (resolved) {
    oscRouterIp = resolved;
    Serial.printf("Signal router: %s\n", oscRouterIp.toString().c_str());
  } else {
    const bool residentLan = WiFi.SSID() == WIFI_NETWORKS[0].ssid;
    oscRouterIp = residentLan
      ? IPAddress(192, 168, 50, 12)
      : IPAddress(192, 168, 0, 41);
    Serial.printf("Signal router discovery failed; using %s fallback %s\n",
      residentLan ? "festival" : "home", oscRouterIp.toString().c_str());
  }
}

static void setPcmStreamEnabled(bool enabled, bool automatic = false) {
  pcmStreamEnabled = enabled;
  pcmAutoDisabled = automatic && !enabled;
  pcmConsecutiveFailures = 0;
  if (pcmQueue) xQueueReset(pcmQueue);
  if (!enabled) pcmUdp.stop();
}

static uint64_t nowUs() {
  return static_cast<uint64_t>(esp_timer_get_time());
}

static const int8_t IMA_INDEX_TABLE[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static const uint16_t IMA_STEP_TABLE[89] = {
  7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,
  73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,
  449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,
  2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,
  7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,
  24623,27086,29794,32767
};

static size_t encodeImaAdpcm(const PcmPacket& source, EncodedPcmPacket& output) {
  output.header = source.header;
  output.header.bitsPerSample = 4;
  output.header.headerBytes = sizeof(PcmPacketHeader) + 4;
  int predictor = source.samples[0], index = 0;
  output.predictor = predictor; output.index = index; output.reserved = 0;
  memset(output.data, 0, sizeof(output.data));
  for (size_t i = 1; i < source.header.sampleCount; ++i) {
    int step = IMA_STEP_TABLE[index], difference = source.samples[i] - predictor;
    uint8_t code = difference < 0 ? 8 : 0;
    if (difference < 0) difference = -difference;
    int delta = step >> 3;
    if (difference >= step) { code |= 4; difference -= step; delta += step; }
    if (difference >= (step >> 1)) { code |= 2; difference -= step >> 1; delta += step >> 1; }
    if (difference >= (step >> 2)) { code |= 1; delta += step >> 2; }
    predictor += code & 8 ? -delta : delta;
    predictor = constrain(predictor, -32768, 32767);
    index = constrain(index + IMA_INDEX_TABLE[code & 7], 0, 88);
    size_t nibble = i - 1, byte = nibble / 2;
    if (nibble & 1) output.data[byte] |= code << 4; else output.data[byte] = code;
  }
  return output.header.headerBytes + (source.header.sampleCount - 1 + 1) / 2;
}

static void beginOscBundle(OscWriter& writer) {
  writer.string("#bundle");
  writer.u32(0);
  writer.u32(1); // OSC immediate timetag
}

static size_t beginOscElement(OscWriter& writer) {
  size_t sizePosition = writer.length;
  writer.u32(0);
  return sizePosition;
}

static void finishOscElement(OscWriter& writer, size_t sizePosition) {
  writer.patchU32(sizePosition, static_cast<uint32_t>(writer.length - sizePosition - 4));
}

static void writeOscBatchHeader(OscWriter& writer, const char* address,
                                const char* typeTags, const char* unit,
                                const PacketHeader& header) {
  writer.string(address);
  writer.string(typeTags);
  writer.u32(header.packetSequence);
  writer.f64(static_cast<double>(header.sendTimeUs));
  writer.string(unit);
}

static bool sendOscPacket(OscWriter& writer) {
  if (!writer.ok || writer.length == 0 || WiFi.status() != WL_CONNECTED) {
    oscSendFailures++;
    return false;
  }
  uint64_t startedUs = nowUs();
  bool sent = oscUdp.beginPacket(oscRouterIp, OSC_ROUTER_PORT);
  if (sent) sent = oscUdp.write(writer.data, writer.length) == writer.length;
  if (sent) sent = oscUdp.endPacket();
  else oscUdp.stop();
  uint32_t elapsedUs = static_cast<uint32_t>(nowUs() - startedUs);
  oscSendAvgUs = oscSendAvgUs ? (oscSendAvgUs * 15 + elapsedUs) / 16 : elapsedUs;
  if (elapsedUs > oscSendMaxUs) oscSendMaxUs = elapsedUs;
  if (elapsedUs > 20000) oscSendStalls++;
  if (!sent) {
    oscSendFailures++;
    return false;
  }
  oscPacketsSent++;
  if (writer.length > oscLargestPacket) oscLargestPacket = writer.length;
  return true;
}

static void sendOscBatches(const TransportPacket& packet) {
  if (packet.length < sizeof(PacketHeader)) return;
  PacketHeader header;
  memcpy(&header, packet.data, sizeof(header));
  if (memcmp(header.magic, "ESKY", 4) != 0 || header.headerBytes != sizeof(PacketHeader)) return;

  size_t bmeOffset = header.headerBytes;
  size_t powerOffset = bmeOffset + header.bmeCount * sizeof(BmeSample);
  size_t audioOffset = powerOffset + header.powerCount * sizeof(PowerSample);
  if (audioOffset + header.audioCount * sizeof(AudioSample) > packet.length) return;

  // Climate and RMS are small enough to share one sub-MTU OSC bundle.
  if (header.bmeCount > 0 || header.audioCount > 0 ||
      (header.powerCount > 0 && (header.flags & 0x01))) {
    OscWriter writer{oscPacketBuffer, sizeof(oscPacketBuffer)};
    beginOscBundle(writer);
    if (header.bmeCount > 0) {
      const char* addresses[] = {
        "/batch/electric-sky/temperature",
        "/batch/electric-sky/humidity",
        "/batch/electric-sky/pressure"
      };
      const char* units[] = {"celsius", "percent", "hpa"};
      for (size_t channel = 0; channel < 3; channel++) {
        char tags[5 + MAX_BME_PER_PACKET * 3] = {',', 'i', 'd', 's'};
        size_t tag = 4;
        for (size_t i = 0; i < header.bmeCount; i++) {
          for (char type : {'i', 'i', 'f'}) tags[tag++] = type;
        }
        tags[tag] = '\0';
        size_t element = beginOscElement(writer);
        writeOscBatchHeader(writer, addresses[channel], tags, units[channel], header);
        for (size_t i = 0; i < header.bmeCount; i++) {
          BmeSample sample;
          memcpy(&sample, packet.data + bmeOffset + i * sizeof(sample), sizeof(sample));
          writer.u32(sample.sequence);
          writer.i32(static_cast<int32_t>(static_cast<int64_t>(sample.timeUs) -
                                          static_cast<int64_t>(header.sendTimeUs)));
          const float values[] = {sample.temperature, sample.humidity, sample.pressure};
          writer.f32(values[channel]);
        }
        finishOscElement(writer, element);
      }
    }
    if (header.audioCount > 0) {
      char tags[5 + MAX_AUDIO_PER_PACKET * 3] = {',', 'i', 'd', 's'};
      size_t tag = 4;
      for (size_t i = 0; i < header.audioCount; i++) {
        for (char type : {'i', 'i', 'f'}) tags[tag++] = type;
      }
      tags[tag] = '\0';
      size_t element = beginOscElement(writer);
      writeOscBatchHeader(writer, "/batch/electric-sky/rms", tags, "dbfs", header);
      for (size_t i = 0; i < header.audioCount; i++) {
        AudioSample sample;
        memcpy(&sample, packet.data + audioOffset + i * sizeof(sample), sizeof(sample));
        writer.u32(sample.sequence);
        writer.i32(static_cast<int32_t>(static_cast<int64_t>(sample.timeUs) -
                                        static_cast<int64_t>(header.sendTimeUs)));
        writer.f32(sample.rmsDb);
      }
      finishOscElement(writer, element);
    }
    if (header.powerCount > 0 && (header.flags & 0x01)) {
      PowerSample sample;
      memcpy(&sample, packet.data + powerOffset +
             (header.powerCount - 1) * sizeof(sample), sizeof(sample));
      const char* addresses[] = {
        "/batch/electric-sky/solar-voltage",
        "/batch/electric-sky/solar-current"
      };
      const char* units[] = {"volts", "ma"};
      const float values[] = {sample.solarBusVoltage, sample.solarCurrentMa};
      for (size_t channel = 0; channel < 2; channel++) {
        size_t element = beginOscElement(writer);
        writeOscBatchHeader(writer, addresses[channel], ",idsiif", units[channel], header);
        writer.u32(sample.sequence);
        writer.i32(static_cast<int32_t>(static_cast<int64_t>(sample.timeUs) -
                                        static_cast<int64_t>(header.sendTimeUs)));
        writer.f32(values[channel]);
        finishOscElement(writer, element);
      }
    }
    sendOscPacket(writer);
  }

  // Power is isolated so a full 63-sample message remains below the MTU.
  if (header.powerCount > 0) {
    OscWriter writer{oscPacketBuffer, sizeof(oscPacketBuffer)};
    beginOscBundle(writer);
    char tags[5 + MAX_POWER_PER_PACKET * 3] = {',', 'i', 'd', 's'};
    size_t tag = 4;
    for (size_t i = 0; i < header.powerCount; i++) {
      for (char type : {'i', 'i', 'f'}) tags[tag++] = type;
    }
    tags[tag] = '\0';
    size_t element = beginOscElement(writer);
    writeOscBatchHeader(writer, "/batch/electric-sky/power", tags, "mw", header);
    for (size_t i = 0; i < header.powerCount; i++) {
      PowerSample sample;
      memcpy(&sample, packet.data + powerOffset + i * sizeof(sample), sizeof(sample));
      writer.u32(sample.sequence);
      writer.i32(static_cast<int32_t>(static_cast<int64_t>(sample.timeUs) -
                                      static_cast<int64_t>(header.sendTimeUs)));
      writer.f32(sample.powerMw);
    }
    finishOscElement(writer, element);
    sendOscPacket(writer);

    if (!(header.flags & 0x01)) return;
    OscWriter solarWriter{oscPacketBuffer, sizeof(oscPacketBuffer)};
    beginOscBundle(solarWriter);
    size_t solarElement = beginOscElement(solarWriter);
    writeOscBatchHeader(solarWriter, "/batch/electric-sky/solar-power", tags, "mw", header);
    for (size_t i = 0; i < header.powerCount; i++) {
      PowerSample sample;
      memcpy(&sample, packet.data + powerOffset + i * sizeof(sample), sizeof(sample));
      solarWriter.u32(sample.sequence);
      solarWriter.i32(static_cast<int32_t>(static_cast<int64_t>(sample.timeUs) -
                                           static_cast<int64_t>(header.sendTimeUs)));
      solarWriter.f32(sample.solarPowerMw);
    }
    finishOscElement(solarWriter, solarElement);
    sendOscPacket(solarWriter);
  }
}

static String isoTime() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  struct tm timeinfo;
  gmtime_r(&tv.tv_sec, &timeinfo);
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
    timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
    static_cast<long>(tv.tv_usec / 1000));
  return String(buffer);
}

static String makeStatusJson(bool* okOut = nullptr) {
  BmeSample bme;
  PowerSample power;
  AudioSample audio;
  bool bmeOk, powerOk, solarPowerOk, audioOk;

  xSemaphoreTake(latestMutex, portMAX_DELAY);
  bme = latestBme;
  power = latestPower;
  audio = latestAudio;
  bmeOk = hasBme;
  powerOk = hasPower;
  solarPowerOk = hasSolarPower;
  audioOk = hasAudio;
  xSemaphoreGive(latestMutex);

  bool cameraOk = sensors.cameraAvailable();
  bool ok = bmeOk && powerOk && audioOk && cameraOk;
  if (okOut) *okOut = ok;

  String json = "{";
  json += "\"firmware_git_sha\":\"" FW_GIT_SHA "\",";
  json += "\"firmware_git_dirty\":" + String(FW_GIT_DIRTY ? "true" : "false") + ",";
  json += "\"firmware_build_utc\":\"" FW_BUILD_UTC "\",";
  json += "\"uptime_ms\":" + String(millis()) + ",";
  json += "\"boot_count\":" + String(bootCount) + ",";
  json += "\"reset_reason_code\":" + String(static_cast<int>(resetReason)) + ",";
  json += "\"reset_reason\":\"" + String(resetReasonName(resetReason)) + "\",";
  json += "\"wifi_ssid\":\"" + WiFi.SSID() + "\",";
  json += "\"wifi_bssid\":\"" + WiFi.BSSIDstr() + "\",";
  json += "\"wifi_channel\":" + String(WiFi.channel()) + ",";
  json += "\"wifi_gateway\":\"" + WiFi.gatewayIP().toString() + "\",";
  json += "\"wifi_rssi_dbm\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0) + ",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"min_free_heap\":" + String(ESP.getMinFreeHeap()) + ",";
  json += "\"transport_stack_free\":" + String(transportTaskHandle ? uxTaskGetStackHighWaterMark(transportTaskHandle) : 0) + ",";
  json += "\"audio_stack_free\":" + String(audioTaskHandle ? uxTaskGetStackHighWaterMark(audioTaskHandle) : 0) + ",";
  json += "\"pcm_stack_free\":" + String(pcmTaskHandle ? uxTaskGetStackHighWaterMark(pcmTaskHandle) : 0) + ",";
  json += "\"timestamp\":\"" + isoTime() + "\",";
  json += "\"bme_actual_hz\":" + String(bmeActualHz, 2) + ",";
  json += "\"power_actual_hz\":" + String(powerActualHz, 2) + ",";
  json += "\"audio_actual_hz\":" + String(audioActualHz, 2) + ",";
  json += "\"bme_queue\":" + String(bmeRing.size()) + ",";
  json += "\"power_queue\":" + String(powerRing.size()) + ",";
  json += "\"audio_queue\":" + String(audioRing.size()) + ",";
  json += "\"bme_overruns\":" + String(bmeRing.overruns()) + ",";
  json += "\"power_overruns\":" + String(powerRing.overruns()) + ",";
  json += "\"audio_overruns\":" + String(audioRing.overruns()) + ",";
  json += "\"transport_queue\":" + String(uxQueueMessagesWaiting(transportQueue)) + ",";
  json += "\"transport_drops\":" + String(transportDrops) + ",";
  json += "\"osc_router\":\"" + oscRouterIp.toString() + ":" + String(OSC_ROUTER_PORT) + "\",";
  json += "\"osc_packets_sent\":" + String(oscPacketsSent) + ",";
  json += "\"osc_send_failures\":" + String(oscSendFailures) + ",";
  json += "\"osc_largest_packet\":" + String(oscLargestPacket) + ",";
  json += "\"osc_send_avg_us\":" + String(oscSendAvgUs) + ",";
  json += "\"osc_send_max_us\":" + String(oscSendMaxUs) + ",";
  json += "\"osc_send_stalls\":" + String(oscSendStalls) + ",";
  json += "\"wifi_reconnects\":" + String(wifiReconnects) + ",";
  json += "\"pcm_stream_enabled\":" + String(pcmStreamEnabled ? "true" : "false") + ",";
  json += "\"pcm_default_enabled\":false,";
  json += "\"pcm_auto_disabled\":" + String(pcmAutoDisabled ? "true" : "false") + ",";
  json += "\"pcm_consecutive_failures\":" + String(pcmConsecutiveFailures) + ",";
  json += "\"pcm_router\":\"" + oscRouterIp.toString() + ":" + String(PCM_ROUTER_PORT) + "\",";
  json += "\"pcm_packets_queued\":" + String(pcmPacketsQueued) + ",";
  json += "\"pcm_packets_sent\":" + String(pcmPacketsSent) + ",";
  json += "\"pcm_queue\":" + String(pcmQueue ? uxQueueMessagesWaiting(pcmQueue) : 0) + ",";
  json += "\"pcm_queue_drops\":" + String(pcmQueueDrops) + ",";
  json += "\"pcm_send_failures\":" + String(pcmSendFailures) + ",";
  json += "\"camera_ok\":" + String(cameraOk ? "true" : "false") + ",";
  json += "\"camera_captures\":" + String(cameraCaptures) + ",";
  json += "\"camera_failures\":" + String(cameraFailures) + ",";
  json += "\"camera_last_bytes\":" + String(cameraLastBytes) + ",";
  json += "\"camera_last_capture_ms\":" + String(cameraLastCaptureMs) + ",";
  json += "\"camera_width\":" + String(cameraLastWidth) + ",";
  json += "\"camera_height\":" + String(cameraLastHeight) + ",";
  json += "\"temp_ok\":" + String(bmeOk ? "true" : "false") + ",";
  json += "\"temp_c\":" + String(bme.temperature, 4) + ",";
  json += "\"humidity\":" + String(bme.humidity, 4) + ",";
  json += "\"pressure_hpa\":" + String(bme.pressure, 4) + ",";
  json += "\"power_ok\":" + String(powerOk ? "true" : "false") + ",";
  json += "\"power_mw\":" + String(power.powerMw, 3) + ",";
  json += "\"solar_power_ok\":" + String(solarPowerOk ? "true" : "false") + ",";
  json += "\"solar_bus_voltage_v\":" + String(power.solarBusVoltage, 4) + ",";
  json += "\"solar_current_ma\":" + String(power.solarCurrentMa, 3) + ",";
  json += "\"solar_power_mw\":" + String(power.solarPowerMw, 3) + ",";
  json += "\"audio_ok\":" + String(audioOk ? "true" : "false") + ",";
  json += "\"audio_rms_db\":" + String(audio.rmsDb, 2);
  json += "}";
  return json;
}

static void bmeTask(void*) {
  TickType_t lastWake = xTaskGetTickCount();
  uint32_t sequence = 0;
  while (true) {
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(BME_INTERVAL_MS));
    if (otaInProgress) continue;
    BME280Reading reading = sensors.readClimate();
    if (!reading.ok) continue;
    BmeSample sample = {++sequence, nowUs(), reading.tempC, reading.humidity, reading.pressureHpa};
    bmeRing.push(sample);
    bmeProduced++;
    xSemaphoreTake(latestMutex, portMAX_DELAY);
    latestBme = sample;
    hasBme = true;
    xSemaphoreGive(latestMutex);
  }
}

static void powerTask(void*) {
  uint32_t sequence = 0;
  uint64_t previousTimeUs = 0;
  uint64_t nextSampleUs = nowUs();
  INA219Reading previousReading = {};
  bool havePrevious = false;
  while (true) {
    if (otaInProgress) {
      vTaskDelay(pdMS_TO_TICKS(10));
      nextSampleUs = nowUs();
      continue;
    }
    uint64_t currentUs = nowUs();
    if (currentUs < nextSampleUs) {
      uint32_t waitUs = static_cast<uint32_t>(nextSampleUs - currentUs);
      if (waitUs >= 1000) vTaskDelay(pdMS_TO_TICKS(waitUs / 1000));
      currentUs = nowUs();
      if (currentUs < nextSampleUs) delayMicroseconds(nextSampleUs - currentUs);
    } else if (currentUs - nextSampleUs > INA219_CONVERSION_US) {
      // Do not replay missed acquisition deadlines in a catch-up burst.
      nextSampleUs = currentUs;
    }
    nextSampleUs += INA219_CONVERSION_US;
    INA219Reading reading = sensors.readPower();
    INA219Reading solarReading = sensors.readSolarPower();
    if (!reading.ok) continue;
    uint64_t sampleTimeUs = nowUs();
    if (previousTimeUs) {
      uint32_t interval = static_cast<uint32_t>(sampleTimeUs - previousTimeUs);
      if (interval > powerIntervalMaxUs) powerIntervalMaxUs = interval;
    }
    if (havePrevious && reading.busVoltageV == previousReading.busVoltageV &&
        reading.currentMa == previousReading.currentMa && reading.powerMw == previousReading.powerMw) {
      powerDuplicates++;
    }
    previousTimeUs = sampleTimeUs;
    previousReading = reading;
    havePrevious = true;
    PowerSample sample = {
      ++sequence, sampleTimeUs, reading.busVoltageV, reading.currentMa, reading.powerMw,
      solarReading.busVoltageV, solarReading.currentMa, solarReading.powerMw
    };
    powerRing.push(sample);
    powerProduced++;
    xSemaphoreTake(latestMutex, portMAX_DELAY);
    latestPower = sample;
    hasPower = true;
    hasSolarPower = solarReading.ok;
    xSemaphoreGive(latestMutex);
  }
}

static void audioTask(void*) {
  uint32_t sequence = 0;
  uint32_t pcmSequence = 0;
  PcmPacket pcmPacket = {};
  size_t pcmCount = 0;
  while (true) {
    if (otaInProgress) {
      // This task owns I2S. Finish any active read, then stop the driver so
      // OTA never tears it down concurrently and DMA is quiet during flash.
      sensors.stopAudio();
      otaAudioStopped = true;
      while (otaInProgress) vTaskDelay(pdMS_TO_TICKS(10));
      if (!sensors.startAudio()) Serial.println("Audio restart failed after OTA error");
      otaAudioStopped = false;
      continue;
    }
    AudioObservables reading;
    if (!sensors.readAudio(reading)) continue;
    uint64_t frameEndUs = nowUs();
    AudioSample sample = {++sequence, frameEndUs, reading.rmsDb};
    audioRing.push(sample);
    audioProduced++;
    xSemaphoreTake(latestMutex, portMAX_DELAY);
    latestAudio = sample;
    hasAudio = true;
    xSemaphoreGive(latestMutex);

    if (!pcmStreamEnabled || !pcmQueue) {
      pcmCount = 0;
      continue;
    }
    if (pcmCount == 0) {
      pcmPacket.header = {
        {'E', 'S', 'A', 'U'}, 1, 1, 16, 0, ++pcmSequence,
        frameEndUs - static_cast<uint64_t>(reading.samples) * 1000000ULL / PCM_SAMPLE_RATE,
        PCM_SAMPLE_RATE, 0, sizeof(PcmPacketHeader), pcmQueueDrops
      };
    }
    size_t copyCount = min(static_cast<size_t>(reading.samples), PCM_SAMPLES_PER_PACKET - pcmCount);
    memcpy(pcmPacket.samples + pcmCount, reading.pcm16, copyCount * sizeof(int16_t));
    pcmCount += copyCount;
    if (pcmCount == PCM_SAMPLES_PER_PACKET) {
      pcmPacket.header.sampleCount = pcmCount;
      pcmPacket.header.queueDrops = pcmQueueDrops;
      if (xQueueSend(pcmQueue, &pcmPacket, 0) == pdTRUE) pcmPacketsQueued++;
      else pcmQueueDrops++;
      pcmCount = 0;
    }
  }
}

static void pcmTransportTask(void*) {
  PcmPacket packet;
  while (true) {
    if (xQueueReceive(pcmQueue, &packet, pdMS_TO_TICKS(100)) != pdTRUE) continue;
    if (otaInProgress || !pcmStreamEnabled) continue;
    size_t bytes = encodeImaAdpcm(packet, encodedPcmWork);
    if (WiFi.status() != WL_CONNECTED ||
        !pcmUdp.beginPacket(oscRouterIp, PCM_ROUTER_PORT) ||
        pcmUdp.write(reinterpret_cast<uint8_t*>(&encodedPcmWork), bytes) != bytes ||
        !pcmUdp.endPacket()) {
      pcmSendFailures++;
      if (++pcmConsecutiveFailures >= PCM_FAILURE_LIMIT) {
        setPcmStreamEnabled(false, true);
      }
    } else {
      pcmPacketsSent++;
      pcmConsecutiveFailures = 0;
    }
    taskYIELD();
  }
}

static void rateTask(void*) {
  uint32_t lastBme = 0, lastPower = 0, lastAudio = 0, lastPowerDuplicates = 0;
  uint64_t previous = nowUs();
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    uint64_t current = nowUs();
    float seconds = (current - previous) / 1000000.0f;
    uint32_t b = bmeProduced, p = powerProduced, a = audioProduced;
    uint32_t duplicates = powerDuplicates;
    uint32_t powerSamples = p - lastPower;
    bmeActualHz = (b - lastBme) / seconds;
    powerActualHz = (p - lastPower) / seconds;
    audioActualHz = (a - lastAudio) / seconds;
    powerIntervalAvgUs = powerSamples ? static_cast<uint16_t>(seconds * 1000000.0f / powerSamples) : 0;
    powerDuplicatePermille = powerSamples ? static_cast<uint16_t>(
      (duplicates - lastPowerDuplicates) * 1000UL / powerSamples) : 0;
    lastBme = b;
    lastPower = p;
    lastAudio = a;
    lastPowerDuplicates = duplicates;
    previous = current;
  }
}

static void setupOTA() {
  ArduinoOTA.setHostname("electric-sky");
  ArduinoOTA.setPort(3232);
  ArduinoOTA.onStart([]() {
    otaInProgress = true;
    uint32_t started = millis();
    while (!otaAudioStopped && millis() - started < 250) delay(1);
    Serial.printf("Audio stopped for OTA: %s\n", otaAudioStopped ? "yes" : "timeout");
    Serial.println("OTA start");
  });
  ArduinoOTA.onEnd([]() { Serial.println("\nOTA end"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA progress: %u%%\r", (progress * 100) / total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA error[%u]\n", error);
    otaInProgress = false;
  });
  ArduinoOTA.begin();
}

static bool buildBatch(TransportPacket& packet) {
  static uint32_t packetSequence = 0;
  uint32_t maxPowerIntervalUs = powerIntervalMaxUs;
  powerIntervalMaxUs = 0;
  BmeSample* bme = transportBmeBatch;
  PowerSample* power = transportPowerBatch;
  AudioSample* audio = transportAudioBatch;

  size_t bmeCount = bmeRing.pop(bme, MAX_BME_PER_PACKET);
  size_t powerCount = powerRing.pop(power, MAX_POWER_PER_PACKET);
  size_t audioCount = audioRing.pop(audio, MAX_AUDIO_PER_PACKET);
  if (bmeCount + powerCount + audioCount == 0) return false;

  PacketHeader header = {
    {'E', 'S', 'K', 'Y'}, 2, static_cast<uint8_t>(sensors.solarPowerAvailable() ? 0x01 : 0),
    sizeof(PacketHeader), ++packetSequence, nowUs(),
    static_cast<uint16_t>(bmeCount), static_cast<uint16_t>(powerCount),
    static_cast<uint16_t>(audioCount), 0,
    bmeRing.overruns(), powerRing.overruns(), audioRing.overruns(),
    static_cast<uint16_t>(bmeActualHz * 10), static_cast<uint16_t>(powerActualHz * 10),
    static_cast<uint16_t>(audioActualHz * 10), static_cast<uint16_t>(bmeRing.size()),
    static_cast<uint16_t>(powerRing.size()), static_cast<uint16_t>(audioRing.size()),
    static_cast<uint16_t>(uxQueueMessagesWaiting(transportQueue)), transportDrops,
    static_cast<uint16_t>(10000 / TRANSPORT_INTERVAL_MS), millis(), powerIntervalAvgUs,
    static_cast<uint16_t>(maxPowerIntervalUs > UINT16_MAX ? UINT16_MAX : maxPowerIntervalUs),
    powerDuplicatePermille, INA219_CONVERSION_US,
    static_cast<int16_t>(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0),
    wifiReconnects, oscSendFailures, oscSendAvgUs, oscSendMaxUs, oscSendStalls
  };

  size_t offset = 0;
  memcpy(packet.data + offset, &header, sizeof(header));
  offset += sizeof(header);
  memcpy(packet.data + offset, bme, bmeCount * sizeof(BmeSample));
  offset += bmeCount * sizeof(BmeSample);
  memcpy(packet.data + offset, power, powerCount * sizeof(PowerSample));
  offset += powerCount * sizeof(PowerSample);
  memcpy(packet.data + offset, audio, audioCount * sizeof(AudioSample));
  offset += audioCount * sizeof(AudioSample);
  packet.length = offset;
  return true;
}

static void transportTask(void*) {
  TickType_t lastWake = xTaskGetTickCount();
  while (true) {
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(TRANSPORT_INTERVAL_MS));
    if (otaInProgress || !buildBatch(transportWorkPacket)) continue;
    // OSC is the live art-data path and must not wait behind a blocked
    // dashboard WebSocket client in loop().
    sendOscBatches(transportWorkPacket);
    if (xQueueSend(transportQueue, &transportWorkPacket, 0) != pdTRUE) {
      xQueueReceive(transportQueue, &transportStalePacket, 0);
      transportDrops++;
      xQueueSend(transportQueue, &transportWorkPacket, 0);
    }
  }
}

static void webSocketEvent(uint8_t number, WStype_t type, uint8_t*, size_t) {
  if (type == WStype_CONNECTED) {
    webSocketClients |= static_cast<uint8_t>(1U << number);
    Serial.printf("[WS] client %u connected\n", number);
  }
  if (type == WStype_DISCONNECTED) {
    webSocketClients &= static_cast<uint8_t>(~(1U << number));
    Serial.printf("[WS] client %u disconnected\n", number);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  resetReason = esp_reset_reason();
  bootCount++;
  Serial.println("\n=================================");
  Serial.println(" Electric Sky Firmware");
  Serial.printf(" Git: %s%s  Built: %s\n", FW_GIT_SHA, FW_GIT_DIRTY ? "-dirty" : "", FW_BUILD_UTC);
  Serial.printf(" Boot: %lu  Reset: %s (%d)\n", static_cast<unsigned long>(bootCount),
    resetReasonName(resetReason), static_cast<int>(resetReason));
  Serial.println("=================================");

  latestMutex = xSemaphoreCreateMutex();
  transportQueueStorage = static_cast<uint8_t*>(heap_caps_malloc(
    TRANSPORT_QUEUE_DEPTH * sizeof(TransportPacket), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  transportQueue = transportQueueStorage ? xQueueCreateStatic(TRANSPORT_QUEUE_DEPTH,
    sizeof(TransportPacket), transportQueueStorage, &transportQueueControl) : nullptr;
  pcmQueueStorage = static_cast<uint8_t*>(heap_caps_malloc(
    PCM_QUEUE_DEPTH * sizeof(PcmPacket), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  pcmQueue = pcmQueueStorage ? xQueueCreateStatic(PCM_QUEUE_DEPTH,
    sizeof(PcmPacket), pcmQueueStorage, &pcmQueueControl) : nullptr;
  if (!latestMutex || !transportQueue || !pcmQueue || !sensors.begin()) {
    Serial.println("Sensor initialization failed. Halting.");
    while (true) delay(1000);
  }

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  bool connected = connectPreferredWifi(20);
  if (connected) {
    Serial.printf("WiFi: %s  IP: %s  RSSI: %d\n", WiFi.SSID().c_str(),
      WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }
  WiFi.setSleep(false);
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  MDNS.begin("electric-sky");
  if (connected) resolveRouterIp();
  setupOTA();

  server.on("/", []() {
    String page(DASHBOARD_HTML);
    page.replace("{{IP}}", WiFi.localIP().toString());
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", page);
    server.client().stop();
  });
  server.on("/status", []() {
    bool ok;
    String json = makeStatusJson(&ok);
    server.sendHeader("Connection", "close");
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(ok ? 200 : 503, "application/json", json);
    server.client().stop();
  });
  server.on("/camera", []() {
    server.sendHeader("Location", "/#camera");
    server.send(302, "text/plain", "camera moved to dashboard");
    server.client().stop();
  });
  server.on("/camera.jpg", []() {
    uint32_t started = millis();
    camera_fb_t* frame = sensors.captureCamera();
    if (!frame) {
      cameraFailures++;
      server.sendHeader("Connection", "close");
      server.send(503, "text/plain", "camera capture failed");
      server.client().stop();
      return;
    }
    cameraCaptures++;
    cameraLastBytes = frame->len;
    cameraLastCaptureMs = millis() - started;
    cameraLastWidth = frame->width;
    cameraLastHeight = frame->height;
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    server.sendHeader("Connection", "close");
    server.setContentLength(frame->len);
    server.send(200, "image/jpeg", "");
    server.client().write(frame->buf, frame->len);
    sensors.releaseCamera(frame);
    server.client().stop();
  });
  server.on("/audio/raw", []() {
    if (server.hasArg("enabled")) {
      String value = server.arg("enabled");
      setPcmStreamEnabled(value == "1" || value == "true" || value == "on");
    }
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", String("{\"enabled\":") +
      (pcmStreamEnabled ? "true" : "false") +
      ",\"auto_disabled\":" + (pcmAutoDisabled ? "true" : "false") + "}");
  });
  server.on("/restart", []() {
    server.send(200, "text/plain", "restarting");
    server.client().stop();
    delay(200);
    ESP.restart();
  });
  server.on("/favicon.ico", []() { server.send(204); });
  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  webSocket.enableHeartbeat(5000, 1000, 1);

  // Network delivery and OTA must preempt acquisition briefly. Every loop
  // iteration yields, so sensor tasks still run while packet timing remains
  // regular under the continuous 1 kHz power and 250 Hz audio workloads.
  vTaskPrioritySet(nullptr, 4);

  // Start acquisition only after transport is ready so setup delays cannot
  // fill the rings and create artificial sequence gaps at boot.
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 3, &audioTaskHandle, 1);
  xTaskCreatePinnedToCore(pcmTransportTask, "pcm-net", 4096, nullptr, 3, &pcmTaskHandle, 1);
  xTaskCreatePinnedToCore(bmeTask, "bme", 4096, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(powerTask, "power", 4096, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(rateTask, "rates", 3072, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(transportTask, "transport", 12288, nullptr, 4, &transportTaskHandle, 1);
  Serial.println("Dashboard: http://electric-sky.local/");
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  webSocket.loop();

  TransportPacket packet;
  if (!otaInProgress && xQueueReceive(transportQueue, &packet, 0) == pdTRUE) {
    uint8_t clients = webSocketClients;
    for (uint8_t client = 0; client < WEBSOCKETS_SERVER_CLIENT_MAX; client++) {
      if (!(clients & (1U << client))) continue;
      if (!webSocket.clientIsConnected(client) ||
          !webSocket.sendBIN(client, packet.data, packet.length)) {
        webSocket.disconnect(client);
        webSocketClients &= static_cast<uint8_t>(~(1U << client));
      }
    }
  }

  static uint32_t lastWifiCheck = 0;
  static uint32_t lastPriorityScan = 0;
  static uint32_t wifiOfflineSince = 0;
  const uint32_t nowMs = millis();
  const bool wifiUsable = WiFi.status() == WL_CONNECTED &&
    static_cast<uint32_t>(WiFi.localIP()) != 0;

  if (wifiUsable) {
    wifiOfflineSince = 0;
    if (!otaInProgress &&
        nowMs - lastPriorityScan >= WIFI_PRIORITY_SCAN_INTERVAL_MS) {
      lastPriorityScan = nowMs;
      if (higherPriorityWifiVisible()) {
        wifiReconnects++;
        if (connectPreferredWifi(12)) resolveRouterIp();
      }
    }
  } else {
    if (wifiOfflineSince == 0) wifiOfflineSince = nowMs;
    if (!otaInProgress && nowMs - lastWifiCheck >= WIFI_RETRY_INTERVAL_MS) {
      lastWifiCheck = nowMs;
      wifiReconnects++;
      if (connectPreferredWifi(12)) resolveRouterIp();
    }
    if (!otaInProgress && nowMs - wifiOfflineSince >= WIFI_OFFLINE_RESTART_MS) {
      Serial.println("WiFi offline for 10 minutes; restarting for recovery");
      delay(100);
      ESP.restart();
    }
  }
  delay(1);
}
