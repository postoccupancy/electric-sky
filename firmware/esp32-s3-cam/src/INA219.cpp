#include "INA219.h"

#define REG_CONFIG        0x00
#define REG_SHUNT_VOLTAGE 0x01
#define REG_BUS_VOLTAGE   0x02
#define REG_POWER         0x03
#define REG_CURRENT       0x04
#define REG_CALIBRATION   0x05

INA219::INA219(uint8_t address, float shuntOhms)
  : _wire(nullptr),
    _address(address),
    _shuntOhms(shuntOhms),
    _currentLsb(0.0001f),
    _powerLsb(_currentLsb * 20.0f) {}

bool INA219::begin(TwoWire& wire) {
  _wire = &wire;

  _wire->beginTransmission(_address);
  if (_wire->endTransmission() != 0) {
    return false;
  }

  calibrate();

  // 32V range, gain /8, 12-bit bus+shunt ADC, continuous mode
  write16(REG_CONFIG, 0x399F);

  return true;
}

void INA219::calibrate() {
  uint16_t cal = (uint16_t)(0.04096f / (_currentLsb * _shuntOhms));
  write16(REG_CALIBRATION, cal);
}

void INA219::write16(uint8_t reg, uint16_t value) {
  _wire->beginTransmission(_address);
  _wire->write(reg);
  _wire->write((value >> 8) & 0xFF);
  _wire->write(value & 0xFF);
  _wire->endTransmission();
}

uint16_t INA219::read16(uint8_t reg) {
  _wire->beginTransmission(_address);
  _wire->write(reg);
  _wire->endTransmission(false);

  _wire->requestFrom((int)_address, 2);
  uint16_t value = ((uint16_t)_wire->read() << 8) | _wire->read();
  return value;
}

int16_t INA219::read16Signed(uint8_t reg) {
  return (int16_t)read16(reg);
}

INA219Reading INA219::read() {
  calibrate();

  uint16_t rawBus = read16(REG_BUS_VOLTAGE);
  int16_t rawShunt = read16Signed(REG_SHUNT_VOLTAGE);
  int16_t rawCurrent = read16Signed(REG_CURRENT);
  uint16_t rawPower = read16(REG_POWER);

  INA219Reading r;
  r.busVoltageV = ((rawBus >> 3) * 0.004f);
  r.shuntVoltageMv = rawShunt * 0.01f;
  r.currentMa = rawCurrent * _currentLsb * 1000.0f;
  r.powerMw = rawPower * _powerLsb * 1000.0f;
  return r;
}