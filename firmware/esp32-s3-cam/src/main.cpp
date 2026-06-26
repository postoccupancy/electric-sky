#include <Arduino.h>
#include <Wire.h>
#include "Camera.h"
#include "INA219.h"
#include "BME280.h"
#include "INMP441.h"

#define I2C_SCL 41
#define I2C_SDA 42

Camera camera;
INA219 ina;
BME280 bme;
INMP441 mic;

void setup() {
  delay(8000);
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nElectric Sky firmware booted");

  // Camera setup
  if (!camera.begin()) {
    Serial.println("Camera failed. Halting.");
    while (true) delay(1000);
  }

  camera_fb_t* fb = camera.capture();

  if (!fb) {
    Serial.println("Camera capture failed");
  } else {
    Serial.printf("Captured frame: %u bytes, %u x %u\n", fb->len, fb->width, fb->height);
    camera.release(fb);
  }


  // I2C setup
  Serial.println("Scanning I2C...");
  Wire.begin(I2C_SDA, I2C_SCL);

  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("I2C found: 0x%02X (%u)\n", addr, addr);
    }
  }

  // INA219 setup
  if (!ina.begin(Wire)) {
    Serial.println("INA219 failed");
  } else {
    Serial.println("INA219 OK");
    INA219Reading p = ina.read();
    Serial.printf(
      "INA219 bus=%.3f V shunt=%.3f mV current=%.3f mA power=%.3f mW\n",
      p.busVoltageV,
      p.shuntVoltageMv,
      p.currentMa,
      p.powerMw
    );
  }

  // BME280 setup
  if (!bme.begin(Wire)) {
    Serial.println("BME280 failed");
  } else {
    Serial.println("BME280 OK");
    BME280Reading b = bme.read();
    Serial.printf(
      "BME280 temp=%.2f C / %.2f F humidity=%.2f %% pressure=%.2f hPa\n",
      b.tempC,
      b.tempF,
      b.humidity,
      b.pressureHpa
    );
  }

  // INMP441 setup
  if (!mic.begin()) {
    Serial.println("INMP441 failed");
  } else {
    Serial.println("INMP441 OK");
    AudioObservables a;
    if (mic.read(a)) {
      Serial.printf(
        "INMP441 rms_db=%.2f peak_db=%.2f zcr=%.5f crest=%.3f samples=%d elapsed=%lu ms\n",
        a.rmsDb,
        a.peakDb,
        a.zcr,
        a.crestFactor,
        a.samples,
        a.elapsedMs
      );
    } else {
      Serial.println("INMP441 read failed");
    }
  }
  
}



void loop() {
  Serial.println("alive");
  delay(2000);
}