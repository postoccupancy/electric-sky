#pragma once
#include <Arduino.h>
#include <Wire.h>

struct INA219Reading {
  float busVoltageV;
  float shuntVoltageMv;
  float currentMa;
  float powerMw;
};

class INA219 {
public:
  explicit INA219(uint8_t address = 0x40, float shuntOhms = 0.1f);

  bool begin(TwoWire& wire = Wire);
  INA219Reading read();

private:
  TwoWire* _wire;
  uint8_t _address;
  float _shuntOhms;
  float _currentLsb;
  float _powerLsb;

  void write16(uint8_t reg, uint16_t value);
  uint16_t read16(uint8_t reg);
  int16_t read16Signed(uint8_t reg);
  void calibrate();
};