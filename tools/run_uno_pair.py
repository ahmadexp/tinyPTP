#!/usr/bin/env python3
"""Start both UNO PHYs together and retain serial sessions for a PTP pair.

SIGTERM/SIGINT stops both roles with x. SIGUSR1 retries initialization on both
boards in the existing sessions. Logs remain separate. Requires pyserial.
"""
import argparse
from contextlib import ExitStack, suppress
from datetime import datetime, timezone
import os
from pathlib import Path
import signal
import sys
import time

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--master-port', required=True)
    parser.add_argument('--follower-port', required=True)
    parser.add_argument('--log-dir', required=True, type=Path)
    parser.add_argument('--seconds', type=float, default=0, help='0 runs until stopped')
    args = parser.parse_args()
    if args.seconds < 0:
        parser.error('--seconds must be nonnegative')
    if Path(args.master_port).resolve() == Path(args.follower_port).resolve():
        parser.error('Master and follower ports must be different')
    args.log_dir.mkdir(parents=True, exist_ok=False)
    state = {'stop': False, 'restart': False}

    def handle_signal(number, _frame):
        state['restart' if number == signal.SIGUSR1 else 'stop'] = True

    for number in (signal.SIGTERM, signal.SIGINT, signal.SIGUSR1):
        signal.signal(number, handle_signal)
    with ExitStack() as stack:
        sessions = []
        try:
            for role, port, command in [('master', args.master_port, b'em'),
                                        ('follower', args.follower_port, b'ef')]:
                log = stack.enter_context((args.log_dir / (role + '.log')).open('x'))
                uart = stack.enter_context(serial.Serial(
                    port, 115200, timeout=0, write_timeout=2, exclusive=True))
                sessions.append((uart, log, command))
                log.write(f'UTC: {datetime.now(timezone.utc).isoformat()}\n'
                          f'Role: {role}; port: {port}; baud: 115200\n')
                log.flush()
                uart.dtr = False
            time.sleep(0.1)
            for uart, _, _ in sessions:
                uart.reset_input_buffer()
                uart.dtr = True
            time.sleep(2)
            if state['stop']:
                return
            for uart, _, command in sessions:
                uart.write(command)
                uart.flush()
            (args.log_dir / 'pid').write_text(str(os.getpid()) + '\n')
            print(f'Pair started; pid={os.getpid()}; logs={args.log_dir}', flush=True)
            started = time.monotonic()
            while not state['stop']:
                if args.seconds and time.monotonic() - started >= args.seconds:
                    break
                if state['restart']:
                    state['restart'] = False
                    for uart, log, command in sessions:
                        log.write(f'\nRetry UTC: {datetime.now(timezone.utc).isoformat()}\n')
                        uart.write(command)
                        uart.flush()
                for uart, log, _ in sessions:
                    data = uart.read(uart.in_waiting or 1)
                    if data:
                        log.write(data.decode('ascii', errors='backslashreplace'))
                        log.flush()
                time.sleep(0.02)
        finally:
            for uart, _, _ in sessions:
                with suppress(serial.SerialException, OSError):
                    uart.write(b'x')
                    uart.flush()
            time.sleep(0.3)
            for uart, log, _ in sessions:
                with suppress(serial.SerialException, OSError):
                    log.write(uart.read(uart.in_waiting).decode('ascii', errors='backslashreplace'))
                    log.flush()
            (args.log_dir / 'pid').unlink(missing_ok=True)
            print('Pair sessions closed; stop commands sent where ports remained available.', flush=True)


if __name__ == '__main__':
    try:
        main()
    except (OSError, serial.SerialException) as error:
        print(f'Pair runner failed: {error}', file=sys.stderr)
        sys.exit(1)
