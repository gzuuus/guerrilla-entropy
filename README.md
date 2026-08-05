# guerrilla-entropy

A DIY, guerrilla-grade hardware entropy generator for ESP32 boards. It
harvests randomness from several independent on-board sources, health-checks
each one, mixes them through a SHA-256 sponge, and exposes the bytes over USB
CDC. A host tool turns that into a seed (hex / BIP39 mnemonic), optionally
XOR-mixed with your own dice rolls or keystrokes.

**Not a certified CSPRNG.** A transparent, home-buildable entropy *contributor*
designed to be XOR-mixed with external entropy (dice, coins, keystrokes) so it
can only ever *add* uncertainty to a human-supervised pool — e.g. as one input
into Bitcoin seed generation.

## Status

Working MVP with an on-device status display. The device produces validated
uniform output, every source is health-gated with fail-close, and the host
tool generates seeds with optional external entropy + BIP39 mnemonics.
Prebuilt `.bin` releases and a no-build flashing guide are in
[`docs/FLASHING.md`](docs/FLASHING.md). See `design/` for what's still planned
(standalone/battery mode, SX1262 RSSI source).

## Target hardware (dev)

LilyGo T3S3 V1 — ESP32-S3 (4 MB flash, 2 MB PSRAM), SX1262 LoRa, IP5306 PMIC,
OLED. No onboard IMU, no SD slot. Port: `/dev/ttyACM0` (native USB CDC).

The architecture is chip-agnostic: the `EntropySource` registry probes at
runtime, so a generic ESP32 with no LoRa still works (baseline sources are
universal on every ESP32). Multi-chip builds are a planned follow-up.

## How it works

```
sources (TRNG, ADC) -> health gate (fail-close) -> SHA-256 pool -> USB CDC
                                                                   |
                       host: XOR external entropy -> hex / BIP39 seed
```

- **Sources**: `esp_random()` TRNG + floating-pin ADC LSBs. Each implements
  `EntropySource` and probes in `begin()`; absent sources are skipped.
- **Health gate**: per-source repetition + proportion tests (NIST SP 800-90B
  simplified). A failing source is skipped, never fatal; if no source is
  healthy, the device emits nothing (fail-close).
- **Pool**: SHA-256 sponge (HW-accelerated) purifies biased input into uniform
  output. Output stays XOR-mixable with external entropy.
- **Host tool**: captures N bytes, optionally XOR-mixes dice/keystrokes, prints
  hex + BIP39 mnemonic with an honest entropy estimate.

## Quickstart

```bash
cd guerrilla-entropy
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt

# build + flash — enter BOOT+RESET before upload, tap RESET after (see AGENTS.md)
.venv/bin/pio run -t upload

# generate a 256-bit seed (device + interactive dice/keystrokes), BIP39 mnemonic
.venv/bin/python3 tools/seed.py --bip39
```

## Quality gate

`ent` (uniformity) + `gzip` (incompressibility) — two independent signals. See
`reports/` for validated samples (TRNG mixed: 7.9998 bits/byte, gzip 100%; raw
ADC 6.8 bits/byte and compresses to 87%, demonstrating the mixer's value).

**`ent` measures uniformity, not entropy.** The pool makes any input look
uniform; real trust comes from multiple independent sources + external dice.

## License

MIT — see [LICENSE](LICENSE).
