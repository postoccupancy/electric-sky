#include "INMP441.h"
#include <driver/i2s.h>
#include <math.h>

#define I2S_PORT I2S_NUM_0
#define MAX_24BIT 8388607.0f

INMP441::INMP441(int sckPin, int wsPin, int sdPin, int sampleRate, int frameSize)
  : _sckPin(sckPin),
    _wsPin(wsPin),
    _sdPin(sdPin),
    _sampleRate(sampleRate),
    _frameSize(frameSize),
    _frameId(0) {}

bool INMP441::begin() {
  i2s_config_t i2s_config = {};
  i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  i2s_config.sample_rate = _sampleRate;
  i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  i2s_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2s_config.dma_buf_count = 4;
  i2s_config.dma_buf_len = _frameSize;
  i2s_config.use_apll = false;
  i2s_config.tx_desc_auto_clear = false;
  i2s_config.fixed_mclk = 0;

  i2s_pin_config_t pin_config = {};
  pin_config.bck_io_num = _sckPin;
  pin_config.ws_io_num = _wsPin;
  pin_config.data_out_num = I2S_PIN_NO_CHANGE;
  pin_config.data_in_num = _sdPin;

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) return false;

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) return false;

  i2s_zero_dma_buffer(I2S_PORT);
  return true;
}

bool INMP441::read(AudioObservables& out) {
  uint32_t start = millis();

  int32_t samples[512];
  size_t bytesRead = 0;

  // Drain any stale frames backed up in the DMA ring buffer so the
  // blocking read below captures the most recent audio, not old data.
  size_t tmp;
  while (i2s_read(I2S_PORT, samples, sizeof(samples), &tmp, 0) == ESP_OK && tmp > 0) {}

  esp_err_t err = i2s_read(
    I2S_PORT,
    samples,
    sizeof(samples),
    &bytesRead,
    pdMS_TO_TICKS(100)
  );

  if (err != ESP_OK || bytesRead == 0) return false;

  int count = bytesRead / sizeof(int32_t);

  int64_t total = 0;
  for (int i = 0; i < count; i++) {
    int32_t s = samples[i] >> 8;
    total += s;
    samples[i] = s;
  }

  int32_t mean = count > 0 ? total / count : 0;

  int32_t peak = 0;
  double sumSq = 0;
  int crossings = 0;
  bool havePrev = false;
  int32_t prev = 0;

  for (int i = 0; i < count; i++) {
    int32_t s = samples[i] - mean;
    int32_t a = abs(s);

    if (a > peak) peak = a;
    sumSq += (double)s * (double)s;

    if (havePrev) {
      if ((s >= 0 && prev < 0) || (s < 0 && prev >= 0)) {
        crossings++;
      }
    } else {
      havePrev = true;
    }

    prev = s;
  }

  int32_t rms = count > 0 ? (int32_t)sqrt(sumSq / count) : 0;

  auto dbfs = [](int32_t value) -> float {
    if (value < 1) value = 1;
    return 20.0f * log10f(value / MAX_24BIT);
  };

  out.frameId = ++_frameId;
  out.samples = count;
  out.dcOffset = mean;
  out.rms = rms;
  out.peak = peak;
  out.rmsDb = dbfs(rms);
  out.peakDb = dbfs(peak);
  out.zcr = count > 0 ? (float)crossings / (float)count : 0.0f;
  out.crestFactor = rms > 0 ? (float)peak / (float)rms : 0.0f;
  out.elapsedMs = millis() - start;

  return true;
}