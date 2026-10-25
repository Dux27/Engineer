"""Extract WESAD wrist features into aligned X/y Parquet files."""

import random
import warnings
from pathlib import Path

import data_split
import neurokit2 as nk
import numpy as np
import pandas as pd
from neurokit2.misc import NeuroKitWarning

CAL = True
OUT_DIR = Path(__file__).resolve().parent.parent / "ml_pipeline" / "datasets"

warnings.filterwarnings(
    "ignore",
    message="EDA signal is sampled at very low frequency.*",
    category=NeuroKitWarning,
)

def basic_stats(signal: np.ndarray, prefix: str) -> dict:
    x = np.asarray(signal, dtype=float).ravel()
    return {
        f"{prefix}_mean": float(np.mean(x)),
        f"{prefix}_median": float(np.median(x)),
        f"{prefix}_std": float(np.std(x)),
        f"{prefix}_min": float(np.min(x)),
        f"{prefix}_max": float(np.max(x)),
        f"{prefix}_range": float(np.ptp(x)),
        f"{prefix}_delta": float(x[-1] - x[0]),
    }


def slope_feature(signal: np.ndarray, prefix: str, sensor: str) -> dict:
    x = np.asarray(signal, dtype=float).ravel()
    fs = data_split.getSensorFrequency("wrist", sensor)
    slope = np.polyfit(np.arange(x.size) / fs, x, 1)[0]
    return {f"{prefix}_slope": float(slope)}


def acc_features(signal: np.ndarray) -> dict:
    acc = np.asarray(signal, dtype=float)
    if acc.ndim != 2 or acc.shape[1] != 3:
        raise ValueError("Wrist ACC must have shape (samples, 3)")

    magnitude = np.linalg.norm(acc, axis=1) / 64.0  # WESAD E4: 1/64 g
    changes = np.diff(magnitude)
    changes[np.abs(changes) < 0.005] = 0

    return {
        "wrist_ACC_mag_mean_nc": float(np.mean(magnitude)),
        "wrist_ACC_mag_std_nc": float(np.std(magnitude)),
        "wrist_ACC_mag_energy_nc": float(np.sum(magnitude**2)),
        "wrist_ACC_mag_mad_nc": float(np.mean(np.abs(magnitude - np.mean(magnitude)))),
        "wrist_ACC_mag_range_nc": float(np.ptp(magnitude)),
        "wrist_ACC_mag_zero_crossings_nc": int(np.sum(changes[:-1] * changes[1:] < 0)),
    }


def eda_scr_features(signal: np.ndarray) -> dict:
    eda = np.asarray(signal, dtype=float).ravel()
    prefix = "wrist_EDA"
    # NaN means the feature could not be estimated, not that it measured zero.
    if eda.size < 10 or np.std(eda) < 1e-8:
        return {
            f"{prefix}_scr_count_nc": np.nan,
            f"{prefix}_scr_mean_amp": np.nan,
            f"{prefix}_scr_max_amp": np.nan,
            f"{prefix}_scr_sum_amp": np.nan,
        }

    signals, _ = nk.eda_process(eda, sampling_rate=data_split.getSensorFrequency("wrist", "EDA"))
    amplitudes = np.asarray(signals["SCR_Amplitude"], dtype=float)
    amplitudes = amplitudes[np.isfinite(amplitudes) & (amplitudes > 0)]
    return {
        f"{prefix}_scr_count_nc": int(amplitudes.size),
        f"{prefix}_scr_mean_amp": float(np.mean(amplitudes)) if amplitudes.size else np.nan,
        f"{prefix}_scr_max_amp": float(np.max(amplitudes)) if amplitudes.size else np.nan,
        f"{prefix}_scr_sum_amp": float(np.sum(amplitudes)),
    }


def bvp_features(signal: np.ndarray) -> dict:
    bvp = np.asarray(signal, dtype=float).ravel()
    names = ("hr_mean", "hr_std", "ppg_amp_mean", "ppg_amp_std")
    features = {f"wrist_BVP_{name}": np.nan for name in names}
    if bvp.size < 10 or np.std(bvp) < 1e-8:
        return features

    fs = data_split.getSensorFrequency("wrist", "BVP")
    clean = np.asarray(nk.ppg_clean(bvp, sampling_rate=fs))
    _, info = nk.ppg_peaks(clean, sampling_rate=fs)
    peaks = np.asarray(info["PPG_Peaks"], dtype=int)

    if peaks.size >= 2:
        heart_rate = 60.0 / (np.diff(peaks) / fs)
        features["wrist_BVP_hr_mean"] = float(np.mean(heart_rate))
        features["wrist_BVP_hr_std"] = float(np.std(heart_rate))
    if peaks.size:
        amplitudes = clean[peaks]
        features["wrist_BVP_ppg_amp_mean"] = float(np.mean(amplitudes))
        features["wrist_BVP_ppg_amp_std"] = float(np.std(amplitudes))
    return features


def extract_features(subject: str, label: str, sensors: dict, index: int) -> dict:
    features = {"subject": subject, "label": label}
    for sensor in ("ACC", "BVP", "EDA", "TEMP"):
        window = sensors[sensor][label][index]
        if sensor == "ACC":
            features.update(acc_features(window))
        elif sensor == "BVP":
            features.update(bvp_features(window))
        elif sensor == "EDA":
            features.update(basic_stats(window, "wrist_EDA"))
            features.update(slope_feature(window, "wrist_EDA", "EDA"))
            features.update(eda_scr_features(window))
        elif sensor == "TEMP":
            features.update(basic_stats(window, "wrist_TEMP"))
            features.update(slope_feature(window, "wrist_TEMP", "TEMP"))
    return features


def calibrate_by_subject_baseline(dataset: list[dict]) -> list[dict]:
    """Append baseline-normalized features; retain original measurements."""
    df = pd.DataFrame(dataset)
    columns = [c for c in df if c not in ("subject", "label") and not c.endswith("_nc")]
    baseline = df.loc[df["label"] == "baseline"].groupby("subject")[columns]
    means, stds = baseline.mean(), baseline.std()

    # Map each row to the statistics of its own subject, without using other subjects.
    mu = means.reindex(df["subject"]).set_axis(df.index)
    sigma = stds.reindex(df["subject"]).set_axis(df.index).replace(0, np.nan)
    calibrated = (df[columns] - mu) / sigma
    calibrated.columns = [f"{column}_cal" for column in columns]
    return pd.concat([df, calibrated], axis=1).to_dict("records")


def main() -> None:
    datasets = data_split.main()

    for split, wrist_data in zip(data_split.SPLIT, datasets):
        dataset = []

        for subject, sensors in wrist_data.items():
            for label in data_split.VALID_LABELS.values():
                count = min(len(sensors[sensor][label]) for sensor in data_split.WRIST_SENSORS)
                for index in range(count):
                    dataset.append(extract_features(subject, label, sensors, index))
            print(f"Extracted {subject}")

        if not dataset:
            raise ValueError(f"No windows found in {split} dataset")
        if CAL:
            dataset = calibrate_by_subject_baseline(dataset)
        random.Random(42).shuffle(dataset)

        df = pd.DataFrame(dataset)
        output_dir = OUT_DIR / f"{split}_{int(data_split.TIME_WINDOW)}s{'_cal' if CAL else ''}"
        output_dir.mkdir(parents=True, exist_ok=True)
        df.drop(columns="label").to_parquet(output_dir / "X.parquet", index=False)
        df[["label"]].to_parquet(output_dir / "y.parquet", index=False)
        print(f"Saved {len(df)} windows to {output_dir}")


if __name__ == "__main__":
    main()
