#pragma once
#include <Arduino.h>
#include <Wire.h>

struct BME280Reading {
  float tempC;
  float tempF;
  float humidity;
  float pressureHpa;
};

class Adafruit_BME280;

class BME280 {
public:
  explicit BME280(uint8_t address = 0x76);
  ~BME280();

  bool begin(TwoWire& wire = Wire);
  BME280Reading read();

private:
  uint8_t _address;
  Adafruit_BME280* _bme;
};