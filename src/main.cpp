#include <Arduino.h>

// Phase 1 — toolchain vertical slice.
// Confirms: PlatformIO build -> esptool flash -> our firmware runs ->
// output appears on native USB CDC (/dev/ttyACM0).
// No entropy logic yet. No pin writes (avoid guessing the LED pin until verified).

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("[guerrilla-entropy] phase 1 alive");
  Serial.printf("chip: %s  rev %d  cores %d\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
  Serial.printf("flash: %u KB  psram: %u KB\n",
                ESP.getFlashChipSize() / 1024, ESP.getPsramSize() / 1024);
  Serial.println("---");
}

static uint32_t counter = 0;
void loop() {
  Serial.printf("heartbeat #%u\n", counter++);
  delay(1000);
}
