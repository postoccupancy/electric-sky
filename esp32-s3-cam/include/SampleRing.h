#pragma once

#include <Arduino.h>

template <typename T, size_t Capacity>
class SampleRing {
public:
  void push(const T& sample) {
    portENTER_CRITICAL(&_mux);
    if (_count == Capacity) {
      _tail = (_tail + 1) % Capacity;
      _count--;
      _overruns++;
    }
    _items[_head] = sample;
    _head = (_head + 1) % Capacity;
    _count++;
    portEXIT_CRITICAL(&_mux);
  }

  size_t pop(T* output, size_t maximum) {
    portENTER_CRITICAL(&_mux);
    size_t popped = 0;
    while (popped < maximum && _count > 0) {
      output[popped++] = _items[_tail];
      _tail = (_tail + 1) % Capacity;
      _count--;
    }
    portEXIT_CRITICAL(&_mux);
    return popped;
  }

  uint32_t overruns() const {
    portENTER_CRITICAL(&_mux);
    uint32_t value = _overruns;
    portEXIT_CRITICAL(&_mux);
    return value;
  }

  size_t size() const {
    portENTER_CRITICAL(&_mux);
    size_t value = _count;
    portEXIT_CRITICAL(&_mux);
    return value;
  }

private:
  T _items[Capacity];
  size_t _head = 0;
  size_t _tail = 0;
  size_t _count = 0;
  uint32_t _overruns = 0;
  mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
};
