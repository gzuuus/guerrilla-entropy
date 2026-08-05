"""PlatformIO extra_script: produce a single merged flash binary.

`pio run -t mergebin` merges bootloader + partition table + boot_app0 + firmware
into one .bin flashable at offset 0x0 — the "grab it and it just works" artifact
for distribution. Reads PIO's own FLASH_EXTRA_IMAGES + $ESP32_APP_OFFSET, so the
merge is guaranteed to match what `pio run -t upload` flashes (no hardcoded
paths or addresses; boot_app0.bin lives in the framework package).

Flash the result with:
    esptool --chip esp32s3 write_flash 0x0 guerrilla-entropy-merged.bin
"""
Import("env")
import os
import subprocess


def merge_bin(source, target, env):
    app_off = env.subst("$ESP32_APP_OFFSET")
    extras = [(a, env.subst(p)) for a, p in env.get("FLASH_EXTRA_IMAGES", [])]
    if not extras:
        print("merge_bin: FLASH_EXTRA_IMAGES empty — nothing to merge with app")
        return
    # firmware.bin sits next to bootloader.bin in the build dir; $BUILD_PATH
    # isn't in scope in a custom-target action, so derive it from an extra path.
    build_dir = os.path.dirname(extras[0][1])
    fw = os.path.join(build_dir, "firmware.bin")
    out = os.path.join(build_dir, "guerrilla-entropy-merged.bin")

    cmd = [env.subst("$PYTHONEXE"), "-m", "esptool",
           "--chip", env.subst("$BOARD_MCU"),
           "merge-bin", "-o", out, app_off, fw]
    for addr, path in extras:
        cmd += [addr, path]

    subprocess.run(cmd, check=True)
    print("merged binary -> %s (%d bytes, flash at 0x0)" % (out, os.path.getsize(out)))


env.AddCustomTarget(
    "mergebin",
    dependencies=["buildprog"],
    actions=[merge_bin],
    title="Merged binary",
    description="Merge bootloader+partitions+app into one flashable .bin (offset 0x0)",
)
