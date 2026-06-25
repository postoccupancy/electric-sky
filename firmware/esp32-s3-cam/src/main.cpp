#include <Arduino.h>
#include "Camera.h"
#include <Wire.h>
#define I2C_SCL 41
#define I2C_SDA 42

Camera camera;

void setup() {
  delay(3000);
  Serial.begin(115200);
  delay(1000);

  Serial.println("\nElectric Sky firmware booted");

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

  Serial.println("Scanning I2C...");
  Wire.begin(I2C_SDA, I2C_SCL);

  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("I2C found: 0x%02X (%u)\n", addr, addr);
    }
  }

}



void loop() {
  Serial.println("alive");
  delay(2000);
}