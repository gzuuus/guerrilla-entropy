#!/usr/bin/env python3
"""Capture raw entropy bytes from the guerrilla-entropy device.

Protocol: drain boot banner -> send 'G' -> read N raw bytes -> send 'S'.
Output is raw binary (8-bit clean), suitable for `ent` and NIST STS.
"""
import argparse, serial, time, sys


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-n", "--bytes", type=int, default=1_000_000,
                    help="bytes to capture (default 1 MB)")
    ap.add_argument("-o", "--out", default="trng.bin", help="output file")
    ap.add_argument("-p", "--port", default="/dev/ttyACM0")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    a = ap.parse_args()

    s = serial.Serial(a.port, a.baud, timeout=2.0)
    time.sleep(0.3)               # let device notice DTR
    s.reset_input_buffer()        # drain boot banner / stale bytes
    s.write(b'G')                 # start streaming
    s.flush()

    written, t0 = 0, time.time()
    with open(a.out, "wb") as f:
        while written < a.bytes:
            chunk = s.read(min(8192, a.bytes - written))
            if not chunk:
                print("timeout / read error", file=sys.stderr)
                break
            f.write(chunk)
            written += len(chunk)
    s.write(b'S')                 # stop streaming
    s.flush()
    s.close()

    dt = time.time() - t0
    rate = (written * 8 / dt / 1000) if dt else 0
    print(f"wrote {written} bytes to {a.out}  ({dt:.1f}s, {rate:.1f} kbit/s)")


if __name__ == "__main__":
    main()
