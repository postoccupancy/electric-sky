# inmp441.py
#
# INMP441 acoustic observable module for MicroPython / ESP32-S3.
# Electric Sky / Resident Frequency v1.
#
# Purpose:
#   Treat microphone audio as one high-resolution environmental sensor.
#   This module does NOT compute FFT, CWT, phase, or resident frequencies.
#   It returns compact acoustic observables suitable for live UDP/OSC
#   streaming and later wavelet analysis.
#
# Wiring:
#   INMP441 VDD -> 3V3
#   INMP441 GND -> GND
#   INMP441 SCK -> GPIO2
#   INMP441 WS  -> GPIO1
#   INMP441 SD  -> GPIO21
#   INMP441 L/R -> GND

from machine import Pin, I2S
import time
import struct
import math


MAX_24BIT = 8388607


class INMP441:
    def __init__(
        self,
        sample_rate=16000,
        frame_size=512,
        sck_pin=2,
        ws_pin=1,
        sd_pin=21,
        i2s_id=0,
        ibuf=12000,
        warmup_reads=1,
    ):
        self.sample_rate = sample_rate
        self.frame_size = frame_size
        self.bytes_per_sample = 4
        self.buf = bytearray(frame_size * self.bytes_per_sample)
        self.frame_id = 0

        self.audio = I2S(
            i2s_id,
            sck=Pin(sck_pin),
            ws=Pin(ws_pin),
            sd=Pin(sd_pin),
            mode=I2S.RX,
            bits=32,
            format=I2S.MONO,
            rate=sample_rate,
            ibuf=ibuf,
        )

        for _ in range(warmup_reads):
            self.audio.readinto(self.buf)

    def _dbfs(self, value):
        if value < 1:
            value = 1
        return 20 * math.log10(value / MAX_24BIT)

    def read_observables(self):
        t0 = time.ticks_ms()
        n = self.audio.readinto(self.buf)

        if not n:
            return None

        count = n // 4
        samples = struct.unpack("<{}i".format(count), self.buf[:n])

        vals = []
        total = 0

        for raw in samples:
            s = raw >> 8
            vals.append(s)
            total += s

        mean = total // count if count else 0

        peak = 0
        sum_sq = 0
        crossings = 0
        prev = 0
        have_prev = False

        for s in vals:
            s = s - mean

            a = abs(s)
            if a > peak:
                peak = a

            sum_sq += s * s

            if have_prev:
                if (s >= 0 and prev < 0) or (s < 0 and prev >= 0):
                    crossings += 1
            else:
                have_prev = True

            prev = s

        rms = int(math.sqrt(sum_sq / count)) if count else 0
        zcr = crossings / count if count else 0
        crest = peak / rms if rms > 0 else 0

        self.frame_id += 1

        return {
            "kind": "audio_observables",
            "frame_id": self.frame_id,
            "sample_rate": self.sample_rate,
            "frame_size": self.frame_size,
            "samples": count,
            "dc_offset": mean,
            "rms": rms,
            "peak": peak,
            "rms_db": round(self._dbfs(rms), 2),
            "peak_db": round(self._dbfs(peak), 2),
            "zcr": round(zcr, 5),
            "crest_factor": round(crest, 3),
            "elapsed_ms": time.ticks_diff(time.ticks_ms(), t0),
        }

    # Backward-compatible alias if you want shorter call sites.
    def read(self):
        return self.read_observables()

    def deinit(self):
        self.audio.deinit()