#!/usr/bin/env python3
"""Analyze diagnostic firmware logs without using a target HR to choose a peak."""

import argparse
import json
import re
from pathlib import Path
import numpy as np
from scipy import signal

p = argparse.ArgumentParser()
p.add_argument("log", type=Path)
p.add_argument("--channel", choices=["red", "ir"], default="red")
p.add_argument("--fs", type=float, default=100)
args = p.parse_args()
text = args.log.read_text(errors="replace")
samples = []
stamps = []
for line in text.splitlines():
    tag = r"(?:BVP|PPG)_RAW" if args.channel == "red" else r"(?:BVP|PPG)_IR"
    m = re.search(r"\((\d+)\).*\[" + tag + r"\] ([0-9,]+)", line)
    if m:
        batch = [int(x) for x in m[2].split(",")]
        samples.extend(batch)
        stamps.append((int(m[1]), len(batch)))
x = np.asarray(samples, dtype=float)
report = {
    "samples": len(x),
    "temperature_factory": [l for l in text.splitlines() if "Factory calibration" in l],
    "warnings_errors": [l for l in text.splitlines() if re.match(r"[EW] \(", l)],
    "device_hr": [float(v) for v in re.findall(r"\[(?:BVP|PPG)(?:_HR)?\] HR=([0-9.]+)", text)],
}
for name, pattern in [
    ("temperature", r"\[TEMP\] SKIN-TEMP=([-0-9.]+).*DEVICE-TEMP: ([-0-9.]+)"),
    ("eda", r"\[EDA\] Raw=(\d+).*Voltage=(\d+) mV"),
]:
    values = np.array(re.findall(pattern, text), dtype=float)
    if values.size:
        report[name] = {
            "count": len(values),
            "mean": values.mean(axis=0).tolist(),
            "std": values.std(axis=0).tolist(),
            "min": values.min(axis=0).tolist(),
            "max": values.max(axis=0).tolist(),
        }
if len(x) > 500:
    fs = args.fs
    report["sampling_rate_observed_hz"] = (
        1000 * (len(x) - stamps[0][1]) / (stamps[-1][0] - stamps[0][0])
    )
    report["raw"] = {
        "mean": float(x.mean()),
        "std": float(x.std()),
        "min": float(x.min()),
        "max": float(x.max()),
        "below_5000_fraction": float(np.mean(x < 5000)),
        "near_clipping_fraction": float(np.mean(x >= 0x3FFFF * 0.98)),
    }
    sos = signal.butter(3, [0.65, 3.5], fs=fs, btype="bandpass", output="sos")
    filtered = signal.sosfiltfilt(sos, x)
    report["windows"] = []
    window = int(10 * fs)
    for start in range(int(2 * fs), len(x) - window + 1, window):
        raw = x[start : start + window]
        v = filtered[start : start + window]
        freq, power = signal.periodogram(v, fs=fs, nfft=16384)
        lo = (freq >= 0.65) & (freq <= 3.5)
        peaks, _ = signal.find_peaks(power)
        peaks = sorted(
            (i for i in peaks if lo[i]), key=lambda i: power[i], reverse=True
        )[:4]
        ac = signal.correlate(v, v, mode="full", method="fft")[len(v) - 1 :]
        ac = ac / (np.arange(len(v), 0, -1) * np.var(v))
        lagpeaks, _ = signal.find_peaks(ac)
        lagpeaks = [i for i in lagpeaks if fs / 3.5 <= i <= fs / 0.65]
        lag = max(lagpeaks, key=lambda i: ac[i]) if lagpeaks else None
        report["windows"].append(
            {
                "start_s": start / fs,
                "dc": float(raw.mean()),
                "ac_std": float(v.std()),
                "spectral_peaks_bpm": [round(float(freq[i] * 60), 2) for i in peaks],
                "acf_bpm": round(60 * fs / lag, 2) if lag else None,
                "acf_correlation": round(float(ac[lag]), 3) if lag else None,
            }
        )
print(json.dumps(report, indent=2))
