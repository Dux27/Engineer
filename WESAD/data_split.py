import os
import pickle
import numpy as np
from typing import List, Dict

PKL_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data_val")  

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

WRIST_SENSORS = ["ACC",    # Accelerometer [x, y, z] (1/64 g)
                 "BVP",    # Blood Volume Pulse 
                 "EDA",    # Electrodermal Activity (uS)
                 "TEMP"]   # Skin Temperature (°C)

CHEST_SENSORS = ["ACC",    # Accelerometer [x, y, z] (g)
                 "ECG",    # Electrocardiogram (mV)
                 "EMG",    # Electromyogram (mV)
                 "EDA",    # Electrodermal Activity (uS)
                 "Resp",   # Respiration 
                 "Temp"]   # Skin Temperature (°C)

TIME_WINDOW = 12.0  # seconds


def initializeLabelsDict(sensors: List[str]) -> Dict:
    """Initialize nested dictionary for storing labeled sensor data.
    dict[subject][sensor][label] = list of data windows"""
    return {
        subject: {
            sensor: {
                label: [] for label in VALID_LABELS.values()
            }
            for sensor in sensors
        }
        for subject in [f.replace(".pkl", "") for f in os.listdir(PKL_DIR) if f.endswith(".pkl")]
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
    
    # DEBUG
    # Calculate and print duration
    # freq = getSensorFrequency(location, sensor)
    # duration = isolated_data.shape[0] / freq
    # print(f"  {location:5s} {sensor:4s} {label:10s}: {duration:7.2f}s")

    return isolated_data


def windowIsolatedSignal(signal: List, location: str, sensor: str) -> List:
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


def processSensors(data: Dict, location: str, sensors: List[str], subject: str,  labels_dict: Dict) -> None:
    """Process all sensors for a given location."""
    total_operations = len(sensors) * len(VALID_LABELS.values())
    current_operation = 0
    
    for sensor in sensors:
        for label in VALID_LABELS.values():
            current_operation += 1
            progress = (current_operation / total_operations) * 100
            print(f"\r  Processing {location}: {progress:.1f}% - {sensor}/{label}", end="", flush=True)
            
            isolated = isolateSignal(data, location, sensor, label)
            windowed = windowIsolatedSignal(isolated, location, sensor)

            labels_dict[subject][sensor][label].extend(windowed)
    
    print()  


def loadPickleFiles() -> List[str]:
    """Get list of pickle files in data directory."""
    return [f for f in os.listdir(PKL_DIR) if f.endswith(".pkl")]


def main():
    """Main processing loop."""
    wrist_labels = initializeLabelsDict(WRIST_SENSORS)
    chest_labels = initializeLabelsDict(CHEST_SENSORS)
    
    pickle_files = loadPickleFiles()
    print(f"Found {len(pickle_files)} pickle files in '{PKL_DIR}'.\n")

    for idx, pkl_file in enumerate(pickle_files, 1):
        # Progress display
        progress = (idx / len(pickle_files)) * 100
        print(f"File {idx}/{len(pickle_files)} ({progress:.1f}%): {pkl_file}")
        
        subject = pkl_file.replace(".pkl", "")

        with open(os.path.join(PKL_DIR, pkl_file), "rb") as f:
            data = pickle.load(f, encoding='latin1')

        processSensors(data, "wrist", WRIST_SENSORS, subject, wrist_labels)
        processSensors(data, "chest", CHEST_SENSORS, subject, chest_labels)
        
        print()  
    
    print("Data preprocessing complete!\n")
    return wrist_labels, chest_labels


if __name__ == "__main__":
    main()