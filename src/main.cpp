#include <Arduino.h>
#include "source.h"
#include "sources/trng.h"
#include "pool.h"

// Phase 3 — TRNG -> SHA-256 pool -> raw binary over USB CDC.
// Protocol (one byte each): 'G' = stream, 'S' = stop, 'I' = info.
// Text only when idle / on 'I'; raw bytes only while streaming, so the host
// drains the banner, sends 'G', and gets a clean binary stream.

namespace {
TrngSource trng;
EntropySource* const sources[] = { &trng };
const size_t num_sources = sizeof(sources) / sizeof(sources[0]);

Pool pool;

bool streaming = false;
uint32_t bytes_emitted = 0;
}  // namespace

static void print_info() {
  Serial.println("[guerrilla-entropy] phase 3 (mixed)");
  Serial.printf("sources (%u):", (unsigned)num_sources);
  for (auto* s : sources) Serial.printf(" %s", s->name());
  Serial.println();
  Serial.printf("emitted: %u bytes  streaming: %d\n", bytes_emitted, streaming);
}

void setup() {
  Serial.begin(115200);
  for (auto* s : sources) s->begin();
  pool.begin();
  print_info();
  Serial.println("cmd: G=stream S=stop I=info");
}

void loop() {
  if (Serial.available()) {
    switch (Serial.read()) {
      case 'G': streaming = true;  break;
      case 'S': streaming = false; break;
      case 'I': print_info();      break;
    }
  }
  if (streaming) {
    // ponytail: 1:1 reseed with TRNG each block (over-conservative on a
    // uniform source). Tune the in:out ratio when biased sources land.
    uint8_t raw[64], out[64];
    trng.gather(raw, sizeof(raw));
    pool.add(raw, sizeof(raw));
    pool.extract(out, sizeof(out));
    Serial.write(out, sizeof(out));
    bytes_emitted += sizeof(out);
  }
}
