#include "BME280.h"
#include <Adafruit_BME280.h>

BME280::BME280(uint8_t address)
  : _address(address), _bme(new Adafruit_BME280()) {}

BME280::~BME280() {
  delete _bme;
}

bool BME280::begin(TwoWire& wire) {
  return _bme->begin(_address, &wire);
}

BME280Reading BME280::read() {
  BME280Reading r;
  r.tempC = _bme->readTemperature();
  r.tempF = r.tempC * 9.0f / 5.0f + 32.0f;
  r.humidity = _bme->readHumidity();
  r.pressureHpa = _bme->readPressure() / 100.0f;
  return r;
}