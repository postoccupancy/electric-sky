#include <Arduino.h>
#include "Camera.h"

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
}

void loop() {
  Serial.println("alive");
  delay(2000);
}