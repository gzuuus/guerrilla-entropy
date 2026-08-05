# Flashing guerrilla-entropy

For a **LilyGo T3S3 V1** (ESP32-S3). No build tools needed — just the
prebuilt `.bin` from the [releases](../../releases) and `esptool`.

## What you need

- The `guerrilla-entropy-merged.bin` from the latest release.
- Python 3, then `pip install esptool`.
- A USB-C data cable (charge-only cables won't work).
- On Linux: your user in the `dialout` group (`sudo usermod -aG dialout $USER`,
  then re-login) so you can open `/dev/ttyACM0` without root.

## Flash it

The merged binary contains the bootloader + partition table + app in one file,
flashed at offset `0x0`. Find your port (`/dev/ttyACM0` on Linux, similar on
macOS, `COM3` on Windows) and run:

```bash
esptool --chip esp32s3 -p /dev/ttyACM0 write_flash 0x0 guerrilla-entropy-merged.bin
```

### Entering download mode (the two-button dance)

The T3S3 uses **native USB CDC with no auto-reset**, so esptool can't reboot the
chip into the bootloader by itself. Enter download mode by hand:

1. Hold **BOOT**.
2. Tap **RESET** (once).
3. Release **BOOT**.

The port reappears and `write_flash` can connect. After it finishes:

4. Tap **RESET** (no BOOT) to launch the app.

The `Hard resetting via RTS pin...` line esptool prints is a **no-op** on this
board — you must tap RESET yourself, or the chip stays in the ROM bootloader
and the app never starts.

## Verify

- The OLED shows `guerrilla-entropy` + status a second after RESET.
- `esptool` / a serial monitor at 115200 baud shows the boot banner:
  `[guerrilla-entropy] phase 5 (health-gated)` and `display: OLED 128x64`.

## Generating a seed

```bash
pip install pyserial   # if not already
python3 tools/seed.py --bip39
```

See the [README](../README.md) for the entropy model — the device is one input
that can only ever *add* uncertainty; mix in dice/keystrokes for
device-compromised resilience.

## Already have a compatible bootloader?

If your board already runs an ESP32-S3 Arduino bootloader (most do), you can
flash the app image alone, skipping the bootloader/partitions:

```bash
esptool --chip esp32s3 -p /dev/ttyACM0 write_flash 0x10000 firmware.bin
```

The merged binary is the safer default — it's self-contained and works on a
blank or wiped chip.

## Flash size note (4 MB)

The T3S3 has **4 MB flash**. The board definition PlatformIO uses assumes 8 MB,
which produces an image whose header overstates flash size — the chip then fails
its flash probe at boot and loops. Our build overrides all three size knobs, so
the released `guerrilla-entropy-merged.bin` carries the correct 4 MB header. If
you build from source, keep the `board_build.flash_size` / `board_upload.flash_size`
/ `board_build.partitions` overrides in `platformio.ini`.
