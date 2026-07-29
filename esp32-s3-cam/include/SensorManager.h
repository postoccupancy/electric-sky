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
  INA219Reading readSolarPower();
  bool solarPowerAvailable() const;
  BME280Reading readClimate();
  bool readAudio(AudioObservables& output);
  void stopAudio();
  bool startAudio();
  bool cameraAvailable() const;
  camera_fb_t* captureCamera();
  void releaseCamera(camera_fb_t* frame);

private:
  Camera camera;
  bool _cameraOk = false;
  INA219 ina;
  INA219 solarIna{0x41};
  bool _solarInaOk = false;
  BME280 bme;
  INMP441 mic;

  SemaphoreHandle_t _i2cMutex = nullptr;

  void scanI2C();
};
