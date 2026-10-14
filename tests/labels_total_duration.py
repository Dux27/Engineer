'''Script to calculate total duration of different labels from pickle files.'''

import os
import math
import pickle
import numpy as np

LABELING_FREQ = 700     # Hz. Its the frequency of the highest frequency signal
PKL_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")        
TIME_WINDOW = 10.0      # seconds

VALID_LABELS = {        # Valid labels to consider according to the experiment protocol
    1: "baseline",
    2: "stress",
    3: "amusement",
    4: "meditation"
}

# time in minutes
durations = {
    "all": 0.0,
    "valid": 0.0,
    "baseline": 0.0,
    "stress": 0.0,
    "amusement": 0.0,
    "meditation": 0.0
}

pickle_files = [
    f for f in os.listdir(PKL_DIR) if f.endswith(".pkl")
]
print(f"Found {len(pickle_files)} pickle files in '{PKL_DIR}' directory.")
print()

for pkl in pickle_files:
    with open(os.path.join(PKL_DIR, pkl), "rb") as f:
        data = pickle.load(f, encoding='latin1')
        
    # get duration of all labels
    durations["all"] += data['label'].shape[0] / LABELING_FREQ / 60.0  
    
    # get duration of valid labels
    labels = data['label']
    mask_valid_labels = np.isin(labels, list(VALID_LABELS.keys()))
    valid_labels = labels[mask_valid_labels]
    durations['valid'] += valid_labels.shape[0] / LABELING_FREQ / 60.0
    
    # get duration per label
    for vl in VALID_LABELS.keys():
        mask_label = np.isin(valid_labels, vl)
        label = valid_labels[mask_label]
        durations[VALID_LABELS[vl]] += label.shape[0] / LABELING_FREQ / 60.0
    
print("Total durations:")
for key, value in durations.items():
    if key == "all" or key == "valid":
        print(f"  - {key}: {value:.2f} min")
        continue
    percentage = (value / durations["valid"]) * 100.0 if durations["valid"] > 0 else 0
    print(f"  - {key}: {value:.2f} min ({percentage:.2f}% of valid labels)")
print()

print(f"Number or labeled samples for {TIME_WINDOW} sec time window:")
for vl in VALID_LABELS.keys():
    print(f"  - {VALID_LABELS[vl]}: {math.floor(durations[VALID_LABELS[vl]] * 60.0 / TIME_WINDOW)}")
        