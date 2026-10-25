"""Split WESAD wrist signals into labeled, fixed-length windows."""

import pickle
from pathlib import Path

import numpy as np

SPLIT = ["train", "val", "test"]  # train, val, test
OUT_DIR = Path(__file__).resolve().parent.parent / "wesad_dataset"

LABELING_FREQ = 700
TIME_WINDOW = 12.0  # seconds

FREQUENCIES_WRIST = {
    "ACC": 32,
    "BVP": 64,
    "EDA": 4,
    "TEMP": 4,
}

VALID_LABELS = {
    1: "baseline",
    2: "stress",
    3: "amusement",
    4: "meditation",
}

WRIST_SENSORS = ["ACC", "BVP", "EDA", "TEMP"]


def getSensorFrequency(location: str, sensor: str) -> int:
    """Return the sample rate of a wrist sensor."""
    if location != "wrist":
        raise ValueError(f"Unsupported location: {location}")
    return FREQUENCIES_WRIST[sensor]


def isolateSignal(data: dict, sensor: str, label: int) -> np.ndarray:
    """Select samples belonging to one WESAD label."""
    labels = np.asarray(data["label"])
    signal = np.asarray(data["signal"]["wrist"][sensor])

    # WESAD labels are sampled at 700 Hz; each wrist sensor has its own rate.
    positions = np.linspace(0, len(labels) - 1, len(signal))
    aligned_labels = labels[np.rint(positions).astype(int)]
    return signal[aligned_labels == label]


def windowIsolatedSignal(signal: np.ndarray, sensor: str) -> list[np.ndarray]:
    """Keep complete, non-overlapping windows and discard the remainder."""
    samples = int(TIME_WINDOW * FREQUENCIES_WRIST[sensor])
    count = len(signal) // samples
    return list(signal[: count * samples].reshape(count, samples, *signal.shape[1:]))


def processSensors(data: dict) -> dict:
    """Return sensor -> label -> list of windows for one subject."""
    return {
        sensor: {
            label_name: windowIsolatedSignal(isolateSignal(data, sensor, label_id), sensor)
            for label_id, label_name in VALID_LABELS.items()
        }
        for sensor in WRIST_SENSORS
    }


def main() -> tuple[dict, dict, dict]:
    """Load subjects into separate train, validation, and test dictionaries."""
    datasets = []

    for d in SPLIT:
        PKL_DIR = OUT_DIR / d
        paths = sorted(PKL_DIR.glob("*.pkl"))
        print(f"Found {len(paths)} subjects in {PKL_DIR}")

        wrist_data = {}
        for index, path in enumerate(paths, start=1):
            print(f"[{index}/{len(paths)}] Processing {path.stem}")
            with path.open("rb") as file:
                data = pickle.load(file, encoding="latin1")  # WESAD files must be trusted
            wrist_data[path.stem] = processSensors(data)

        datasets.append(wrist_data)

    return tuple(datasets)


if __name__ == "__main__":
    main()
