#include <Arduino.h>
#include "source.h"
#include "sources/trng.h"
#include "sources/adc_float.h"
#include "pool.h"
#include "health.h"

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
  uint32_t passed = 0;
  uint32_t failed = 0;
  SourceSlot(EntropySource* s) : src(s) {}
};
SourceSlot slots[] = { &trng, &adc };
const size_t num_sources = sizeof(slots) / sizeof(slots[0]);

Pool pool;

int solo = -1;           // -1 = aggregate all
bool raw_mode = false;   // true = bypass pool + health (inspection only)
bool streaming = false;
uint32_t bytes_emitted = 0;
uint32_t failclose_rounds = 0;
}  // namespace

static void print_info() {
  Serial.println("[guerrilla-entropy] phase 5 (health-gated)");
  for (size_t i = 0; i < num_sources; i++)
    Serial.printf("  %zu=%-12s pass=%u fail=%u\n", i,
                  slots[i].src->name(), slots[i].passed, slots[i].failed);
  Serial.printf("active: %s | mode: %s | emitted: %u | failclose: %u | streaming: %d\n",
                solo < 0 ? "all" : slots[solo].src->name(),
                raw_mode ? "raw" : "mixed",
                bytes_emitted, failclose_rounds, streaming);
}

// Gather + health-check one source into a temp buffer; on pass, fold into pool.
// Returns true if the source contributed this round.
static bool poll_source(size_t i, uint8_t* buf, size_t n) {
  slots[i].src->gather(buf, n);
  if (HealthCheck::check(buf, n)) {
    slots[i].passed++;
    pool.add(buf, n);
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
  print_info();
  Serial.println("cmd: 0-9 solo a=all M=mixed R=raw G=stream S=stop I=info T=test");;
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

  if (!streaming) return;

  const size_t CHUNK = 64;
  if (raw_mode) {
    // raw: unfiltered source output, no health gate, no pool (inspection)
    uint8_t raw[CHUNK];
    size_t off = 0;
    if (solo >= 0 && solo < (int)num_sources) {
      off = slots[solo].src->gather(raw, CHUNK);
    } else {
      size_t per = CHUNK / num_sources;
      for (size_t i = 0; i < num_sources; i++) {
        slots[i].src->gather(raw + off, per);
        off += per;
      }
      if (off < CHUNK) slots[0].src->gather(raw + off, CHUNK - off);
    }
    Serial.write(raw, CHUNK);
    bytes_emitted += CHUNK;
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
