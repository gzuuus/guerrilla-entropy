#include <Arduino.h>
#include "source.h"
#include "sources/trng.h"
#include "sources/adc_float.h"
#include "pool.h"

// Phase 4 — multi-source + raw/mixed modes.
// Protocol:  0-9 solo source N | a all | M mixed-via-pool (default) |
//            R raw (bypass pool) | G stream | S stop | I info
// 'R' exists to contrast a source's raw stats vs pooled: the mixer should turn
// a biased raw source into uniform pooled output. See AGENTS.md.

namespace {
TrngSource trng;
AdcFloatSource adc;
EntropySource* const sources[] = { &trng, &adc };
const size_t num_sources = sizeof(sources) / sizeof(sources[0]);

Pool pool;

int solo = -1;           // -1 = aggregate all
bool raw_mode = false;   // true = bypass pool, emit gathered bytes directly
bool streaming = false;
uint32_t bytes_emitted = 0;
}  // namespace

static void print_info() {
  Serial.println("[guerrilla-entropy] phase 4 (multi-source)");
  Serial.printf("sources (%u):", (unsigned)num_sources);
  for (size_t i = 0; i < num_sources; i++)
    Serial.printf(" %zu=%s", i, sources[i]->name());
  Serial.println();
  Serial.printf("active: %s | mode: %s | emitted: %u | streaming: %d\n",
                solo < 0 ? "all" : sources[solo]->name(),
                raw_mode ? "raw" : "mixed",
                bytes_emitted, streaming);
}

static size_t gather_active(uint8_t* buf, size_t n) {
  if (solo >= 0 && solo < (int)num_sources)
    return sources[solo]->gather(buf, n);
  size_t per = n / num_sources, off = 0;
  for (size_t i = 0; i < num_sources; i++) {
    sources[i]->gather(buf + off, per);
    off += per;
  }
  if (off < n) sources[0]->gather(buf + off, n - off);  // remainder -> source 0
  return n;
}

void setup() {
  Serial.begin(115200);
  for (auto* s : sources) s->begin();
  pool.begin();
  print_info();
  Serial.println("cmd: 0-9 solo a=all M=mixed R=raw G=stream S=stop I=info");
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
    }
  }
  if (streaming) {
    uint8_t raw[64], out[64];
    gather_active(raw, sizeof(raw));
    if (raw_mode) {
      Serial.write(raw, sizeof(raw));
      bytes_emitted += sizeof(raw);
    } else {
      pool.add(raw, sizeof(raw));
      pool.extract(out, sizeof(out));
      Serial.write(out, sizeof(out));
      bytes_emitted += sizeof(out);
    }
  }
}
