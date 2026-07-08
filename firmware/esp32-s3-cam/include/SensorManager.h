#pragma once

#include <Arduino.h>
#include "Camera.h"
#include "INA219.h"
#include "BME280.h"
#include "INMP441.h"

struct SensorFrame {
  uint32_t frameId;
  uint32_t timestampMs;

  INA219Reading power;
  BME280Reading climate;
  AudioObservables audio;
};

class SensorManager {
public:
  bool begin();
  SensorFrame read();
  void stopForOTA();

private:
  Camera camera;
  bool _cameraOk = false;
  INA219 ina;
  BME280 bme;
  INMP441 mic;

  uint32_t frameId = 0;

  void scanI2C();
};