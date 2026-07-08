#include "SensorManager.h"
#include <Wire.h>
#include <driver/i2s.h>

#define I2C_SDA 42
#define I2C_SCL 41

void SensorManager::scanI2C() {
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  I2C found: 0x%02X (%u)\n", addr, addr);
    }
  }
}

bool SensorManager::begin() {
  Serial.println("Initializing Camera...");
  _cameraOk = camera.begin();
  if (!_cameraOk) {
    Serial.println("  Camera FAILED — continuing without camera");
  } else {
    Serial.println("  Camera OK");
    camera_fb_t* fb = camera.capture();
    if (fb) {
      Serial.printf("  Camera captured: %u bytes, %u x %u\n", fb->len, fb->width, fb->height);
      camera.release(fb);
    }
  }

  Serial.println("Initializing I2C...");
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setTimeOut(50);
  scanI2C();

  Serial.println("Initializing INA219...");
  if (!ina.begin(Wire)) {
    Serial.println("  INA219 FAILED");
    return false;
  }
  Serial.println("  INA219 OK");

  Serial.println("Initializing BME280...");
  if (!bme.begin(Wire)) {
    Serial.println("  BME280 FAILED");
    return false;
  }
  Serial.println("  BME280 OK");

  Serial.println("Initializing INMP441...");
  if (!mic.begin()) {
    Serial.println("  INMP441 FAILED");
    return false;
  }
  Serial.println("  INMP441 OK");

  return true;
}

void SensorManager::stopForOTA() {
  i2s_driver_uninstall(I2S_NUM_0);
}

SensorFrame SensorManager::read() {
  SensorFrame frame;

  frame.frameId = ++frameId;
  frame.timestampMs = millis();

  unsigned long t0 = millis();
  frame.power = ina.read();
  unsigned long t1 = millis();
  frame.climate = bme.read();
  unsigned long t2 = millis();
  mic.read(frame.audio);
  unsigned long t3 = millis();

  Serial.printf("sensor timing — ina:%lums bme:%lums mic:%lums\n",
    t1 - t0, t2 - t1, t3 - t2);

  return frame;
}