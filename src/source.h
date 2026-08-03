#pragma once
#include <stddef.h>
#include <stdint.h>

// Common interface for every entropy source.
// begin() probes hardware — returning false means "absent"; the mixer skips
// that source (never fatal). Baseline sources are universal; optional ones
// (LoRa, IMU) probe and may be skipped. See AGENTS.md.
class EntropySource {
 public:
  virtual ~EntropySource() = default;
  virtual bool begin() = 0;
  virtual const char* name() const = 0;
  // Fill buf with up to n bytes. Return bytes written (may be < n).
  virtual size_t gather(uint8_t* buf, size_t n) = 0;
};
