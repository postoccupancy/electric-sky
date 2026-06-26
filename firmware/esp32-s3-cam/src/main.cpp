#include <Arduino.h>
#include "SensorManager.h"

SensorManager sensors;

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
}

void loop() {
  SensorFrame f = sensors.read();

  Serial.printf(
    "frame=%lu temp=%s %.2fC rh=%.2f%% pressure=%.2fhPa power=%s %.3fmW audio_rms=%.2fdB\n",
    f.frameId,
    f.climate.ok ? "OK" : "BAD",
    f.climate.tempC,
    f.climate.humidity,
    f.climate.pressureHpa,
    f.power.ok ? "OK" : "BAD",
    f.power.powerMw,
    f.audio.rmsDb
  );

  delay(1000);
}