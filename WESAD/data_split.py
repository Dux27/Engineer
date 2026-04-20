import os
import pickle
import numpy as np
from typing import List, Dict

PKL_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data_test")  

LABELING_FREQ = 700
FREQUENCIES_CHEST = 700
FREQUENCIES_WRIST = {
    "ACC": 32,
    "BVP": 64,
    "EDA": 4,
    "TEMP": 4
}

VALID_LABELS = {
    1: "baseline",
    2: "stress",
    3: "amusement",
    4: "meditation"
}

WRIST_SENSORS = ["ACC", "BVP", "EDA", "TEMP"]
CHEST_SENSORS = ["ACC", "ECG", "EMG", "EDA", "Resp", "Temp"]

TIME_WINDOW = 10.0  # seconds


def initializeLabelsDict(sensors: List[str]) -> Dict:
    """Initialize nested dictionary for storing labeled sensor data.
    dict[sensor][label] = list of data windows"""
    return {
        sensor: {label: [] for label in VALID_LABELS.values()}
        for sensor in sensors
    }


def valueToKey(d: Dict, value: str) -> int:
    """Get dictionary key by value."""
    for k, v in d.items():
        if v == value:
            return k
    raise KeyError(f"{value!r} not found in dictionary")


def getSensorFrequency(location: str, sensor: str) -> int:
    """Get sampling frequency for a given sensor."""
    if location == "wrist":
        return FREQUENCIES_WRIST[sensor]
    return FREQUENCIES_CHEST


def isolateSignal(data: Dict, location: str, sensor: str, label: str) -> np.ndarray:
    """Extract sensor data for a specific label."""
    if location not in ["wrist", "chest"]:
        raise ValueError(f"Invalid sensor location: {location}")
    
    try:
        labels = data['label']
    except KeyError:
        raise KeyError(f"The data does not contain 'label' key.")
    
    try:
        data_sensor = data['signal'][location][sensor]
    except KeyError:
        raise KeyError(f"The data does not contain sensor '{sensor}'")

    # Align 700 Hz labels with sensor's sampling rate
    label_positions = np.linspace(0, len(labels) - 1, len(data_sensor))
    label_indices = np.clip(np.round(label_positions).astype(int), 0, len(labels) - 1)
    labels_aligned = labels[label_indices]
    
    # Filter by label
    label_idx = valueToKey(VALID_LABELS, label)
    sensor_mask = labels_aligned == label_idx
    isolated_data = data_sensor[sensor_mask]

    # Calculate and print duration
    freq = getSensorFrequency(location, sensor)
    duration = isolated_data.shape[0] / freq
    
    # DEBUG
    # print(f"  {location:5s} {sensor:4s} {label:10s}: {duration:7.2f}s")

    return isolated_data


def divideIsolatedSignal(signal: List, location: str, sensor: str) -> List:
    freq = getSensorFrequency(location, sensor)
    window_samples = int(TIME_WINDOW * freq)     # Number of samples per window

    # Calculate number of complete windows
    num_windows = len(signal) // window_samples

    # Truncate to fit exact windows (discard remainder)
    truncated_length = num_windows * window_samples
    truncated_signal = signal[:truncated_length]

    # Reshape based on dimensions
    if signal.ndim == 1:
        divided_signal = truncated_signal.reshape(num_windows, window_samples)
    else:
        # Multi-dimensional signal (ACC with x,y,z axes)
        # Use -1 to get last dimension
        divided_signal = truncated_signal.reshape(num_windows, window_samples, -1)
    
    return list(divided_signal)


def processSensors(data: Dict, location: str, sensors: List[str], 
                   labels_dict: Dict) -> None:
    """Process all sensors for a given location."""
    for sensor in sensors:
        for label in VALID_LABELS.values():
            isolated = isolateSignal(data, location, sensor, label)
            windowed = divideIsolatedSignal(isolated, location, sensor)
            labels_dict[sensor][label].extend(windowed)
            
            num_windows = len(labels_dict[sensor][label])
            total_duration = num_windows * TIME_WINDOW
            
            # DEBUG
            # print(f"   - Windows created: {len(windowed)}")
            # print(f"   - Total: {num_windows} windows ({total_duration:.2f}s)")


def loadPickleFiles() -> List[str]:
    """Get list of pickle files in data directory."""
    return [f for f in os.listdir(PKL_DIR) if f.endswith(".pkl")]


def main():
    """Main processing loop."""
    wrist_labels = initializeLabelsDict(WRIST_SENSORS)
    chest_labels = initializeLabelsDict(CHEST_SENSORS)
    
    pickle_files = loadPickleFiles()
    print(f"Found {len(pickle_files)} pickle files in '{PKL_DIR}'.\n")

    progress = 0
    for pkl_file in pickle_files:
        progress += 1
        print(f"Preprocessing progress: {progress / len(pickle_files) * 100:.2f}% - {pkl_file}")
        
        with open(os.path.join(PKL_DIR, pkl_file), "rb") as f:
            data = pickle.load(f, encoding='latin1')

        processSensors(data, "wrist", WRIST_SENSORS, wrist_labels)
        processSensors(data, "chest", CHEST_SENSORS, chest_labels)
    
    return wrist_labels, chest_labels
