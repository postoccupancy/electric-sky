#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
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

const char* WIFI_SSID = "Knight-MacDonald_EXT";
const char* WIFI_SSID_FB = "Knight-MacDonald";
const char* WIFI_PASSWORD = "409Jasper!";

constexpr uint32_t BME_INTERVAL_MS = 10;
constexpr uint32_t POWER_INTERVAL_MS = 1;
constexpr uint32_t TRANSPORT_INTERVAL_MS = 34;
constexpr size_t MAX_BME_PER_PACKET = 8;
constexpr size_t MAX_POWER_PER_PACKET = 64;
constexpr size_t MAX_AUDIO_PER_PACKET = 16;
constexpr size_t TRANSPORT_PACKET_BYTES = 2048;
constexpr size_t TRANSPORT_QUEUE_DEPTH = 4;

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
} __attribute__((packed));

struct TransportPacket {
  uint16_t length;
  uint8_t data[TRANSPORT_PACKET_BYTES];
};

static_assert(sizeof(BmeSample) == 24, "BME wire format changed");
static_assert(sizeof(PowerSample) == 24, "power wire format changed");
static_assert(sizeof(AudioSample) == 16, "audio wire format changed");
static_assert(sizeof(PacketHeader) == 58, "packet header changed");
static_assert(sizeof(PacketHeader) + MAX_BME_PER_PACKET * sizeof(BmeSample) +
  MAX_POWER_PER_PACKET * sizeof(PowerSample) + MAX_AUDIO_PER_PACKET * sizeof(AudioSample)
  <= TRANSPORT_PACKET_BYTES, "transport packet buffer too small");

WebServer server(80);
WebSocketsServer webSocket(81);
SensorManager sensors;

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

static SemaphoreHandle_t latestMutex;
static QueueHandle_t transportQueue;
static volatile uint32_t transportDrops = 0;
static BmeSample latestBme = {};
static PowerSample latestPower = {};
static AudioSample latestAudio = {};
static bool hasBme = false;
static bool hasPower = false;
static bool hasAudio = false;

static uint64_t nowUs() {
  return static_cast<uint64_t>(esp_timer_get_time());
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
  bool bmeOk, powerOk, audioOk;

  xSemaphoreTake(latestMutex, portMAX_DELAY);
  bme = latestBme;
  power = latestPower;
  audio = latestAudio;
  bmeOk = hasBme;
  powerOk = hasPower;
  audioOk = hasAudio;
  xSemaphoreGive(latestMutex);

  bool ok = bmeOk && powerOk && audioOk;
  if (okOut) *okOut = ok;

  String json = "{";
  json += "\"firmware_git_sha\":\"" FW_GIT_SHA "\",";
  json += "\"firmware_git_dirty\":" + String(FW_GIT_DIRTY ? "true" : "false") + ",";
  json += "\"firmware_build_utc\":\"" FW_BUILD_UTC "\",";
  json += "\"uptime_ms\":" + String(millis()) + ",";
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
  json += "\"temp_ok\":" + String(bmeOk ? "true" : "false") + ",";
  json += "\"temp_c\":" + String(bme.temperature, 4) + ",";
  json += "\"humidity\":" + String(bme.humidity, 4) + ",";
  json += "\"pressure_hpa\":" + String(bme.pressure, 4) + ",";
  json += "\"power_ok\":" + String(powerOk ? "true" : "false") + ",";
  json += "\"power_mw\":" + String(power.powerMw, 3) + ",";
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
  TickType_t lastWake = xTaskGetTickCount();
  uint32_t sequence = 0;
  while (true) {
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(POWER_INTERVAL_MS));
    if (otaInProgress) continue;
    INA219Reading reading = sensors.readPower();
    if (!reading.ok) continue;
    PowerSample sample = {++sequence, nowUs(), reading.busVoltageV, reading.currentMa, reading.powerMw};
    powerRing.push(sample);
    powerProduced++;
    xSemaphoreTake(latestMutex, portMAX_DELAY);
    latestPower = sample;
    hasPower = true;
    xSemaphoreGive(latestMutex);
  }
}

static void audioTask(void*) {
  uint32_t sequence = 0;
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
    AudioSample sample = {++sequence, nowUs(), reading.rmsDb};
    audioRing.push(sample);
    audioProduced++;
    xSemaphoreTake(latestMutex, portMAX_DELAY);
    latestAudio = sample;
    hasAudio = true;
    xSemaphoreGive(latestMutex);
  }
}

static void rateTask(void*) {
  uint32_t lastBme = 0, lastPower = 0, lastAudio = 0;
  uint64_t previous = nowUs();
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    uint64_t current = nowUs();
    float seconds = (current - previous) / 1000000.0f;
    uint32_t b = bmeProduced, p = powerProduced, a = audioProduced;
    bmeActualHz = (b - lastBme) / seconds;
    powerActualHz = (p - lastPower) / seconds;
    audioActualHz = (a - lastAudio) / seconds;
    lastBme = b;
    lastPower = p;
    lastAudio = a;
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
  BmeSample bme[MAX_BME_PER_PACKET];
  PowerSample power[MAX_POWER_PER_PACKET];
  AudioSample audio[MAX_AUDIO_PER_PACKET];

  size_t bmeCount = bmeRing.pop(bme, MAX_BME_PER_PACKET);
  size_t powerCount = powerRing.pop(power, MAX_POWER_PER_PACKET);
  size_t audioCount = audioRing.pop(audio, MAX_AUDIO_PER_PACKET);
  if (bmeCount + powerCount + audioCount == 0) return false;

  PacketHeader header = {
    {'E', 'S', 'K', 'Y'}, 1, 0, sizeof(PacketHeader), ++packetSequence, nowUs(),
    static_cast<uint16_t>(bmeCount), static_cast<uint16_t>(powerCount),
    static_cast<uint16_t>(audioCount), 0,
    bmeRing.overruns(), powerRing.overruns(), audioRing.overruns(),
    static_cast<uint16_t>(bmeActualHz * 10), static_cast<uint16_t>(powerActualHz * 10),
    static_cast<uint16_t>(audioActualHz * 10), static_cast<uint16_t>(bmeRing.size()),
    static_cast<uint16_t>(powerRing.size()), static_cast<uint16_t>(audioRing.size()),
    static_cast<uint16_t>(uxQueueMessagesWaiting(transportQueue)), transportDrops
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
  TransportPacket packet;
  TransportPacket stale;
  while (true) {
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(TRANSPORT_INTERVAL_MS));
    if (otaInProgress || !buildBatch(packet)) continue;
    if (xQueueSend(transportQueue, &packet, 0) != pdTRUE) {
      xQueueReceive(transportQueue, &stale, 0);
      transportDrops++;
      xQueueSend(transportQueue, &packet, 0);
    }
  }
}

static void webSocketEvent(uint8_t number, WStype_t type, uint8_t*, size_t) {
  if (type == WStype_CONNECTED) Serial.printf("[WS] client %u connected\n", number);
  if (type == WStype_DISCONNECTED) Serial.printf("[WS] client %u disconnected\n", number);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=================================");
  Serial.println(" Electric Sky Firmware");
  Serial.printf(" Git: %s%s  Built: %s\n", FW_GIT_SHA, FW_GIT_DIRTY ? "-dirty" : "", FW_BUILD_UTC);
  Serial.println("=================================");

  latestMutex = xSemaphoreCreateMutex();
  transportQueue = xQueueCreate(TRANSPORT_QUEUE_DEPTH, sizeof(TransportPacket));
  if (!latestMutex || !transportQueue || !sensors.begin()) {
    Serial.println("Sensor initialization failed. Halting.");
    while (true) delay(1000);
  }

  auto tryConnect = [](const char* ssid, const char* password, int attempts) {
    WiFi.begin(ssid, password);
    for (int i = 0; i < attempts && WiFi.status() != WL_CONNECTED; i++) delay(500);
    return WiFi.status() == WL_CONNECTED;
  };

  bool connected = tryConnect(WIFI_SSID, WIFI_PASSWORD, 20);
  if (!connected) {
    WiFi.disconnect(true);
    delay(500);
    connected = tryConnect(WIFI_SSID_FB, WIFI_PASSWORD, 20);
  }
  if (connected) {
    Serial.printf("WiFi: %s  IP: %s  RSSI: %d\n", WiFi.SSID().c_str(),
      WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }
  WiFi.setSleep(false);
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  MDNS.begin("electric-sky");
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

  // Network delivery and OTA must preempt acquisition briefly. Every loop
  // iteration yields, so sensor tasks still run while packet timing remains
  // regular under the continuous 1 kHz power and 250 Hz audio workloads.
  vTaskPrioritySet(nullptr, 4);

  // Start acquisition only after transport is ready so setup delays cannot
  // fill the rings and create artificial sequence gaps at boot.
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(bmeTask, "bme", 4096, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(powerTask, "power", 4096, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(rateTask, "rates", 3072, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(transportTask, "transport", 8192, nullptr, 4, nullptr, 1);
  Serial.println("Dashboard: http://electric-sky.local/");
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  webSocket.loop();

  TransportPacket packet;
  if (!otaInProgress && xQueueReceive(transportQueue, &packet, 0) == pdTRUE) {
    webSocket.broadcastBIN(packet.data, packet.length);
  }

  static uint32_t lastWifiCheck = 0;
  static uint8_t wifiFailCount = 0;
  if (!otaInProgress && millis() - lastWifiCheck >= 30000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      const char* ssid = (++wifiFailCount % 2 == 0) ? WIFI_SSID_FB : WIFI_SSID;
      WiFi.disconnect(true);
      delay(200);
      WiFi.begin(ssid, WIFI_PASSWORD);
    } else {
      wifiFailCount = 0;
    }
  }
  delay(1);
}
