#!/usr/bin/env python3
"""Capture board Serial to a file with host timestamps (read-only, no writes to the board).

Uses the pyserial bundled with PlatformIO. Opening the native USB port may reset
the board (USB_UART_CHIP_RESET); that is expected. Stop with Ctrl+C or --minutes.

Example:
  python tools/serial_capture.py --port COM5 --out ~/xuna_backups/<dir>/board-trial.log --minutes 120
"""
import argparse
import datetime
import time
from pathlib import Path

import serial


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--minutes", type=float, default=0, help="0 = until Ctrl+C")
    args = parser.parse_args()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    deadline = time.monotonic() + args.minutes * 60 if args.minutes > 0 else None
    port = serial.Serial()
    port.port, port.baudrate, port.timeout = args.port, args.baud, 1
    port.dtr = port.rts = False  # matches monitor_dtr/monitor_rts = 0
    port.open()
    with port, args.out.open("a", encoding="utf-8") as log:
        try:
            while deadline is None or time.monotonic() < deadline:
                raw = port.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                stamp = datetime.datetime.now().isoformat(timespec="seconds")
                log.write(f"{stamp} {line}\n")
                log.flush()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
