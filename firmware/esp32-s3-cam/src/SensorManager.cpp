#include "SensorManager.h"
#include <Wire.h>

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
  if (!camera.begin()) {
    Serial.println("  Camera FAILED");
    return false;
  }
  Serial.println("  Camera OK");

  camera_fb_t* fb = camera.capture();
  if (fb) {
    Serial.printf("  Camera captured: %u bytes, %u x %u\n", fb->len, fb->width, fb->height);
    camera.release(fb);
  } else {
    Serial.println("  Camera capture FAILED");
  }

  Serial.println("Initializing I2C...");
  Wire.begin(I2C_SDA, I2C_SCL);
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

SensorFrame SensorManager::read() {
  SensorFrame frame;

  frame.frameId = ++frameId;
  frame.timestampMs = millis();

  frame.power = ina.read();
  frame.climate = bme.read();
  mic.read(frame.audio);

  return frame;
}