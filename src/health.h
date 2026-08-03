#pragma once
#include <stdint.h>
#include <stddef.h>

// Catastrophic-failure health tests (NIST SP 800-90B simplified).
// Catches STUCK sources (repetition) and grossly skewed ones (proportion).
// Does NOT grade mild bias — the SHA-256 pool handles that. Thresholds are
// deliberately conservative so a healthy source never false-positives.
//
// A failing chunk means "do not trust this source's output this round" —
// the mixer skips it (fail-close). See AGENTS.md.
class HealthCheck {
 public:
  static constexpr unsigned REP_CUTOFF = 5;  // >5 identical bytes in a row = stuck

  // true = healthy enough to feed the pool this round.
  static bool check(const uint8_t* buf, size_t n) {
    return check_repetition(buf, n) && check_proportion(buf, n);
  }

 private:
  static bool check_repetition(const uint8_t* buf, size_t n) {
    if (n == 0) return true;
    uint8_t prev = buf[0];
    unsigned run = 1;
    for (size_t i = 1; i < n; i++) {
      if (buf[i] == prev) {
        if (++run > REP_CUTOFF) return false;
      } else {
        prev = buf[i];
        run = 1;
      }
    }
    return true;
  }

  static bool check_proportion(const uint8_t* buf, size_t n) {
    if (n == 0) return true;
    const unsigned cutoff = n / 4;          // uniform never approaches this
    unsigned counts[256] = {0};
    for (size_t i = 0; i < n; i++) counts[buf[i]]++;
    for (int v = 0; v < 256; v++)
      if (counts[v] > cutoff) return false;
    return true;
  }
};
