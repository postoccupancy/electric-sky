// main_esp32s3cam.ino
// ESP32-S3-CAM (Freenove) — BME280 + INMP441 + OV2640 camera → POST to esp32_api
//
// Arduino IDE board settings:
//   Board:            ESP32S3 Dev Module
//   PSRAM:            OPI PSRAM
//   Flash Size:       16MB (or 8MB if your board is 8MB)
//   Partition Scheme: Huge APP (3MB No OTA / 1MB SPIFFS)
//   Upload Speed:     921600
//
// Required libraries (install via Arduino Library Manager):
//   Adafruit BME280 Library
//   Adafruit Unified Sensor
//   (esp_camera and I2S are part of the ESP32 Arduino core — no install needed)

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "esp_camera.h"
#include "driver/i2s.h"
#include "secrets.h"

// ============================================================
// Pin definitions — Freenove ESP32-S3-CAM (CAMERA_MODEL_ESP32S3_EYE)
// ============================================================

// Camera (OV2640) — DO NOT use these GPIOs for anything else
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  15
#define SIOD_GPIO_NUM  4
#define SIOC_GPIO_NUM  5
#define Y9_GPIO_NUM    16
#define Y8_GPIO_NUM    17
#define Y7_GPIO_NUM    18
#define Y6_GPIO_NUM    12
#define Y5_GPIO_NUM    10
#define Y4_GPIO_NUM    8
#define Y3_GPIO_NUM    9
#define Y2_GPIO_NUM    11
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM  7
#define PCLK_GPIO_NUM  13

// BME280 on I2C (same as MicroPython firmware)
#define I2C_SDA 41
#define I2C_SCL 42

// INMP441 I2S (confirmed working pins from MicroPython session)
#define I2S_SCK_PIN  2
#define I2S_WS_PIN   1
#define I2S_SD_PIN   21
#define I2S_PORT     I2S_NUM_0

// ============================================================
// Config
// ============================================================
#define SENSOR_INTERVAL_MS  2000     // BME280 + audio POST every 2 seconds
#define CAMERA_INTERVAL_MS  30000    // Camera capture every 30 seconds
#define I2S_SAMPLE_RATE     16000
#define I2S_READ_SAMPLES    512      // samples per read

// ============================================================
// Globals
// ============================================================
Adafruit_BME280 bme;
bool cameraReady = false;
float lastLuminance = -1.0;  // -1 = no valid frame yet

// ============================================================
// Camera
// ============================================================
bool cameraSetup() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  // Grayscale at QQVGA (160x120 = 19200 bytes): fast, easy luminance calculation
  config.pixel_format = PIXFORMAT_GRAYSCALE;
  config.frame_size   = FRAMESIZE_QQVGA;
  config.fb_count     = 1;
  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.grab_mode    = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }
  Serial.println("Camera OK");
  return true;
}

// Returns mean pixel brightness (0–255), or -1 on failure.
// In grayscale QQVGA each byte is one pixel.
float captureLuminance() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return -1.0;
  }
  uint64_t sum = 0;
  for (size_t i = 0; i < fb->len; i++) sum += fb->buf[i];
  float mean = (float)sum / fb->len;
  esp_camera_fb_return(fb);
  return mean;
}

// ============================================================
// INMP441 I2S
// ============================================================
void i2sSetup() {
  i2s_config_t cfg = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate          = I2S_SAMPLE_RATE,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
    // L/R pin tied to GND on breakout = left channel
    .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 4,
    .dma_buf_len          = 64,
    .use_apll             = false,
    .tx_desc_auto_clear   = false,
    .fixed_mclk           = 0,
  };
  i2s_pin_config_t pins = {
    .bck_io_num   = I2S_SCK_PIN,
    .ws_io_num    = I2S_WS_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = I2S_SD_PIN,
  };
  i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_PORT, &pins);
}

// Returns RMS level in dBFS.
float readRmsDb() {
  int32_t samples[I2S_READ_SAMPLES];
  size_t bytes_read = 0;
  i2s_read(I2S_PORT, samples, sizeof(samples), &bytes_read, pdMS_TO_TICKS(100));
  int count = bytes_read / sizeof(int32_t);
  if (count == 0) return -120.0;

  double sum_sq = 0;
  for (int i = 0; i < count; i++) {
    // INMP441: 24-bit data packed in upper bits of 32-bit word
    float s = (float)(samples[i] >> 8) / (float)(1 << 23);
    sum_sq += s * s;
  }
  float rms = sqrt(sum_sq / count);
  if (rms < 1e-10) return -120.0;
  return 20.0 * log10(rms);
}

// ============================================================
// HTTP POST
// ============================================================
void postData(float temp_c, float temp_f, float rh, float pres,
              float rms_db, float luminance) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost, skipping POST");
    return;
  }
  HTTPClient http;
  http.begin(SERVER_ENDPOINT);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Ingest-Token", TOKEN);

  String body = "{";
  body += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
  body += "\"temp_c\":"     + String(temp_c, 1) + ",";
  body += "\"temp_f\":"     + String(temp_f, 1) + ",";
  body += "\"rh\":"         + String(rh, 1)     + ",";
  body += "\"pres\":"       + String(pres, 1);
  if (rms_db > -119.0) {
    body += ",\"rms_db\":"    + String(rms_db, 1);
  }
  if (luminance >= 0) {
    body += ",\"luminance\":" + String(luminance, 1);
  }
  body += "}";

  int status = http.POST(body);
  Serial.printf("POST %d — %s\n", status, body.c_str());
  http.end();
}

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32-S3-CAM booting ===");

  // BME280
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!bme.begin(0x76, &Wire)) {
    Serial.println("ERROR: BME280 not found. Check GPIO41/42 wiring.");
    while (1) delay(1000);
  }
  Serial.println("BME280 OK (0x76)");

  // INMP441
  i2sSetup();
  Serial.println("INMP441 I2S OK");

  // Camera
  cameraReady = cameraSetup();
  if (!cameraReady) {
    Serial.println("WARNING: Camera not available. Continuing without it.");
  }

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi connecting");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 50) {
    delay(300);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nConnected: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nWiFi failed — will retry in loop");
  }

  // NTP (UTC)
  configTime(0, 0, "pool.ntp.org");
}

// ============================================================
// Loop
// ============================================================
unsigned long lastSensor = 0;
unsigned long lastCamera = 0;

void loop() {
  unsigned long now = millis();

  // Camera every 30 seconds
  if (cameraReady && (lastCamera == 0 || now - lastCamera >= CAMERA_INTERVAL_MS)) {
    lastLuminance = captureLuminance();
    Serial.printf("Luminance: %.1f\n", lastLuminance);
    lastCamera = now;
  }

  // Sensor POST every 2 seconds
  if (lastSensor == 0 || now - lastSensor >= SENSOR_INTERVAL_MS) {
    float temp_c = bme.readTemperature();
    float temp_f = temp_c * 9.0 / 5.0 + 32.0;
    float rh     = bme.readHumidity();
    float pres   = bme.readPressure() / 100.0;
    float rms_db = readRmsDb();

    Serial.printf("T=%.1fC  RH=%.1f%%  P=%.1fhPa  RMS=%.1fdB  Lum=%.1f\n",
                  temp_c, rh, pres, rms_db, lastLuminance);

    postData(temp_c, temp_f, rh, pres, rms_db, lastLuminance);
    lastSensor = now;
  }
}
