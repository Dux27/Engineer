#!/usr/bin/env python3
"""Capture real diagnostic UART/USB logs; run with the ESP-IDF Python environment."""

import argparse
import subprocess
import sys
import time
from pathlib import Path
import os
import fcntl

p = argparse.ArgumentParser()
p.add_argument("--port", default="/dev/ttyACM0")
p.add_argument("--duration", type=float, default=60)
p.add_argument(
    "--reset",
    action="store_true",
    help="reset into existing flash app; never writes flash",
)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
if a.duration <= 0:
    p.error("duration must be positive")
# Acquire the same advisory lock as tools/sensor_monitor.py BEFORE reset or
# configuring the serial port. Two readers must never split the byte stream.
lock_fd = os.open(a.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
try:
    try:
        fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        p.exit(
            1,
            "Serial port is busy. Use the monitor --log recording instead of a second reader.\n",
        )
    import serial

    a.output.parent.mkdir(parents=True, exist_ok=True)
    if a.reset:
        subprocess.run(
            [
                sys.executable,
                "-m",
                "esptool",
                "--chip",
                "esp32s3",
                "--port",
                a.port,
                "--before",
                "usb_reset",
                "--after",
                "hard_reset",
                "--no-stub",
                "read_mac",
            ],
            check=True,
            timeout=25,
        )
    count = 0
    with (
        serial.Serial(a.port, 115200, timeout=0.1) as device,
        a.output.open("wb") as output,
    ):
        end = time.monotonic() + a.duration
        try:
            while time.monotonic() < end:
                chunk = device.read(device.in_waiting or 1)
                output.write(chunk)
                count += len(chunk)
        except KeyboardInterrupt:
            pass
    print(f"Saved {count} bytes to {a.output}")
finally:
    os.close(lock_fd)
