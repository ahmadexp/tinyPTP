#!/usr/bin/env python3
"""Capture an UNO diagnostic session. Requires pyserial on the test host."""

import argparse
from datetime import datetime, timezone
from pathlib import Path
import sys
import time

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    parser.add_argument("output", type=Path)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--reset", action="store_true", help="Pulse DTR to reset the UNO")
    parser.add_argument("--command", default="", help="ASCII command to send after opening")
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("--seconds must be positive")
    command = args.command.encode("ascii")
    # Exclusive creation protects earlier evidence from accidental overwrites.
    with args.output.open("x", encoding="utf-8") as log:
        header = (
            f"UTC: {datetime.now(timezone.utc).isoformat()}\n"
            f"Port: {args.port}; baud: {args.baud}; reset: {args.reset}\n"
        )
        log.write(header)
        print(header, end="", flush=True)
        with serial.Serial(args.port, args.baud, timeout=0.2, exclusive=True) as uart:
            if args.reset:
                uart.dtr = False
                time.sleep(0.1)
                uart.reset_input_buffer()
                uart.dtr = True
            if command:
                # Opening the UNO serial port can reset it even without --reset.
                time.sleep(2)
                uart.write(command)
                uart.flush()
            deadline = time.monotonic() + args.seconds
            while time.monotonic() < deadline:
                data = uart.read(uart.in_waiting or 1)
                if data:
                    text = data.decode("ascii", errors="backslashreplace")
                    log.write(text)
                    log.flush()
                    print(text, end="", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, serial.SerialException, UnicodeEncodeError) as error:
        print(f"Capture failed: {error}", file=sys.stderr)
        sys.exit(1)
