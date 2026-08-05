#include <Arduino.h>
#include "source.h"
#include "sources/trng.h"
#include "sources/adc_float.h"
#include "pool.h"
#include "health.h"
#include "display.h"

// Phase 5 — health-gated mixer. Every source's gather() passes a repetition
// + proportion test before entering the pool. Fail-close: if no source is
// healthy in a round, emit nothing. Graceful degradation: a failed source
// is skipped, healthy ones keep feeding the pool.
//
// Protocol: 0-9 solo | a all | M mixed (default, health-gated) | R raw
//           (bypass pool+health, for inspection) | G stream | S stop | I info

namespace {
TrngSource trng;
AdcFloatSource adc;

struct SourceSlot {
  EntropySource* src;
  uint64_t passed = 0;
  uint64_t failed = 0;
  SourceSlot(EntropySource* s) : src(s) {}
};
SourceSlot slots[] = { &trng, &adc };
const size_t num_sources = sizeof(slots) / sizeof(slots[0]);

Pool pool;

int solo = -1;           // -1 = aggregate all
bool raw_mode = false;   // true = bypass pool + health (inspection only)
bool streaming = false;
uint64_t bytes_emitted = 0;
uint64_t failclose_rounds = 0;

OledDisplay display;
bool have_display = false;
}  // namespace

static void print_info() {
  Serial.println("[guerrilla-entropy] phase 5 (health-gated)");
  Serial.printf("display: %s\n", have_display ? "OLED 128x64" : "none (headless)");
  for (size_t i = 0; i < num_sources; i++)
    Serial.printf("  %zu=%-12s pass=%llu fail=%llu\n", i,
                  slots[i].src->name(), slots[i].passed, slots[i].failed);
  Serial.printf("active: %s | mode: %s | emitted: %llu | failclose: %llu | streaming: %d\n",
                solo < 0 ? "all" : slots[solo].src->name(),
                raw_mode ? "raw" : "mixed",
                bytes_emitted, failclose_rounds, streaming);
}

// Gather + health-check one source into a temp buffer; on pass, fold into pool.
// Returns true if the source contributed this round. Honors gather()'s actual
// byte count (may be < n) so an under-filling source never pools uninitialized
// bytes, and a 0-byte gather doesn't defeat fail-close (the freshness policy).
static bool poll_source(size_t i, uint8_t* buf, size_t n) {
  size_t got = slots[i].src->gather(buf, n);
  if (got == 0) return false;                  // no contribution; keep fail-close honest
  if (HealthCheck::check(buf, got)) {
    slots[i].passed++;
    pool.add(buf, got);
    return true;
  }
  slots[i].failed++;
  return false;
}

// On-device proof that the health gate rejects stuck/biased input and accepts
// varied input. Run with 'T'.
static void run_selftest() {
  uint8_t b[64];
  auto report = [&](const char* label, bool want) {
    bool got = HealthCheck::check(b, 64);
    Serial.printf("  %-22s %s (want %s)\n", label, got ? "PASS" : "FAIL", want ? "PASS" : "FAIL");
  };
  Serial.println("[health self-test]");
  memset(b, 0xAA, 64); report("stuck 0xAA x64", false);
  memset(b, 0x00, 64); report("stuck 0x00 x64", false);
  memset(b, 0x11, 64); for (int i = 0; i < 14; i++) b[i] = 0x22; report("biased 50/14", false);
  for (int i = 0; i < 64; i++) b[i] = (uint8_t)esp_random();
  report("varied esp_random", true);
}

void setup() {
  Serial.begin(115200);
  for (auto& s : slots) s.src->begin();
  pool.begin();
  have_display = display.begin();
  print_info();
  Serial.println("cmd: 0-9 solo a=all M=mixed R=raw G=stream S=stop I=info T=test");
}

void loop() {
  if (Serial.available()) {
    uint8_t c = Serial.read();
    if (c >= '0' && c <= '9') {
      int idx = c - '0';
      if (idx < (int)num_sources) solo = idx;
    } else switch (c) {
      case 'a': solo = -1;         break;
      case 'M': raw_mode = false;  break;
      case 'R': raw_mode = true;   break;
      case 'G': streaming = true;   break;
      case 'S': streaming = false;  break;
      case 'I': print_info();       break;
      case 'T': run_selftest();      break;
    }
  }

  // Status redraw at ~4 Hz. 4 Hz keeps the I2C sendBuffer out of the way of
  // the entropy stream; all-sources throughput is bounded by the ADC source's
  // analogRead() cost, not by this redraw (measured: trng-solo mixed stays
  // ~110 KB/s with the display live, matching the pre-display baseline).
  if (have_display) {
    static uint32_t last_disp = 0;
    uint32_t now = millis();
    if (now - last_disp >= 250) {
      last_disp = now;
      char mode[16];
      if (raw_mode)            snprintf(mode, sizeof mode, "RAW");
      else if (solo >= 0)      snprintf(mode, sizeof mode, "SOLO:%s", slots[solo].src->name());
      else                     snprintf(mode, sizeof mode, "MIXED");
      DisplaySnapshot s;
      s.mode = mode;
      s.streaming = streaming;
      s.emitted = bytes_emitted;
      s.failclose = failclose_rounds;
      for (size_t i = 0; i < num_sources && i < (size_t)DisplaySnapshot::MAX_SRC; i++) {
        s.name[i] = slots[i].src->name();
        s.pass[i] = slots[i].passed;
        s.fail[i] = slots[i].failed;
      }
      s.n_src = num_sources;
      display.show(s);
    }
  }

  if (!streaming) return;

  const size_t CHUNK = 64;
  if (raw_mode) {
    // raw: unfiltered source output, no health gate, no pool (inspection)
    uint8_t raw[CHUNK];
    size_t off = 0;
    if (solo >= 0 && solo < (int)num_sources) {
      off = slots[solo].src->gather(raw, CHUNK);   // honor actual byte count
    } else {
      // fair interleave for inspection (assumes full fill; `ent` catches a gap)
      size_t per = CHUNK / num_sources;
      for (size_t i = 0; i < num_sources; i++) {
        slots[i].src->gather(raw + off, per);
        off += per;
      }
      if (off < CHUNK) slots[0].src->gather(raw + off, CHUNK - off);
    }
    Serial.write(raw, off);
    bytes_emitted += off;
    return;
  }

  // mixed: per-source gather -> health gate -> pool; fail-close if none healthy
  uint8_t sbuf[CHUNK];
  bool any = false;
  if (solo >= 0 && solo < (int)num_sources) {
    any = poll_source(solo, sbuf, CHUNK);
  } else {
    size_t per = CHUNK / num_sources;
    for (size_t i = 0; i < num_sources; i++)
      if (poll_source(i, sbuf, per)) any = true;
  }
  if (any) {
    uint8_t out[CHUNK];
    pool.extract(out, CHUNK);
    Serial.write(out, CHUNK);
    bytes_emitted += CHUNK;
  } else {
    failclose_rounds++;   // fail-close: no fresh entropy this round, emit nothing
  }
}
