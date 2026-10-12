#!/usr/bin/env python3
"""Echo only tinyPTP 60-byte diagnostic frames on a dedicated Linux test NIC.

Requires root/CAP_NET_RAW. Does not configure the NIC or any clocks.
"""
import argparse
from pathlib import Path
import socket
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("interface")
    parser.add_argument("--seconds", type=float, default=45)
    args = parser.parse_args()
    mac = bytes.fromhex(Path(f"/sys/class/net/{args.interface}/address").read_text().strip().replace(":", ""))
    deadline = time.monotonic() + args.seconds
    seen = set()
    with socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(0x88B5)) as sock:
        sock.bind((args.interface, 0))
        sock.settimeout(0.25)
        print(f"Listening on {args.interface}, MAC {mac.hex(':')}", flush=True)
        while time.monotonic() < deadline:
            try:
                frame = sock.recv(2048)
            except socket.timeout:
                continue
            if len(frame) != 60 or frame[12:15] != b"\x88\xb5\xa1":
                continue
            sequence = frame[15]
            if sequence >= 10 or frame[16:] != bytes(i ^ sequence ^ 0x5a for i in range(16, 60)):
                continue
            reply = frame[6:12] + mac + frame[12:14] + b"\xa2" + frame[15:]
            sent = sock.send(reply)
            seen.add(sequence)
            print(f"RX valid seq={sequence} source={frame[6:12].hex(':')}; echo bytes={sent}", flush=True)
    print(f"Unique valid frames={len(seen)}/10", flush=True)


if __name__ == "__main__":
    main()
