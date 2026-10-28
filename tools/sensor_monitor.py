#!/usr/bin/env python3

"""Fixed-position ESP32 sensor dashboard. Linux/macOS; Python standard library only."""

import argparse
import curses
import errno
import os
import re
import select
import termios
import time
from dataclasses import dataclass

ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
NUMBER = r"([-+]?\d+(?:\.\d+)?)"
ACC = re.compile(r"\[ACC\] ACC: X=" + NUMBER + r" Y=" + NUMBER + r" Z=" + NUMBER + r" \| DEVICE-TEMP=" + NUMBER)
TEMP = re.compile(r"\[TEMP\] SKIN-TEMP=" + NUMBER + r" C \| DEVICE-TEMP: " + NUMBER)
PPG = re.compile(r"\[PPG\] HR=" + NUMBER + r" bpm \| PPI=(\d+) ms")
NAMES = ("MPU6050", "MAX30105", "TPiS1385")


@dataclass
class Sensor:
    values: tuple | None = None
    updated: float | None = None
    status: str = "Waiting for data"


class Dashboard:
    def __init__(self):
        self.sensors = {name: Sensor() for name in NAMES}
        self.buffer = ""
        self.device_ms = None
        self.notice = "No warnings or errors"
        self.lines = 0

    def feed(self, text, now):
        self.buffer += text
        # Bound memory even if the device emits an unterminated line.
        self.buffer = self.buffer[-16384:]
        while "\n" in self.buffer:
            line, self.buffer = self.buffer.split("\n", 1)
            self.parse(ANSI.sub("", line).strip(), now)

    def parse(self, line, now):
        self.lines += 1
        stamp = re.search(r"\((\d+)\)", line)
        if stamp:
            device_ms = int(stamp[1])
            if self.device_ms is not None and device_ms < self.device_ms:
                self.sensors = {name: Sensor() for name in NAMES}
                self.notice = "Board restarted; previous readings cleared"
            self.device_ms = device_ms
        for name, pattern in (("MPU6050", ACC), ("MAX30105", PPG), ("TPiS1385", TEMP)):
            match = pattern.search(line)
            if match:
                self.sensors[name] = Sensor(tuple(map(float, match.groups())), now, "OK")
                return
        if "[PPG] PPI unavailable" in line:
            self.sensors["MAX30105"].status = "Beat candidate; no valid interval"
        for name in NAMES:
            if name + ":" in line and "initialized successfully" in line.lower():
                self.sensors[name].status = "Initialized; waiting for measurement"
            if "Failed to read " + name in line or (name + ":" in line and re.match(r"[EW] \(", line)):
                self.sensors[name].status = line.split(":", 1)[-1].strip()
        if re.match(r"[EW] \(", line):
            self.notice = line


def age(sensor, now):
    if sensor.updated is None:
        return "no measurement yet"
    elapsed = max(0, now - sensor.updated)
    return f"{elapsed:.1f}s ago" + ("  [STALE]" if elapsed > 3 else "")


def draw(screen, model, title, now, received):
    height, width = screen.getmaxyx()
    rows = [
        "SENSOR MONITOR  |  q: quit",
        title,
        f"Serial activity: {('waiting' if received is None else f'{now - received:.1f}s ago')}  |  Lines: {model.lines}",
        "",
    ]
    for name in NAMES:
        sensor = model.sensors[name]
        rows.append(f"{name}  |  Last valid update: {age(sensor, now)}")
        if sensor.values is None:
            rows.append("  --")
        elif name == "MPU6050":
            x, y, z, temperature = sensor.values
            rows.append(f"  Acceleration: X {x:7.3f}  Y {y:7.3f}  Z {z:7.3f} m/s²  |  Device {temperature:.2f} °C")
        elif name == "MAX30105":
            hr, ppi = sensor.values
            rows.append(f"  Heart rate: {hr:.1f} bpm  |  Pulse interval: {ppi:.0f} ms")
        else:
            skin, device = sensor.values
            rows.append(f"  Skin: {skin:.2f} °C  |  Device: {device:.2f} °C")
        rows += ["  " + sensor.status, ""]
    rows += ["PPG age tracks valid beat estimates, not raw sample acquisition.",
             "Latest warning/error (retained):", "  " + model.notice]
    if height < len(rows) or width < 65:
        rows = ["Enlarge terminal (at least 65 columns x 20 rows).", "q: quit"]
    # Update the existing screen, without clearing the terminal on every frame.
    for row in range(height):
        screen.move(row, 0)
        screen.clrtoeol()
        if row < len(rows):
            try:
                screen.addnstr(row, 0, rows[row], max(0, width - 1), curses.A_BOLD if row == 0 else 0)
            except curses.error:
                pass
    screen.refresh()


def demo_lines(tick):
    stamp = tick * 100
    lines = [f"I ({stamp}) APP: [ACC] ACC: X=0.123 Y=-0.456 Z=9.807 | DEVICE-TEMP=25.30 C",
             f"I ({stamp}) APP: [TEMP] SKIN-TEMP=32.10 C | DEVICE-TEMP: 25.40 C"]
    if tick % 8 == 0:
        lines.append(f"I ({stamp}) APP: [PPG] HR=75.0 bpm | PPI=800 ms")
    return "\n".join(lines) + "\n"


def run(screen, args, fd):
    try:
        curses.curs_set(0)
    except curses.error:
        pass
    screen.nodelay(True)
    model = Dashboard()
    start = time.monotonic()
    received = None
    last_demo = -1
    title = "DEMO — simulated readings" if args.demo else f"{args.port}  |  {args.baud} baud"
    while True:
        now = time.monotonic()
        if args.duration is not None and now - start >= args.duration:
            return
        key = screen.getch()
        if key in (ord("q"), ord("Q"), 27):
            return
        if args.demo:
            tick = int((now - start) * 10)
            if tick != last_demo:
                model.feed(demo_lines(tick), now)
                received = now
                last_demo = tick
        elif select.select([fd], [], [], 0)[0]:
            try:
                chunk = os.read(fd, 65536)
            except BlockingIOError:
                chunk = None
            if chunk == b"":
                raise OSError("Serial device disconnected")
            if chunk:
                received = now
                model.feed(chunk.decode("utf-8", errors="replace"), now)
        draw(screen, model, title, now, received)
        time.sleep(0.1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--baud", type=int, choices=(9600, 57600, 115200, 230400, 460800), default=115200)
    parser.add_argument("--demo", action="store_true", help="Show simulated data without a board")
    parser.add_argument("--duration", type=float, help="Exit after this many seconds")
    args = parser.parse_args()
    fd = None
    original = None
    try:
        if not args.demo:
            fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            # Prevent another monitor from consuming bytes from the same port.
            import fcntl
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            original = termios.tcgetattr(fd)
            settings = termios.tcgetattr(fd)
            settings[0] = settings[1] = settings[3] = 0
            settings[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            settings[4] = settings[5] = getattr(termios, f"B{args.baud}")
            settings[6][termios.VMIN] = 0
            settings[6][termios.VTIME] = 0
            termios.tcsetattr(fd, termios.TCSANOW, settings)
        curses.wrapper(run, args, fd)
    except KeyboardInterrupt:
        pass
    except OSError as exc:
        hint = " Close other serial monitors first." if exc.errno in (errno.EBUSY, errno.EAGAIN) else ""
        parser.exit(1, f"Monitor error: {exc}.{hint}\n")
    finally:
        if fd is not None:
            if original is not None:
                try:
                    termios.tcsetattr(fd, termios.TCSANOW, original)
                except OSError:
                    pass
            os.close(fd)


if __name__ == "__main__":
    main()
