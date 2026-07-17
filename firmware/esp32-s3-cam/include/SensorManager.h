#pragma once

#include <Arduino.h>
#include "Camera.h"
#include "INA219.h"
#include "BME280.h"
#include "INMP441.h"

class SensorManager {
public:
  bool begin();
  INA219Reading readPower();
  BME280Reading readClimate();
  bool readAudio(AudioObservables& output);
  void stopAudio();
  bool startAudio();

private:
  Camera camera;
  bool _cameraOk = false;
  INA219 ina;
  BME280 bme;
  INMP441 mic;

  SemaphoreHandle_t _i2cMutex = nullptr;

  void scanI2C();
};
