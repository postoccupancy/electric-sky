#include <Arduino.h>
#include <ArduinoOTA.h>
#include "SensorManager.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <time.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

const char* WIFI_SSID      = "Knight-MacDonald";
const char* WIFI_SSID_FB   = "Knight-MacDonald_EXT";  // fallback
const char* WIFI_PASSWORD  = "409Jasper!";

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

  auto tryConnect = [](const char* ssid, const char* pass, int maxAttempts) -> bool {
    Serial.printf("Connecting to %s", ssid);
    WiFi.begin(ssid, pass);
    for (int i = 0; i < maxAttempts; i++) {
      if (WiFi.status() == WL_CONNECTED) break;
      delay(500);
      Serial.print(".");
    }
    Serial.println();
    return WiFi.status() == WL_CONNECTED;
  };

  bool connected = tryConnect(WIFI_SSID, WIFI_PASSWORD, 20);
  if (!connected) {
    Serial.println("Primary failed, trying fallback");
    WiFi.disconnect(true);
    delay(500);
    connected = tryConnect(WIFI_SSID_FB, WIFI_PASSWORD, 20);
  }

  if (connected) {
    Serial.printf("WiFi connected: %s  IP: %s  RSSI: %d\n",
      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.println("WiFi connection failed on both SSIDs");
  }

  WiFi.setSleep(false);

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  if (MDNS.begin("electric-sky")) {
    Serial.println("mDNS started: http://electric-sky.local/status");
  }

  setupOTA();

  server.on("/", []() {
    static const char html[] PROGMEM = R"(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Electric Sky</title>
<style>
body{font-family:monospace;background:#111;color:#eee;padding:2rem;max-width:480px}
h1{color:#adf;margin:0 0 1.5rem;font-size:1.4rem;letter-spacing:.05em}
.row{display:flex;justify-content:space-between;align-items:baseline;padding:.45rem 0;border-bottom:1px solid #222}
.label{color:#888;font-size:.85rem}
.value{color:#fff;font-size:1.1rem}
.unit{color:#666;font-size:.8rem;margin-left:.3rem}
#status{font-size:.8rem;color:#aaa;margin-top:1.2rem}
.dot{display:inline-block;width:6px;height:6px;border-radius:50%;background:#4f4;margin-right:.4rem;vertical-align:middle}
.dot.err{background:#f44}
</style>
</head>
<body>
<h1>Electric Sky</h1>
<div id="rows"></div>
<div id="status">connecting...</div>
<script>
function fmt_uptime(ms){
  var s=Math.floor(ms/1000),h=Math.floor(s/3600),m=Math.floor((s%3600)/60),sec=s%60;
  return (h?h+'h ':'')+m+'m '+sec+'s';
}
var rows=document.getElementById('rows');
var status=document.getElementById('status');
var computed={};

function addRow(id,label,unit){
  var d=document.createElement('div');
  d.className='row';
  d.innerHTML='<span class="label">'+label+'</span><span><span class="value" id="v_'+id+'">—</span><span class="unit">'+unit+'</span></span>';
  rows.appendChild(d);
  return document.getElementById('v_'+id);
}

var e_tempC=addRow('temp_c','Temperature','°C');
var e_tempF=addRow('temp_f','Temperature','°F');
var e_hum=addRow('humidity','Humidity','%');
var e_pres=addRow('pressure','Pressure','hPa');
var e_power=addRow('power_mw','Solar power','mW');
var e_audio=addRow('audio_rms_db','Audio RMS','dBFS');
var e_uptime=addRow('uptime','Since last boot','');
var e_reading=addRow('frame','Sensor reading','');
var e_ts=addRow('timestamp','Timestamp','UTC');

function connect(){
  var ws=new WebSocket('ws://'+location.hostname+':81/');
  ws.onopen=function(){status.innerHTML='<span class="dot"></span>live';};
  ws.onmessage=function(e){
    var d=JSON.parse(e.data);
    if(d.error){status.innerHTML='<span class="dot err"></span>'+d.error;return;}
    e_tempC.textContent=d.temp_c.toFixed(2);
    e_tempF.textContent=(d.temp_c*9/5+32).toFixed(2);
    e_hum.textContent=d.humidity.toFixed(2);
    e_pres.textContent=d.pressure_hpa.toFixed(2);
    e_power.textContent=d.power_mw.toFixed(0);
    e_audio.textContent=d.audio_rms_db.toFixed(1);
    e_uptime.textContent=fmt_uptime(d.uptime_ms);
    e_reading.textContent='#'+d.frame+' (every 5s)';
    e_ts.textContent=d.timestamp?d.timestamp.replace('T',' ').replace('Z',''):'—';
    status.innerHTML='<span class="dot"></span>live &mdash; updates every 500ms';
  };
  ws.onclose=function(){status.innerHTML='<span class="dot err"></span>reconnecting...';setTimeout(connect,2000);};
  ws.onerror=function(){ws.close();};
}
connect();
</script>
</body>
</html>)";
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", html);
    server.client().stop();
  });

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
  static uint8_t wifiFailCount = 0;
  if (!otaInProgress && millis() - lastWifiCheck >= 30000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      wifiFailCount++;
      const char* ssid = (wifiFailCount % 2 == 0) ? WIFI_SSID_FB : WIFI_SSID;
      WiFi.disconnect(true);
      delay(200);
      WiFi.begin(ssid, WIFI_PASSWORD);
      Serial.printf("WiFi reconnect attempt %u on %s\n", wifiFailCount, ssid);
    } else {
      wifiFailCount = 0;
    }
  }

  delay(1);
}
