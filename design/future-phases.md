# Future phases — design notes

Captures the planned-but-not-built work so a future session (or another
builder) can pick it up without re-deriving the design. The MVP through
phase 5 + host tool is done; everything here is hardening / expansion.

Guiding order of value: **7 → 8 → 4b**, then SP800-90B when a new physical
source lands. See `AGENTS.md` for conventions, gotchas, and the invariants
that must hold across all of this.

---

## Phase 7 — Storage + standalone (offline collection)

**Goal:** disconnect from the host, harvest entropy on battery into PSRAM,
snapshot to flash, and dump later over USB. The air-gapped usage scenario —
the right way to generate high-value seeds.

**Approach:**
- Working pool stays in PSRAM (fast, byte-addressable, already used).
- Add a flash data partition (custom `partitions.csv` entry, ~1–2 MB) for
  snapshots. NOR flash = erase-before-write at 4 KB sectors, ~100k cycles —
  buffer in PSRAM, flush full sectors, never write byte-at-a-time (gotcha #4).
- Collection modes:
  - **Continuous**: harvest → pool → append to a ring buffer in flash until
    full, then stop (or wrap).
  - **Timed**: deep-sleep, wake on timer, harvest a chunk, flush, sleep.
  Maximizes battery life; typical ESP32-S3 deep-sleep is ~10 µA.
- Dump: a protocol command streams the stored bytes back over USB CDC; host
  `capture.py` (or a new `dump.py`) writes them to a file.

**Key decisions / risks:**
- Custom partition table means re-flashing with erase (fine — we already
  control the whole flash). The current `default.csv` is a placeholder.
- LittleFS vs raw partition: raw is simpler and we control sector layout.
  Prefer raw unless we need a filesystem.
- Battery: IP5306 PMIC is confirmed (`batMv` readable). Add a low-battery
  check that flushes + stops cleanly before cutoff.
- Fail-close interaction: if all sources go unhealthy while collecting
  offline, the device must stop writing (not store stale pool state).

**Exit criteria:** unplug → it collects on battery for N minutes → plug in →
dump ≥ the expected bytes → `ent`+`gzip` pass on the dumped stream.

---

## Phase 8 — Distribution (anyone can build / flash)

**Goal:** a non-builder user grabs a `.bin`, flashes it, and runs. No
PlatformIO, no Python, no venv on their machine.

**Approach:**
- CI (GitHub Actions) builds per-chip `.bin` artifacts via PlatformIO and
  attaches them to releases.
- Targets, in order: `esp32s3` (our board), `esp32` (classic, most common).
  Add C3/C6/S2 as bonus.
- Flashing options for end users:
  - **esptool** one-liner (documented): `esptool --chip esp32s3 write_flash ...`
  - **Web flasher** (ESP Web Tools) — browser-based, lowest friction. Needs
    manifest files; worth adding.
- A `docs/FLASHING.md` with the per-board BOOT+RESET dance and the flash-size
    caveat (gotcha #5 — the devkitc-1 8 MB default).

**Key decisions / risks:**
- Pin assignments per board: the `EntropySource` registry probes at runtime,
  so a generic ESP32 with no LoRa just runs baseline sources. LoRa source
  needs board-specific SPI pins — ship defaults for known boards (T3S3,
  T-Beam, Heltec), overridable via build flags.
- The native-USB flash dance (two button presses, gotcha #1) must be
  documented clearly per board; classic ESP32 boards with a UART bridge
  (CP2102) auto-reset and are easier.

**Exit criteria:** a second person, with no dev setup, flashes a release
`.bin` and generates a seed.

---

## Phase 4b — SX1262 LoRa RSSI source

**Goal:** a third independent entropy source from the LoRa radio.

**Approach:**
- Add `RadioLib` to `platformio.ini` `lib_deps`.
- New `sources/sx1262_rssi.h` (+ likely `.cpp`): `begin()` inits the radio
  into RX with no traffic, `gather()` reads the RSSI register rapidly and
  packs low bits. Probe the SPI bus for an SX1262; return `false` if absent
  (non-LoRa boards skip it).
- Needs the T3S3's exact SX1262 SPI pin assignments (SCK/MOSI/MISO/NSS/RST/
  BUSY). **Confirm from the LilyGo schematic** before wiring — these vary by
  sub-revision and guessing risks driving a pin used for something else.

**Key decisions / risks:**
- RSSI entropy rate is unknown — **measure, don't guess** (this is in
  `AGENTS.md` open questions). Capture raw RSSI bytes, run the quality gate.
- If RSSI is strongly biased, the pool handles it (as it does for ADC), but
  the health gate must not reject it wholesale — tune thresholds against real
  RSSI data.
- Diminishing returns: TRNG + ADC already feed the pool well. SX1262 adds
  independence (defense-in-depth) more than raw throughput.

**Exit criteria:** SX1262 source probes, contributes, raw vs pooled passes
the quality gate, doesn't false-positive the health gate.

---

## Deferred — SP800-90B / NIST STS

Not worth the machinery for a guerrilla toy *until* a new physical source of
unknown quality lands (e.g. the avalanche-noise board — a reverse-biased
transistor junction on an ADC pin, ~$2, the single best quality-per-dollar
HW RNG upgrade). At that point: grade the new source's min-entropy properly
with the NIST SP800-90B reference tool, then decide extraction ratio and
whether to whiten before the pool.

For the current sources, `ent` + `gzip` + the contrast method (raw vs pooled)
is the agreed quality gate (see `AGENTS.md`).

---

## Design principles that must hold (do not regress)

- **THE invariant:** output stays XOR-mixable. The device can only add
  uncertainty. No purifying function that assumes the pool is already uniform.
- **Fail-close:** a degraded source is skipped; if none healthy, emit nothing.
- **`ent` ≠ entropy.** Never present uniform-looking output as proven
  unpredictability. Trust comes from independence + combination.
- **Guerrilla:** minimal, cheap, auditable. Question any feature needing
  expensive parts or heavy libraries.
