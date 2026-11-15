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
import sys
from dataclasses import dataclass
from pathlib import Path

ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
NUMBER = r"([-+]?\d+(?:\.\d+)?)"
ADXL_ACC = re.compile(
    r"\[ACC\] ACC: X="
    + NUMBER
    + r" Y="
    + NUMBER
    + r" Z="
    + NUMBER
    + r" \| SENSOR=ADXL345"
)
TEMP = re.compile(r"\[TEMP\] SKIN-TEMP=" + NUMBER + r" C \| DEVICE-TEMP: " + NUMBER)
PPG = re.compile(r"\[PPG\] HR=" + NUMBER + r" bpm \| PPI=(\d+) ms")
PPG_HR = re.compile(r"\[PPG_HR\] HR=" + NUMBER + r" bpm")
EDA = re.compile(
    r"\[EDA\] Raw=(\d+) \| Voltage=(?:(\d+) mV|unavailable) \| Clipped=(yes|no)"
)
NAMES = ("ADXL345", "MAX30102", "TPiS1385", "GroveGSR")


@dataclass
class Sensor:
    values: tuple | None = None
    updated: float | None = None
    status: str = "Waiting for data"
    last_values: tuple | None = None

    def __post_init__(self):
        if self.values is not None:
            self.last_values = self.values


class Dashboard:
    def __init__(self):
        self.sensors = {name: Sensor() for name in NAMES}
        self.buffer = ""
        self.device_ms = None
        self.notice = "No warnings or errors"
        self.lines = 0
        self.ppg_quality = "waiting"

    def feed(self, text, now):
        self.buffer += text
        # Process complete frames before bounding the unfinished tail. A large
        # read after a brief pause must not discard earlier valid HR messages.
        lines = self.buffer.split("\n")
        self.buffer = lines.pop()[-16384:]
        for line in lines:
            self.parse(ANSI.sub("", line).strip(), now)

    def parse(self, line, now):
        self.lines += 1
        stamp = re.search(r"\((\d+)\)", line)
        if stamp:
            device_ms = int(stamp[1])
            if self.device_ms is not None and device_ms < self.device_ms:
                self.sensors = {name: Sensor() for name in NAMES}
                self.notice = "Board restarted; previous readings cleared"
                self.ppg_quality = "waiting"
            self.device_ms = device_ms
        for name, pattern in (
            ("ADXL345", ADXL_ACC),
            ("MAX30102", PPG),
            ("TPiS1385", TEMP),
        ):
            match = pattern.search(line)
            if match:
                self.sensors[name] = Sensor(
                    tuple(map(float, match.groups())), now, "OK"
                )
                return
        eda = EDA.search(line)
        waveform_hr = PPG_HR.search(line)
        if waveform_hr:
            self.sensors["MAX30102"] = Sensor(
                (float(waveform_hr[1]), None),
                now,
                "IR waveform estimate; pulse intervals reported separately",
            )
            return
        if eda:
            raw, voltage, clipped = eda.groups()
            status = "ADC clipped; check signal range" if clipped == "yes" else "OK"
            if voltage is None and clipped != "yes":
                status = "Raw only; ADC voltage calibration unavailable"
            self.sensors["GroveGSR"] = Sensor(
                (int(raw), int(voltage) if voltage is not None else None), now, status
            )
            return
        if "[PPG] PPI unavailable" in line:
            self.sensors["MAX30102"].status = "Beat candidate; no valid interval"
        if "[PPG_QUALITY]" in line:
            quality = re.search(r"status=(\w+).*periodicity=([-0-9.]+)", line)
            if quality:
                self.ppg_quality = f"{quality[1]} r={float(quality[2]):.2f}"
            if "status=unreliable" in line:
                self.sensors["MAX30102"].values = None
                self.sensors[
                    "MAX30102"
                ].status = (
                    "Weak/nonperiodic PPG; HR unavailable (or initial 8 s settling)"
                )
            else:
                self.sensors[
                    "MAX30102"
                ].status = "Periodic IR signal; waiting for stable waveform estimate"
        for name in NAMES:
            if name + ":" in line and "initialized successfully" in line.lower():
                self.sensors[name].status = "Initialized; waiting for measurement"
            if "Failed to read " + name in line or (
                name + ":" in line and re.match(r"[EW] \(", line)
            ):
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
        update_label = (
            "Last sample update" if name == "GroveGSR" else "Last valid update"
        )
        rows.append(f"{name}  |  {update_label}: {age(sensor, now)}")
        if sensor.values is None:
            if name == "MAX30102" and sensor.last_values is not None:
                rows.append(
                    f"  HR unavailable now | Last accepted HR: {sensor.last_values[0]:.1f} bpm"
                )
            else:
                rows.append("  --")
        elif name == "ADXL345":
            x, y, z = sensor.values
            rows.append(f"  Acceleration: X {x:7.3f}  Y {y:7.3f}  Z {z:7.3f} m/s²")
        elif name == "MAX30102":
            hr, ppi = sensor.values
            ppi_label = f"{ppi:.0f} ms" if ppi is not None else "unavailable"
            rows.append(f"  Heart rate: {hr:.1f} bpm  |  Pulse interval: {ppi_label}")
        elif name == "GroveGSR":
            raw, voltage = sensor.values
            voltage_label = f"{voltage} mV" if voltage is not None else "unavailable"
            rows.append(f"  EDA raw: {raw}/4095  |  Voltage: {voltage_label}")
        else:
            skin, device = sensor.values
            rows.append(f"  Skin: {skin:.2f} °C  |  Device: {device:.2f} °C")
        rows += ["  " + sensor.status, ""]
    rows += [
        "PPG age tracks accepted HR estimates; PPI may be unavailable independently.",
        "Latest warning/error (retained):",
        "  " + model.notice,
    ]
    if height < len(rows) or width < 65:
        if height >= 10 and width >= 40:
            compact = rows[:3]
            for name in ("MAX30102", "ADXL345", "TPiS1385", "GroveGSR"):
                offset = 4 + 4 * NAMES.index(name)
                compact.append(f"{name}: {rows[offset + 1].strip()}")
                if height >= 16:
                    compact.append(
                        f"  {age(model.sensors[name], now)} | {model.sensors[name].status}"
                    )
            compact.append("Latest warning: " + model.notice)
            rows = compact
        else:
            rows = [
                "Enlarge terminal to 40 columns x 10 rows",
                "or restart with --plain. q: quit",
            ]
    # Update the existing screen, without clearing the terminal on every frame.
    for row in range(height):
        screen.move(row, 0)
        screen.clrtoeol()
        if row < len(rows):
            try:
                screen.addnstr(
                    row,
                    0,
                    rows[row],
                    max(0, width - 1),
                    curses.A_BOLD if row == 0 else 0,
                )
            except curses.error:
                pass
    screen.refresh()


def demo_lines(tick):
    stamp = tick * 100
    lines = [
        f"I ({stamp}) APP: [ACC] ACC: X=0.123 Y=-0.456 Z=9.807 | DEVICE-TEMP=25.30 C",
        f"I ({stamp}) APP: [ACC] ACC: X=0.123 Y=-0.456 Z=9.807 | SENSOR=ADXL345",
        f"I ({stamp}) APP: [TEMP] SKIN-TEMP=32.10 C | DEVICE-TEMP: 25.40 C",
        f"I ({stamp}) APP: [EDA] Raw=1800 | Voltage=1360 mV | Clipped=no",
    ]
    if tick % 8 == 0:
        lines.append(f"I ({stamp}) APP: [PPG] HR=75.0 bpm | PPI=800 ms")
    return "\n".join(lines) + "\n"


def run(screen, args, fd, logfile=None):
    try:
        curses.curs_set(0)
    except curses.error:
        pass
    screen.nodelay(True)
    model = Dashboard()
    start = time.monotonic()
    received = None
    last_demo = -1
    title = (
        "DEMO — simulated readings"
        if args.demo
        else f"{args.port}  |  {args.baud} baud"
    )
    while True:
        now = time.monotonic()
        if args.duration is not None and now - start >= args.duration:
            return
        key = screen.getch()
        if key in (ord("q"), ord("Q")):
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
                if logfile is not None:
                    logfile.write(chunk)
                    logfile.flush()
                model.feed(chunk.decode("utf-8", errors="replace"), now)
        draw(screen, model, title, now, received)
        time.sleep(0.1)


def plain_snapshot(model, now):
    ppg = model.sensors["MAX30102"]
    if ppg.values is not None:
        hr = f"HR: {ppg.values[0]:.1f} bpm ({age(ppg, now)})"
    elif ppg.last_values is not None:
        hr = f"HR: unavailable now; last accepted {ppg.last_values[0]:.1f} bpm ({age(ppg, now)})"
    else:
        hr = "HR: waiting for accepted estimate"
    temp = model.sensors["TPiS1385"]
    eda = model.sensors["GroveGSR"]
    temperature = f"{temp.values[0]:.2f} C" if temp.values else "--"
    voltage = (
        f"{eda.values[1]} mV" if eda.values and eda.values[1] is not None else "--"
    )
    acc = model.sensors["ADXL345"].values
    acceleration = ",".join(f"{v:.2f}" for v in acc[:3]) if acc else "--"
    return f"{hr} | quality: {model.ppg_quality} | TEMP: {temperature} | EDA: {voltage} | ACC: {acceleration}"


def run_plain(args, fd, logfile=None):
    model = Dashboard()
    start = last_output = time.monotonic()
    last_demo = -1
    source = "DEMO - simulated readings" if args.demo else args.port
    print(f"ESP32 sensor monitor | {source} | Ctrl+C to stop", flush=True)
    while args.duration is None or time.monotonic() - start < args.duration:
        now = time.monotonic()
        if args.demo:
            tick = int((now - start) * 10)
            if tick != last_demo:
                model.feed(demo_lines(tick), now)
                last_demo = tick
        elif select.select([fd], [], [], 0.1)[0]:
            try:
                chunk = os.read(fd, 65536)
            except BlockingIOError:
                chunk = None
            if chunk == b"":
                raise OSError("Serial device disconnected")
            if chunk:
                if logfile is not None:
                    logfile.write(chunk)
                    logfile.flush()
                model.feed(chunk.decode("utf-8", errors="replace"), now)
        if now - last_output >= 1:
            print(plain_snapshot(model, now), flush=True)
            last_output = now
        if args.demo:
            time.sleep(0.05)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument(
        "--baud",
        type=int,
        choices=(9600, 57600, 115200, 230400, 460800),
        default=115200,
    )
    parser.add_argument(
        "--demo", action="store_true", help="Show simulated data without a board"
    )
    parser.add_argument("--duration", type=float, help="Exit after this many seconds")
    parser.add_argument(
        "--log", type=Path, help="Record raw serial data to this file (overwrites it)"
    )
    parser.add_argument(
        "--plain",
        action="store_true",
        help="Print readable status once per second without curses",
    )
    args = parser.parse_args()
    fd = None
    original = None
    logfile = None
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
        if args.log:
            args.log.parent.mkdir(parents=True, exist_ok=True)
            logfile = args.log.open("wb")
        if args.plain or not sys.stdout.isatty():
            run_plain(args, fd, logfile)
        else:
            curses.wrapper(run, args, fd, logfile)
    except KeyboardInterrupt:
        pass
    except OSError as exc:
        hint = (
            " Close other serial monitors first."
            if exc.errno in (errno.EBUSY, errno.EAGAIN)
            else ""
        )
        parser.exit(1, f"Monitor error: {exc}.{hint}\n")
    except curses.error as exc:
        parser.exit(1, f"Terminal display error: {exc}. Restart with --plain.\n")
    finally:
        if logfile is not None:
            logfile.close()
        if fd is not None:
            if original is not None:
                try:
                    termios.tcsetattr(fd, termios.TCSANOW, original)
                except OSError:
                    pass
            os.close(fd)


if __name__ == "__main__":
    main()
