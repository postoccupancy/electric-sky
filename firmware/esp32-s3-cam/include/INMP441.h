#pragma once
#include <Arduino.h>

struct AudioObservables {
  uint32_t frameId;
  int samples;
  int32_t dcOffset;
  int32_t rms;
  int32_t peak;
  float rmsDb;
  float peakDb;
  float zcr;
  float crestFactor;
  uint32_t elapsedMs;
};

class INMP441 {
public:
  INMP441(
    int sckPin = 2,
    int wsPin = 1,
    int sdPin = 21,
    int sampleRate = 16000,
    int frameSize = 512
  );

  bool begin();
  bool read(AudioObservables& out);

private:
  int _sckPin;
  int _wsPin;
  int _sdPin;
  int _sampleRate;
  int _frameSize;
  uint32_t _frameId;
};