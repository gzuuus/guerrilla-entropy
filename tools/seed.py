#!/usr/bin/env python3
"""guerrilla-entropy seed generator.

Captures N bytes from the device, optionally mixes in external entropy
(dice rolls / keystrokes with timing), and prints the result as hex (and
optionally a BIP39 mnemonic).

The mix is XOR of two streams: device_bytes XOR shake256(external). Either
input being good makes the output good — the device can only ever ADD
uncertainty (see AGENTS.md, THE invariant).

Modes:
  default      interactive prompt; Enter to skip (device-only) or type to mix
  --device-only   skip the prompt, pure device output
  -e TEXT      non-interactive external entropy (e.g. dice rolls)
  --self-test  validate BIP39 against known vectors (no device needed)
"""
import argparse, hashlib, os, serial, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
WORDLIST = os.path.join(HERE, "bip39_english.txt")


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

def collect_interactive():
    print("Add external entropy? Type dice rolls / coin flips / mash keys.")
    print("  (Enter to finish, or an empty Enter to SKIP and use device-only)")
    sys.stdout.flush()
    try:
        import termios, tty
        fd = sys.stdin.fileno()
        old = termios.tcgetattr(fd)
        chars, deltas, prev = bytearray(), [], 0
        try:
            tty.setcbreak(fd)
            t0 = time.perf_counter_ns()
            prev = t0
            while True:
                ch = sys.stdin.read(1)
                now = time.perf_counter_ns()
                if ch in ("\n", "\r"):
                    break
                deltas.append(now - prev)
                chars.extend(ch.encode("utf-8", "ignore"))
                prev = now
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old)
        if not chars:
            return None
        blob = bytes(chars) + b"".join((d & 0xFFFF_FFFF_FFFF_FFFF).to_bytes(8, "little") for d in deltas)
        print(f"  captured {len(chars)} chars, {len(deltas)} timing samples")
        return blob
    except (ImportError, ValueError):
        # non-tty / non-unix fallback
        s = input("  external string: ").strip()
        return s.encode() if s else None


# ---------- device capture ----------

def capture_device(port, baud, n):
    s = serial.Serial(port, baud, timeout=4.0)
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
            sys.exit("device timeout — is it running the phase-4 firmware? (BOOT+RESET to flash)")
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

    dev = capture_device(a.port, a.baud, a.bytes)

    ext_blob = None
    if a.device_only:
        ext_blob = None
    elif a.external is not None:
        ext_blob = a.external.encode("utf-8")
    else:
        ext_blob = collect_interactive()

    if ext_blob:
        ext = hashlib.shake_256(ext_blob).digest(a.bytes)
        seed = bytes(x ^ y for x, y in zip(dev, ext))
    else:
        ext = None
        seed = dev

    print()
    print(f"device   ({len(dev):2d}B): {dev.hex()}")
    if ext is not None:
        print(f"external ({len(ext):2d}B): {ext.hex()}")
    else:
        print(f"external      : <none — device-only>")
    print("─" * 39)
    print(f"seed     ({len(seed):2d}B): {seed.hex()}")
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
