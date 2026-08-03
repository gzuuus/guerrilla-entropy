#pragma once
#include <stdint.h>
#include <string.h>
#include <esp_system.h>
#include "../source.h"

// ESP32-S3 hardware TRNG via esp_random(). Baseline, universal.
// ponytail: TRNG quality depends on RF noise; stats may drop with both radios
// off. Lever later: briefly enable WiFi to feed it (phase 4).
class TrngSource : public EntropySource {
 public:
  bool begin() override { return true; }  // TRNG always present on ESP32
  const char* name() const override { return "trng"; }
  size_t gather(uint8_t* buf, size_t n) override {
    size_t i = 0;
    while (i < n) {
      uint32_t r = esp_random();
      size_t chunk = (n - i < 4) ? (n - i) : 4;
      memcpy(buf + i, &r, chunk);
      i += chunk;
    }
    return i;
  }
};
