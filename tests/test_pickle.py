import os
import pickle
import numpy as np
import matplotlib.pyplot as plt

LABELING_FREQ = 700          # Hz. Its the frequency of the highest frequency signal
FREQUENCIES_CHEST = 700      # For chest, all signals are at 700 Hz
FREQUENCIES_WRIST = {        # For wrist, different signals have different frequencies
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

PKL = "S14.pkl"
PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
with open(os.path.join(PATH, PKL), "rb") as f:
    data = pickle.load(f, encoding='latin1')
    
def ValueToKey(d, value):
    for k, v in d.items():
        if v == value:
            return k
    raise KeyError(f"{value!r} not found")

def plotSignalWithSelectedLabel(location: str, sensor: str, label: str) -> None:
    try:
        labels = data['label']                                      # (N_label,)
    except KeyError:
        raise KeyError(f"The data does not contain '{label}' key.")
    try:
        data_sensor = data['signal'][location][sensor]                  # (N_sensor,)
    except KeyError:
        raise KeyError(f"The data does not contain sensor '{sensor}'")

    if location == "wrist":
        fs_sensor = FREQUENCIES_WRIST[sensor]
    elif location == "chest":
        fs_sensor = FREQUENCIES_CHEST
    else:
        raise ValueError("Invalid sensor location")
        
    t_sensor  = np.arange(len(data_sensor)) / fs_sensor  # seconds between samples = 1/fs_temp

    # # DEBUG
    # print("Label length:", len(labels))
    # print("Sensor data length:", len(data_sensor))

    # Align 700 Hz labels with this sensor's sampling grid.
    # We map each sensor sample to the closest label index instead of forcing
    # an integer block size (needed for sensors whose frequency doesn't divide 700).
    label_positions = np.linspace(0, len(labels) - 1, len(data_sensor))
    label_indices = np.clip(np.round(label_positions).astype(int), 0, len(labels) - 1)
    labels_aligned = labels[label_indices]

    i_label = ValueToKey(VALID_LABELS, label)
    sensor_mask = labels_aligned == i_label

    data_sensor_labeled = data_sensor[sensor_mask]
    t_sensor_labeled = t_sensor[sensor_mask]
    min_time = t_sensor_labeled[0]
    t_sensor_labeled = t_sensor_labeled - min_time  # start from 0

    # ---- Plot ----
    plt.figure(figsize=(12,4))
    plt.plot(t_sensor_labeled, data_sensor_labeled, label=f"{location.capitalize()} {sensor} signal during {label}")
    plt.xlabel("Time (seconds)")
    plt.ylabel("Signal value")
    plt.title(f"{location.capitalize()} {sensor} during {label} (label = {i_label})")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()
    plt.show()
    
print(data.keys())                          # dict_keys(['signal', 'label', 'subject'])
print("")
print(data['signal'].keys())                # dict_keys(['chest', 'wrist'])
print(data['label']     )                   # Unique labels: 0-7
print(data['subject'])                      # SX
print("")
print(data['signal']['chest'].keys())       # dict_keys(['ACC', 'ECG', 'EMG', 'EDA', 'Temp', 'Resp'])
print(data['signal']['wrist'].keys())       # dict_keys(['ACC', 'BVP', 'EDA', 'TEMP'])

print(f"Duration of experiment (based on label): {data['label'].shape[0] / LABELING_FREQ / 60:.2f} min")                                 # 700 Hz
print(f"Duration of experiment (based on TEMP): {data['signal']['wrist']['TEMP'].shape[0] / FREQUENCIES_WRIST['TEMP'] / 60:.2f} min")    # 4 Hz
print("")

labels = data['label']
mask_valid = np.isin(labels, list(VALID_LABELS.keys()))
labels_filtered = labels[mask_valid]
print(f"Duration of VALID data for {data['subject']}: {labels_filtered.shape[0] / LABELING_FREQ / 60:.2f} min")      # 700 Hz
for vl in VALID_LABELS.keys():
    mask = np.isin(labels_filtered, vl)
    filtered = labels_filtered[mask]
    print(f"Duration of {VALID_LABELS[vl]} data: {filtered.shape[0] / LABELING_FREQ / 60:.2f} min")                  # 700 Hz
    
plotSignalWithSelectedLabel("chest", "Resp", "amusement")
