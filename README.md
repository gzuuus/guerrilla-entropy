# guerrilla-entropy

A DIY, guerrilla-grade hardware entropy generator for the LilyGo T3S3
(ESP32-S3 + SX1262 LoRa). It harvests randomness from several independent
on-board sources, health-checks each one, mixes them through a SHA-256
sponge, and exposes the bytes over USB CDC.

**Not a certified CSPRNG.** It is a transparent, home-buildable entropy
*contributor*, designed to be XOR-mixed with your own external entropy
(dice, coins, keystrokes) so it can only ever *add* uncertainty to a
human-supervised pool — e.g. as one input into a Bitcoin seed generation.

## Status
Scaffolding — firmware not yet written.

## Target hardware
- LilyGo T3S3 V1 — ESP32-S3 (QFN56), 4 MB flash, 2 MB PSRAM, SX1262 LoRa,
  IP5306 PMIC, OLED. No onboard IMU, no SD slot.

## Entropy sources (v1)
- ESP32-S3 TRNG (`esp_random`)
- SX1262 LoRa RSSI noise (RX, no traffic)
- Floating-pin ADC LSB noise

## Storage plan
- Working pool / mixer: 2 MB PSRAM (fast, byte-addressable)
- Persistent snapshot: custom partition in 4 MB flash (erase-before-write, 4 KB sectors)
- Output: USB CDC (`/dev/ttyACM0`)

## License
TBD
