# ina219.py
# Minimal MicroPython INA219 reader

import time

_REG_CONFIG = 0x00
_REG_SHUNT_VOLTAGE = 0x01
_REG_BUS_VOLTAGE = 0x02
_REG_POWER = 0x03
_REG_CURRENT = 0x04
_REG_CALIBRATION = 0x05

class INA219:
    def __init__(self, i2c, address=0x40, shunt_ohms=0.1):
        self.i2c = i2c
        self.address = address
        self.shunt_ohms = shunt_ohms

        # Simple default calibration:
        # current_lsb = 0.1 mA/bit
        self.current_lsb = 0.0001
        self.power_lsb = self.current_lsb * 20

        cal = int(0.04096 / (self.current_lsb * self.shunt_ohms))
        self._write16(_REG_CALIBRATION, cal)

        # 32V range, gain /8, 12-bit bus+shunt ADC, continuous mode
        self._write16(_REG_CONFIG, 0x399F)

    def _write16(self, reg, value):
        self.i2c.writeto_mem(
            self.address,
            reg,
            bytes([(value >> 8) & 0xFF, value & 0xFF])
        )

    def _read16(self, reg, signed=False):
        data = self.i2c.readfrom_mem(self.address, reg, 2)
        value = (data[0] << 8) | data[1]
        if signed and value & 0x8000:
            value -= 65536
        return value

    def bus_voltage_v(self):
        raw = self._read16(_REG_BUS_VOLTAGE)
        return ((raw >> 3) * 0.004)

    def shunt_voltage_mv(self):
        raw = self._read16(_REG_SHUNT_VOLTAGE, signed=True)
        return raw * 0.01

    def current_ma(self):
        # Re-write calibration before reading current; INA219 can reset it.
        cal = int(0.04096 / (self.current_lsb * self.shunt_ohms))
        self._write16(_REG_CALIBRATION, cal)
        raw = self._read16(_REG_CURRENT, signed=True)
        return raw * self.current_lsb * 1000

    def power_mw(self):
        raw = self._read16(_REG_POWER)
        return raw * self.power_lsb * 1000

    def read(self):
        return {
            "kind": "ina219",
            "address": self.address,
            "bus_voltage_v": round(self.bus_voltage_v(), 3),
            "shunt_voltage_mv": round(self.shunt_voltage_mv(), 3),
            "current_ma": round(self.current_ma(), 3),
            "power_mw": round(self.power_mw(), 3),
            "ts_ms": time.ticks_ms(),
        }