# AGENTS.md

Authoritative context for any coding agent on this repo. **Read first.**

## What this is

A DIY hardware **entropy contributor** for ESP32 boards. Harvests randomness
from multiple independent on-board sources, health-checks each, mixes them in
a SHA-256 sponge, and emits bytes over USB CDC. Designed to be **XOR-mixed
with external human entropy** (dice, coins, keystrokes) — e.g. as one input
into a Bitcoin seed.

## What it is NOT

- **Not a certified CSPRNG.** Never present it as one.
- **Not a standalone oracle.** Its job is to *add* uncertainty to a
  human-supervised pool, never be the sole source.
- Not a Meshtastic device. We flash our own firmware over whatever was there.

## Guerrilla philosophy (hard constraints)

- Minimal, cheap, home-buildable. A feature needing a $20 part or a 500-line
  library must justify itself.
- Transparent over clever. Boring code you can audit at 3am.
- Deletion over addition. Shortest correct diff wins.
- Reuse Arduino-ESP32 core, RadioLib, stdlib before writing new code.

## THE invariant (do not break)

> **Output MUST always remain XOR-mixable with external entropy.**
> The device can only ever *add* uncertainty. Never replace the raw/XOR path
> with a "purifying" function that assumes the pool is already uniform —
> that silently breaks the safety model.

Secondary invariants:
- Never claim more entropy than sources provably produce.
- Every source probes in `begin()`; a failing/missing source is **skipped, not fatal**.
- Baseline (TRNG + ADC) is universal on every ESP32 — the device must produce
  entropy with zero optional hardware attached.

## Hardware (dev target)

LilyGo **T3S3 V1**: ESP32-S3 (QFN56, 4 MB flash, 2 MB PSRAM), SX1262 LoRa,
IP5306 PMIC, OLED. **No IMU, no SD slot.** Port `/dev/ttyACM0` (native USB
CDC). User is in the `dialout` group.

## v1 scope

Single chip target: **ESP32-S3**. Multi-chip is a future `[env:…]` addition,
not a rewrite — the `EntropySource` registry makes it a one-liner. **Do not**
build board-abstraction layers, chip-detection, pin-config DSLs, or plugin
loaders until a second board forces it. YAGNI.

## Stack

PlatformIO + Arduino-ESP32 core + RadioLib (SX1262). Dev tooling in a
project-local Python venv (`.venv/`). End users never need PlatformIO — they
flash a prebuilt `.bin`.

## Dev environment

```bash
cd guerrilla-entropy
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
source .venv/bin/activate        # then: pio ...
```

## Build / flash / monitor

```bash
.venv/bin/pio run                # build
.venv/bin/pio run -t upload      # flash (board must be in download mode — see gotcha #1)
.venv/bin/pio device monitor     # serial @ 115200
```

**Full flash dance** (native USB has no auto-reset — see gotcha #1):
`BOOT+RESET` (enter bootloader) → `pio run -t upload` → `RESET` (launch app).
The upload's "Hard resetting via RTS pin" line is a **no-op** — you MUST tap
RESET manually or the app never starts (chip stays in ROM bootloader).

## Hardware gotchas (these cost real time — respect them)

1. **Auto-reset over USB CDC fails on the T3S3 — two button presses per flash.**
   To enter download mode: hold **BOOT** → tap **RESET** → release **BOOT**.
   After `pio run -t upload` finishes, tap **RESET** again (no BOOT) to actually
   launch the app. The upload's RTS reset is a no-op on native USB.
2. **Debian's system `esptool` is broken** (missing `stub_flasher_32s3.json`).
   Use `--no-stub` for reads, or the venv's pip-installed `esptool` for flashing.
3. **USB CDC vanishes ~1 s on reset** — boot-log capture must tolerate reconnect.
4. **NOR flash = erase-before-write at 4 KB sectors, ~100k cycles.** Always
   buffer in PSRAM and flush a full sector. Never write byte-at-a-time.
5. **The `esp32-s3-devkitc-1` board def is the 8 MB / no-PSRAM N8 variant.**
   Our T3S3 is 4 MB flash + 2 MB PSRAM. If the binary header overstates flash
   size, the chip fails its flash probe at boot (`do_core_init` assert, boot
   loop). Override **all three** knobs in `platformio.ini` — `board_build.flash_size` alone
   only fixes the partition table, not the image header:
   `board_build.flash_size=4MB`, `board_build.partitions=default.csv`,
   `board_upload.flash_size=4MB`.

## Architecture

```
src/
  main.cpp          setup/loop, USB CDC command loop
  source.h          EntropySource interface: begin()->bool, gather(), name()
  sources/
    trng.cpp          esp_random()              — baseline, universal
    sx1262_rssi.cpp   RadioLib, RX-no-traffic   — optional
    adc_float.cpp     floating-pin ADC LSBs     — baseline, universal
  health.cpp        per-source repetition + proportion tests (NIST SP 800-90B-lite)
  pool.cpp          SHA-256 sponge mixer (HW-accel), PSRAM-backed
  store.cpp         flash-partition snapshot (v2)
tools/
  capture.py        host: grab N bytes, XOR in dice/keystrokes, write file
```

## Adding an entropy source

1. New file in `src/sources/`, implement `EntropySource`.
2. `begin()` probes hardware; return `false` if absent → mixer skips it.
3. Register it in `main.cpp`'s source list. Done.

## Code style

- C++17. One source per file. `snake_case` files/functions, `PascalCase` types.
- No exceptions in hot paths. No Arduino `String` in the mixer. No `delay()` in gather.
- Mark deliberate shortcuts: `// ponytail: <ceiling>, <upgrade path>`.

## Validation (required before any "it works" claim)

**Quality gate = `ent` + `gzip`** (two independent, cheap signals):
- `ent` — uniformity (entropy bits/byte → 8.0, chi-square p not pathological).
- `gzip` — incompressibility (compressed/orig ≈ 100%; biased/structured data
  compresses smaller — e.g. raw ADC → 87%, flagged).

Capture via `tools/capture.py` (supports `-s` source, `-m raw|mixed`); save
reports under `reports/`. SP800-90B / NIST STS deferred — revisit when adding
a new physical source of unknown quality (e.g. avalanche-noise board).

**`ent` measures uniformity, NOT entropy.** The SHA-256 pool makes *any* input
look uniform — a passing gate on pooled output does NOT prove the source
contributed unpredictability. To judge a source, contrast raw vs pooled
(`-m raw` then `-m mixed`): raw shows the source's real distribution/bias,
pooled shows the mixer doing its job. Real trust comes from multiple
independent sources + external dice, never from a single gate pass.

## Git

- Branch: `master`.
- Commits: `<type>: <subject>` (`chore`/`feat`/`fix`/`docs`/`refactor`). Atomic.

## Open questions (don't invent answers)

- License (TBD).
- Final pin assignments & partition table.
- Additional chip targets / supported boards.
- SX1262 RSSI entropy-rate estimate (measure, don't guess).
