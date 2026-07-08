#include <Arduino.h>
#include <ArduinoOTA.h>
#include "SensorManager.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <time.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

const char* WIFI_SSID = "Knight-MacDonald";
const char* WIFI_PASSWORD = "409Jasper!";

WebServer server(80);
WebSocketsServer webSocket(81);
SensorManager sensors;

static SemaphoreHandle_t frameMutex;
static SensorFrame latestFrame;
static bool hasFrame = false;

volatile bool otaInProgress = false;

// --- helpers ---

static String isoTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 10)) return "";
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buf);
}

static bool getLatestFrameCopy(SensorFrame& out) {
  xSemaphoreTake(frameMutex, portMAX_DELAY);
  bool ok = hasFrame;
  if (ok) out = latestFrame;
  xSemaphoreGive(frameMutex);
  return ok;
}

static String makeStatusJson(bool* okOut = nullptr) {
  SensorFrame frame;
  bool ok = getLatestFrameCopy(frame);

  if (okOut) *okOut = ok;

  if (!ok) return "{\"error\":\"no frame yet\"}";

  String json = "{";
  json += "\"frame\":" + String(frame.frameId) + ",";
  json += "\"uptime_ms\":" + String(millis()) + ",";
  json += "\"timestamp\":\"" + isoTime() + "\",";
  json += "\"temp_ok\":" + String(frame.climate.ok ? "true" : "false") + ",";
  json += "\"temp_c\":" + String(frame.climate.tempC, 2) + ",";
  json += "\"humidity\":" + String(frame.climate.humidity, 2) + ",";
  json += "\"pressure_hpa\":" + String(frame.climate.pressureHpa, 2) + ",";
  json += "\"power_ok\":" + String(frame.power.ok ? "true" : "false") + ",";
  json += "\"power_mw\":" + String(frame.power.powerMw, 3) + ",";
  json += "\"audio_rms_db\":" + String(frame.audio.rmsDb, 2);
  json += "}";
  return json;
}

// --- OTA ---

void setupOTA() {
  ArduinoOTA.setHostname("electric-sky");
  ArduinoOTA.setPort(3232);

  ArduinoOTA.onStart([]() {
    otaInProgress = true;
    sensors.stopForOTA();
    Serial.println("OTA start");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA end");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA progress: %u%%\r", (progress * 100) / total);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive failed");
    else if (error == OTA_END_ERROR) Serial.println("End failed");
    otaInProgress = false;
  });

  ArduinoOTA.begin();

  Serial.println("OTA ready");
  Serial.println("OTA hostname: electric-sky.local");
}

// --- sensor task ---

static void sensorTask(void*) {
  const TickType_t interval = pdMS_TO_TICKS(5000);
  TickType_t lastWake = xTaskGetTickCount();

  while (true) {
    vTaskDelayUntil(&lastWake, interval);

    if (otaInProgress) continue;

    SensorFrame f = sensors.read();

    xSemaphoreTake(frameMutex, portMAX_DELAY);
    latestFrame = f;
    hasFrame = true;
    xSemaphoreGive(frameMutex);
  }
}

// --- WebSocket ---

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      Serial.printf("[WS] client %u connected\n", num);
      String json = makeStatusJson();
      webSocket.sendTXT(num, json);
      break;
    }

    case WStype_DISCONNECTED:
      Serial.printf("[WS] client %u disconnected\n", num);
      break;

    default:
      break;
  }
}

// --- setup / loop ---

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=================================");
  Serial.println(" Electric Sky Firmware");
  Serial.println("=================================");

  if (!sensors.begin()) {
    Serial.println("Sensor initialization failed. Halting.");
    while (true) delay(1000);
  }

  Serial.println("System ready.");

  frameMutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(sensorTask, "sensors", 8192, nullptr, 1, nullptr, 1);

  Serial.print("Connecting to WiFi");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.println(WiFi.RSSI());
  } else {
    Serial.println("WiFi connection failed");
  }

  WiFi.setSleep(false);

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  if (MDNS.begin("electric-sky")) {
    Serial.println("mDNS started: http://electric-sky.local/status");
  }

  setupOTA();

  server.on("/status", []() {
    bool ok = false;
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

  server.on("/favicon.ico", []() {
    server.send(204);
    server.client().stop();
  });

  server.begin();
  Serial.println("HTTP server started");
  Serial.println("Open: http://" + WiFi.localIP().toString() + "/status");

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket started: ws://electric-sky.local:81/");
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  webSocket.loop();

  static unsigned long lastWsBroadcast = 0;
  const unsigned long WS_BROADCAST_MS = 500;

  if (millis() - lastWsBroadcast >= WS_BROADCAST_MS) {
    lastWsBroadcast = millis();
    String json = makeStatusJson();
    webSocket.broadcastTXT(json);
  }

  // Reconnect if WiFi drops; skip during OTA to avoid disrupting the transfer
  static unsigned long lastWifiCheck = 0;
  if (!otaInProgress && millis() - lastWifiCheck >= 30000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
    }
  }

  delay(1);
}
