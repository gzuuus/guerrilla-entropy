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

## Hardware support

**Dev target:** LilyGo T3S3 V1 — ESP32-S3 (4 MB flash, 2 MB PSRAM), SX1262 LoRa,
IP5306 PMIC, onboard OLED. No IMU, no SD slot. Port: `/dev/ttyACM0` (native
USB CDC).

The architecture is chip-agnostic: every `EntropySource` and the display probe
at runtime, so a board with only the baseline hardware still works.

**Boards**

| Board | Chip | Tested? | Notes |
|---|---|---|---|
| LilyGo T3S3 V1 | ESP32-S3 | ✅ | dev target; onboard OLED |
| Other ESP32-S3 boards | ESP32-S3 | — | baseline sources work; set `-DOLED_SDA`/`-DOLED_SCL` if a display is wired |
| ESP32 / S2 / C3 / C6 | various | — | baseline only; OLED needs pin overrides; no multi-chip build yet |

**Displays**

| Display | Tested? | Notes |
|---|---|---|
| SSD1306 128×64, I²C 0x3C | ✅ | default; the T3S3 V1 OLED |
| Other SSD1306 (128×32, etc.) | — | swap geometry in `src/display.h` |
| SH1106 / other controllers | — | needs a different U8g2 constructor |

Anything marked ✅ is what releases are built and verified against. Anything
marked — should work by the probe-and-skip design but hasn't been confirmed on
hardware.

## How it works

```
sources (TRNG, ADC) -> health gate (fail-close) -> SHA-256 pool -> USB CDC
                                                                   |
                       host: XOR external entropy -> hex / BIP39 seed
```

- **Sources**: each implements `EntropySource` and probes in `begin()`; an
  absent source is skipped, never fatal. See [Entropy sources](#entropy-sources).
- **Health gate**: per-source repetition + proportion tests (NIST SP 800-90B
  simplified). A failing source is skipped, never fatal; if no source is
  healthy, the device emits nothing (fail-close).
- **Pool**: SHA-256 sponge (HW-accelerated) purifies biased input into uniform
  output. Output stays XOR-mixable with external entropy.
- **Host tool**: captures N bytes, optionally XOR-mixes dice/keystrokes, prints
  hex + BIP39 mnemonic with an honest entropy estimate.

## Entropy sources

Every source implements `EntropySource`, probes its hardware in `begin()`, and
is **skipped (not fatal)** if absent — so a board with only the baseline
sources still produces entropy. The SHA-256 pool mixes whatever's healthy.

| Source | File | Harvests | Present on |
|---|---|---|---|
| **TRNG** | `src/sources/trng.h` | `esp_random()` — the ESP32-S3 hardware RNG (internal RF + thermal noise) | every ESP32 (baseline) |
| **ADC float** | `src/sources/adc_float.h` | low 2 bits × 4 reads of a floating ADC1 pin (ambient EM) | every ESP32 (baseline) |

Planned sources (SX1262 LoRa RSSI, avalanche-noise board) are tracked in
[`design/future-phases.md`](design/future-phases.md). To add one, see
"Adding an entropy source" in [`AGENTS.md`](AGENTS.md).

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

## Serial protocol

The USB CDC interface takes single-byte commands (the host tools send these
for you; handy for inspection):

| Key | Action |
|---|---|
| `0`–`9` | solo a source by index |
| `a` | aggregate all sources |
| `M` | mixed mode (default: health-gated, through the pool) |
| `R` | raw mode (bypass pool + health — inspection only) |
| `G` / `S` | start / stop streaming |
| `I` | print status + per-source counters |
| `T` | run the on-device health self-test |

`T` is the quickest way to confirm the health gate rejects stuck/biased input
and accepts varied input.

## Quality gate

`ent` (uniformity) + `gzip` (incompressibility) — two independent signals. See
`reports/` for validated samples (TRNG mixed: 7.9998 bits/byte, gzip 100%; raw
ADC 6.8 bits/byte and compresses to 87%, demonstrating the mixer's value).

**`ent` measures uniformity, not entropy.** The pool makes any input look
uniform; real trust comes from multiple independent sources + external dice.

## License

MIT — see [LICENSE](LICENSE).
