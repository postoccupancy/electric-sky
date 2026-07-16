#include "BME280.h"
#include <Adafruit_BME280.h>

BME280::BME280(uint8_t address)
  : _address(address), _bme(new Adafruit_BME280()) {}

BME280::~BME280() {
  delete _bme;
}

bool BME280::begin(TwoWire& wire) {
  if (!_bme->begin(_address, &wire)) return false;
  _bme->setSampling(
    Adafruit_BME280::MODE_NORMAL,
    Adafruit_BME280::SAMPLING_X1,    // x1 → ~100Hz ODR
    Adafruit_BME280::SAMPLING_X1,
    Adafruit_BME280::SAMPLING_X1,
    Adafruit_BME280::FILTER_OFF,     // no IIR lag at max rate
    Adafruit_BME280::STANDBY_MS_0_5  // 0.5ms standby between measurements
  );
  return true;
}

BME280Reading BME280::read() {
  BME280Reading r;
  r.ok = true;

  r.tempC = _bme->readTemperature();
  r.tempF = r.tempC * 9.0f / 5.0f + 32.0f;
  r.humidity = _bme->readHumidity();
  r.pressureHpa = _bme->readPressure() / 100.0f;

  if (isnan(r.tempC) || isnan(r.humidity) || isnan(r.pressureHpa) ||
      r.tempC < -40 || r.tempC > 85 ||
      r.humidity < 0 || r.humidity > 100 ||
      r.pressureHpa < 300 || r.pressureHpa > 1100) {
    r.ok = false;
  }

  return r;
}