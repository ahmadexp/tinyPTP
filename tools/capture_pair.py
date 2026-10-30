#!/usr/bin/env python3
"""Reset two UNO R3s together and log their autonomous roles at 115200 baud."""
import argparse
from contextlib import ExitStack
from datetime import datetime, timezone
from pathlib import Path
import time

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("master_port")
    parser.add_argument("follower_port")
    parser.add_argument("output", type=Path, help="New capture directory")
    parser.add_argument("--seconds", type=float, default=300)
    parser.add_argument("--debug", action="store_true", help="Toggle diagnostic output after reset")
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("--seconds must be positive")
    if Path(args.master_port).resolve() == Path(args.follower_port).resolve():
        parser.error("Use two different serial ports")
    args.output.mkdir(parents=True, exist_ok=False)
    with ExitStack() as stack:
        ports = [stack.enter_context(serial.Serial(p, 115200, timeout=0, exclusive=True))
                 for p in (args.master_port, args.follower_port)]
        logs = [stack.enter_context((args.output / name).open("x", encoding="utf-8"))
                for name in ("master.log", "follower.log")]
        for port, log in zip(ports, logs):
            log.write(f"UTC: {datetime.now(timezone.utc).isoformat()}\n"
                      f"Port: {port.port}; baud: 115200; paired reset; debug: {args.debug}\n")
            log.flush()
            port.dtr = False
        time.sleep(0.1)
        for port in ports:
            port.reset_input_buffer()
            port.dtr = True
        start = time.monotonic()
        debug_sent = False
        while time.monotonic() - start < args.seconds:
            if args.debug and not debug_sent and time.monotonic() - start >= 2:
                for port in ports:
                    port.write(b"d")
                debug_sent = True
            for port, log in zip(ports, logs):
                data = port.read(port.in_waiting)
                if data:
                    log.write(data.decode("ascii", errors="backslashreplace"))
                    log.flush()
            time.sleep(0.01)
    # No stop command: both boards retain their autonomous roles after capture.


if __name__ == "__main__":
    main()
