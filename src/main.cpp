#include <Arduino.h>
#include "source.h"
#include "sources/trng.h"

// Phase 2 — TRNG end-to-end. Command-gated raw binary over USB CDC.
// Protocol (one byte each): 'G' = stream, 'S' = stop, 'I' = info.
// Text is emitted only when idle / on 'I'; raw bytes only while streaming,
// so tools/capture.py drains the banner, sends 'G', and gets a clean stream.

namespace {
TrngSource trng;
EntropySource* const sources[] = { &trng };
const size_t num_sources = sizeof(sources) / sizeof(sources[0]);

bool streaming = false;
uint32_t bytes_emitted = 0;
}  // namespace

static void print_info() {
  Serial.println("[guerrilla-entropy] phase 2");
  Serial.printf("sources (%u):", (unsigned)num_sources);
  for (auto* s : sources) Serial.printf(" %s", s->name());
  Serial.println();
  Serial.printf("emitted: %u bytes  streaming: %d\n", bytes_emitted, streaming);
}

void setup() {
  Serial.begin(115200);
  for (auto* s : sources) s->begin();
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
    uint8_t buf[256];
    size_t n = trng.gather(buf, sizeof(buf));
    Serial.write(buf, n);
    bytes_emitted += n;
  }
}
