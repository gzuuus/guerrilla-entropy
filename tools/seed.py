#!/usr/bin/env python3
"""guerrilla-entropy seed generator.

Captures N bytes from the device, optionally mixes in external entropy
(dice rolls / coin flips / key mashing — each captures keystroke timing too),
and prints the result as hex (and optionally a BIP39 mnemonic).

The mix is XOR of two streams: device_bytes XOR shake256(external). Either
input being good makes the output good — the device can only ever ADD
uncertainty (see AGENTS.md, THE invariant).

Modes:
  default      interactive guided flow (dice -> coin -> mash; Enter to skip each)
  --device-only   skip external entropy
  -e TEXT      non-interactive external entropy as text
  --self-test  validate BIP39 against known vectors (no device needed)
"""
import argparse, hashlib, math, os, serial, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
WORDLIST = os.path.join(HERE, "bip39_english.txt")

# Rough per-sample estimates for the unlabeled parts. Clearly rough — see the
# "ent != entropy" principle in AGENTS.md. Dice/coin values are exact; these
# cover key-mashing chars and keystroke-timing jitter.
MASH_CHAR_BITS = 2.5
TIMING_BITS = 4.0


# ---------- BIP39 ----------

def load_wordlist():
    with open(WORDLIST, encoding="utf-8") as f:
        words = [w.strip() for w in f if w.strip()]
    if len(words) != 2048:
        sys.exit(f"bad wordlist: {len(words)} words (expected 2048)")
    return words


def bip39_mnemonic(entropy: bytes) -> str:
    if len(entropy) not in (16, 20, 24, 28, 32):
        sys.exit(f"BIP39 needs 16/20/24/28/32 bytes of entropy, got {len(entropy)}")
    words = load_wordlist()
    cs_bits = len(entropy) * 8 // 32
    bits = "".join(f"{b:08b}" for b in entropy)
    bits += "".join(f"{b:08b}" for b in hashlib.sha256(entropy).digest())[:cs_bits]
    return " ".join(words[int(bits[i:i + 11], 2)] for i in range(0, len(bits), 11))


def self_test():
    vectors = [
        ("00000000000000000000000000000000",
         "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"),
        ("80808080808080808080808080808080",
         "letter advice cage absurd amount doctor acoustic avoid letter advice cage above"),
        ("ffffffffffffffffffffffffffffffff",
         "zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo wrong"),
    ]
    ok = True
    for ent_hex, expected in vectors:
        got = bip39_mnemonic(bytes.fromhex(ent_hex))
        passed = got == expected
        ok &= passed
        print(f"  [{'PASS' if passed else 'FAIL'}] {ent_hex}")
        if not passed:
            print(f"        got:      {got}")
            print(f"        expected: {expected}")
    print("bip39 self-test:", "PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)


# ---------- external entropy ----------

def pack_deltas(deltas):
    return b"".join((d & 0xFFFF_FFFF_FFFF_FFFF).to_bytes(8, "little") for d in deltas)


def read_line_timed(prompt, echo=True, counter_label=None):
    """Read a line in cbreak mode, capturing per-key nanosecond timings.
    echo=True prints each typed char; counter_label shows a live counter.
    Falls back to plain input() (no timing) if stdin is not a tty."""
    print(prompt)
    sys.stdout.write("  > ")
    sys.stdout.flush()
    if not sys.stdin.isatty():
        return input(), []
    try:
        import termios, tty
        fd = sys.stdin.fileno()
        old = termios.tcgetattr(fd)
        chars, deltas = [], []
        prev = time.perf_counter_ns()
        try:
            tty.setcbreak(fd)
            while True:
                ch = sys.stdin.read(1)
                now = time.perf_counter_ns()
                if ch in ("\n", "\r"):
                    break
                if ch in ("\x7f", "\x08"):    # DEL / Backspace - edit, not entropy
                    if chars:
                        chars.pop()
                        if echo:
                            sys.stdout.write("\b \b"); sys.stdout.flush()
                        elif counter_label:
                            sys.stdout.write(f"\r  {counter_label}: {len(chars)}   ")
                            sys.stdout.flush()
                    prev = now
                    continue
                deltas.append(now - prev)
                chars.append(ch)
                prev = now
                if echo:
                    sys.stdout.write(ch); sys.stdout.flush()
                elif counter_label:
                    sys.stdout.write(f"\r  {counter_label}: {len(chars)} ")
                    sys.stdout.flush()
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old)
        print()
        return "".join(chars), deltas
    except (ImportError, ValueError, OSError):
        return input(), []


def collect_dice(target):
    need = math.ceil(target / math.log2(6))
    txt, deltas = read_line_timed(
        f"[1/3] Dice (D6)\n  Roll ~50 times ({need} for full {target}-bit resilience). Any separator.",
        echo=True)
    rolls = sum(1 for c in txt if c in "123456")
    if rolls == 0:
        return None
    vb = rolls * math.log2(6)
    tb = len(deltas) * TIMING_BITS
    blob = txt.encode() + pack_deltas(deltas)
    label = f"dice: {rolls} rolls = {vb:.0f} bits" + (f" (+{tb:.0f} timing)" if deltas else "")
    return (blob, label, vb + tb)


def collect_coin(target):
    txt, deltas = read_line_timed(
        "[2/3] Coin (H/T)\n  Flip ~30 times. Any separator (H/T).",
        echo=True)
    flips = sum(1 for c in txt.upper() if c in "HT")
    if flips == 0:
        return None
    vb = float(flips)
    tb = len(deltas) * TIMING_BITS
    blob = txt.encode() + pack_deltas(deltas)
    label = f"coin: {flips} flips = {vb:.0f} bits" + (f" (+{tb:.0f} timing)" if deltas else "")
    return (blob, label, vb + tb)


def collect_mash(target):
    txt, deltas = read_line_timed(
        "[3/3] Mash keys\n  Mash randomly for a few seconds - timing is the entropy. Enter to stop.",
        echo=False, counter_label="keys")
    if not txt:
        return None
    cb = len(txt) * MASH_CHAR_BITS
    tb = len(deltas) * TIMING_BITS
    blob = txt.encode() + pack_deltas(deltas)
    return (blob, f"mash: {len(txt)} keys = ~{cb + tb:.0f} bits (rough)", cb + tb)


def estimate_text(s):
    """Rough estimate for non-interactive -e text."""
    dice = sum(1 for c in s if c in "123456")
    other = len([c for c in s if not c.isspace()]) - dice
    return dice * math.log2(6) + other * 2.0


# ---------- device capture ----------

def open_serial(port, baud, timeout):
    if not os.path.exists(port):
        sys.exit(f"device not found at {port}\n"
                 f"  is it plugged in and running firmware (not in bootloader mode)?\n"
                 f"  BOOT+RESET is only for flashing; tap RESET to run normally.")
    try:
        return serial.Serial(port, baud, timeout=timeout)
    except serial.SerialException as e:
        sys.exit(f"cannot open {port}: {e}\n"
                 f"  (permissions? try: sudo usermod -aG dialout $USER, then re-login)")


def capture_device(port, baud, n):
    s = open_serial(port, baud, 4.0)
    time.sleep(0.3)
    s.reset_input_buffer()
    s.write(b"aM")          # all sources, mixed mode
    s.flush()
    time.sleep(0.05)
    s.write(b"G")
    s.flush()
    buf = bytearray()
    while len(buf) < n:
        chunk = s.read(min(8192, n - len(buf)))
        if not chunk:
            sys.exit("device timeout - is it running the guerrilla-entropy firmware? (BOOT+RESET to flash)")
        buf.extend(chunk)
    s.write(b"S")
    s.flush()
    s.close()
    return bytes(buf)


# ---------- main ----------

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-n", "--bytes", type=int, default=32, help="seed size in bytes (default 32)")
    ap.add_argument("--bip39", action="store_true", help="also print BIP39 mnemonic")
    ap.add_argument("--device-only", action="store_true", help="skip external entropy")
    ap.add_argument("-e", "--external", help="external entropy as text (non-interactive)")
    ap.add_argument("-p", "--port", default="/dev/ttyACM0")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--self-test", action="store_true", help="validate BIP39 vectors and exit")
    a = ap.parse_args()

    if a.self_test:
        self_test()

    if a.bip39 and a.bytes not in (16, 20, 24, 28, 32):
        sys.exit(f"--bip39 requires -n in {{16,20,24,28,32}}, got {a.bytes}")

    target = a.bytes * 8

    # 1. collect external entropy
    ext_sources = []   # list of (blob, label, bits)
    if a.device_only:
        pass
    elif a.external is not None:
        bits = estimate_text(a.external)
        ext_sources.append((a.external.encode(), f"-e text: ~{bits:.0f} bits (rough)", bits))
    else:
        print(f"\n== External entropy (Enter to skip any step) ==")
        print(f"Target for device-compromised resilience: {target} bits\n")
        for collector in (collect_dice, collect_coin, collect_mash):
            res = collector(target)
            if res:
                ext_sources.append(res)
                print(f"  -> {res[1]}\n")

    # 2. capture device bytes
    dev = capture_device(a.port, a.baud, a.bytes)

    # 3. mix
    if ext_sources:
        full_blob = b"".join(s[0] for s in ext_sources)
        ext = hashlib.shake_256(full_blob).digest(a.bytes)
        seed = bytes(x ^ y for x, y in zip(dev, ext))
        total_ext = sum(s[2] for s in ext_sources)
    else:
        ext = None
        seed = dev
        total_ext = 0

    # 4. output
    dev_bits = len(dev) * 8
    print()
    print(f"device   ({len(dev):2d}B): {dev.hex()}")
    print(f"  ~{dev_bits} bits (device floor; TRNG-backed)")
    if ext is not None:
        print(f"external ({len(ext):2d}B): {ext.hex()}")
        for _, label, _ in ext_sources:
            print(f"  - {label}")
        if total_ext >= target:
            print(f"  => sufficient device-compromised resilience (>= {target} bits)")
        else:
            print(f"  => ~{total_ext:.0f} bits, short of {target} (device carries the seed)")
        print(f"  [a {len(seed)}B seed holds at most {dev_bits} bits; external only matters if the device is compromised]")
    else:
        print("external      : <none - device-only>")
    print("-" * 39)
    print(f"seed     ({len(seed):2d}B): {seed.hex()}")
    print(f"  {dev_bits} bits - full for {len(seed)}B. Device-backed;"
          + (" external adds resilience if device compromised." if ext is not None
             else " device-only."))
    if a.bip39:
        words_count = (len(seed) * 8 + len(seed) * 8 // 32) // 11
        print()
        print(f"mnemonic ({words_count} words):")
        print("  " + bip39_mnemonic(seed))
    print()
    print("# tip: for high-value seeds, run this air-gapped and verify the")
    print("#       wordlist checksum (see tools/bip39_english.txt).")


if __name__ == "__main__":
    main()
