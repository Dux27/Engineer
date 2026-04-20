import os
import random
import warnings
import data_split
import numpy as np
import pandas as pd
import neurokit2 as nk

CAL = True
OUT_DIR = f"WESAD/model/{data_split.PKL_DIR.split(os.sep)[-1].split('_')[-1]}_{int(data_split.TIME_WINDOW)}s{'_cal' if CAL else ''}"

def basicStats(x: np.ndarray, prefix: str) -> dict:
    '''Calculate basic statistical features of the signal x'''
    x = np.asarray(x).squeeze()   # Ensure x is a numpy array and flatten to 1D
    feats = {}

    feats[f"{prefix}_mean"] = float(np.mean(x))
    feats[f"{prefix}_median"] = float(np.median(x))
    feats[f"{prefix}_std"] = float(np.std(x))
    feats[f"{prefix}_min"] = float(np.min(x))
    feats[f"{prefix}_max"] = float(np.max(x))
    feats[f"{prefix}_range"] = float(np.max(x) - np.min(x))
    feats[f"{prefix}_delta"] = float(x[-1] - x[0])
    return feats


def slopeFeature(x: np.ndarray, prefix: str, location: str, sensor: str) -> dict:
   '''Calculate the slope (a - rate of change) of the signal x over time based on sensor frequency'''
   x = np.asarray(x).squeeze()   
   feats = {}

   freq = data_split.getSensorFrequency(location, sensor)
   time = np.arange(len(x)) / freq

   slope, _ = np.polyfit(time, x, 1)    # Linear regression. Slope = a
   feats[f"{prefix}_slope"] = float(slope)
   return feats


def edaScrFeatures(eda: np.ndarray, prefix: str, location: str) -> dict:
    """Extract SCR features for wrist (E4) or chest (RespiBAN) EDA."""
    eda = np.asarray(eda, dtype=float).squeeze()
    feats = {}

    feats[f"{prefix}_scr_count_nc"] = 0
    feats[f"{prefix}_scr_mean_amp"] = 0.0
    feats[f"{prefix}_scr_max_amp"] = 0.0
    feats[f"{prefix}_scr_sum_amp"] = 0.0

    freq = data_split.getSensorFrequency(location, "EDA")

    if eda.size < 10 or np.std(eda) < 1e-8:
        return feats

    try:
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            signals, info = nk.eda_process(eda, sampling_rate=freq)

        scr_amplitudes = np.asarray(signals["SCR_Amplitude"].values, dtype=float)
        scr_amplitudes = scr_amplitudes[~np.isnan(scr_amplitudes)]
        scr_amplitudes = scr_amplitudes[scr_amplitudes > 0]

        if scr_amplitudes.size == 0:
            return feats

        feats[f"{prefix}_scr_count_nc"] = int(scr_amplitudes.size)
        feats[f"{prefix}_scr_mean_amp"] = float(np.mean(scr_amplitudes))
        feats[f"{prefix}_scr_max_amp"] = float(np.max(scr_amplitudes))
        feats[f"{prefix}_scr_sum_amp"] = float(np.sum(scr_amplitudes))

    except (ValueError, IndexError, KeyError) as e:
        pass

    return feats


def accFeatures(acc: np.ndarray, prefix: str, location: str) -> dict:
    """Extract the most informative features from 3-axis accelerometer data (ACC)"""
    acc = np.asarray(acc, dtype=float)
    feats = {}

    ZERO_CROSSING_THRESHOLD = 0.005  # g

    if acc.ndim != 2 or acc.shape[1] != 3:
        raise ValueError("ACC must be a array of [ax, ay, az].")

    mag = np.sqrt(acc[:,0]**2 + acc[:,1]**2 + acc[:,2]**2)  # Magnitude

    if location == "wrist":
        mag = mag / 64.0  

    feats[f"{prefix}_mag_mean_nc"] = float(np.mean(mag))
    feats[f"{prefix}_mag_std_nc"] = float(np.std(mag))
    feats[f"{prefix}_mag_energy_nc"] = float(np.sum(mag**2))
    feats[f"{prefix}_mag_mad_nc"] = float(np.mean(np.abs(mag - np.mean(mag))))  # Mean Absolute Deviation
    feats[f"{prefix}_mag_range_nc"] = float(np.max(mag) - np.min(mag))
    
    diff = np.diff(mag)
    diff[np.abs(diff) < ZERO_CROSSING_THRESHOLD] = 0
    zero_crossings = np.sum(diff[:-1] * diff[1:] < 0)
    feats[f"{prefix}_mag_zero_crossings_nc"] = int(zero_crossings)

    return feats


def ecgFeatures(ecg: np.ndarray) -> dict:
    '''Extract ECG-based features for stress detection'''
    ecg = np.asarray(ecg, dtype=float).squeeze()
    feats = {}

    freq = data_split.getSensorFrequency("chest", "ECG")
    signals, info = nk.ecg_process(ecg, sampling_rate=freq)

    heart_rate = np.asarray(signals["ECG_Rate"].values, dtype=float)
    heart_rate = heart_rate[~np.isnan(heart_rate)]
    heart_rate = heart_rate[heart_rate > 0]

    if heart_rate.size == 0:
        feats["chest_ECG_hr_mean"] = 0.0
        feats["chest_ECG_hr_std"] = 0.0
    else:
        feats["chest_ECG_hr_mean"] = float(np.mean(heart_rate))
        feats["chest_ECG_hr_std"] = float(np.std(heart_rate))
    r_peaks = np.asarray(info["ECG_R_Peaks"], dtype=int)  
    if r_peaks.size >= 2:
        rr = np.diff(r_peaks) / float(freq) * 1000.0  # ms
        feats["chest_ECG_rr_mean"] = float(np.mean(rr))
        feats["chest_ECG_rr_std"] = float(np.std(rr))
    else:
        feats["chest_ECG_rr_mean"] = 0.0
        feats["chest_ECG_rr_std"] = 0.0

    if r_peaks.size >= 3:
        try:
            peaks_dict = {"ECG_R_Peaks": r_peaks}
            hrv_df = nk.hrv_time(peaks_dict, sampling_rate=freq, show=False)    
            hrv_row = hrv_df.iloc[0]

            feats["chest_ECG_hrv_rmssd"] = float(hrv_row.get("HRV_RMSSD", 0.0))   # Root Mean Square of Successive Differences
            feats["chest_ECG_hrv_sdnn"] = float(hrv_row.get("HRV_SDNN", 0.0))     # Standard Deviation of NN intervals (RR is NN, after filtering)
            feats["chest_ECG_hrv_pnn50"] = float(hrv_row.get("HRV_pNN50", 0.0))   # Proportion of NN50 divided by total number of NN intervals
        except Exception:
            feats["chest_ECG_hrv_rmssd"] = 0.0
            feats["chest_ECG_hrv_sdnn"] = 0.0
            feats["chest_ECG_hrv_pnn50"] = 0.0
    else:
        feats["chest_ECG_hrv_rmssd"] = 0.0
        feats["chest_ECG_hrv_sdnn"] = 0.0
        feats["chest_ECG_hrv_pnn50"] = 0.0

    return feats


def bvpFeatures(bvp: np.ndarray) -> dict:
    '''Extract BVP/PPG-based features for stress detection'''
    bvp = np.asarray(bvp, dtype=float).squeeze()
    feats = {}

    fs = data_split.getSensorFrequency("wrist", "BVP")

    feats["wrist_BVP_hr_mean"] = 0.0
    feats["wrist_BVP_hr_std"] = 0.0
    feats["wrist_BVP_ppg_amp_mean"] = 0.0
    feats["wrist_BVP_ppg_amp_std"] = 0.0

    # Reject flat signals
    if bvp.size < 10 or np.std(bvp) < 1e-8:
        return feats

    try:
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            ppg_clean = nk.ppg_clean(bvp, sampling_rate=fs)

            _, info = nk.ppg_peaks(ppg_clean, sampling_rate=fs)
            peaks = np.asarray(info.get("PPG_Peaks", []), dtype=int)

        if peaks.size >= 2:
            ibi = np.diff(peaks) / float(fs)  
            hr = 60.0 / ibi
            feats["wrist_BVP_hr_mean"] = float(np.mean(hr))
            feats["wrist_BVP_hr_std"] = float(np.std(hr))

        if peaks.size > 0:
            peak_vals = np.asarray(ppg_clean)[peaks]
            feats["wrist_BVP_ppg_amp_mean"] = float(np.mean(peak_vals))
            feats["wrist_BVP_ppg_amp_std"] = float(np.std(peak_vals))

        return feats

    except Exception:
        return feats


def pttFeatures(ecg, ppg) -> dict:
    ecg = np.asarray(ecg, dtype=float).squeeze()
    ppg = np.asarray(ppg, dtype=float).squeeze()
    feats = {}

    if ecg.ndim != 1 or ppg.ndim != 1 or ecg.size < 10 or ppg.size < 10:
        return feats

    fs_ecg = data_split.getSensorFrequency("chest", "ECG")
    fs_ppg = data_split.getSensorFrequency("wrist", "BVP")

    feats["PTT_mean"] = 0.0
    feats["PTT_std"] = 0.0
    feats["PTT_median"] = 0.0

    # Reject flat signals
    if np.std(ecg) < 1e-8 or np.std(ppg) < 1e-8:
        return feats

    try:
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")

            ecg_clean = nk.ecg_clean(ecg, sampling_rate=fs_ecg)
            _, info_ecg = nk.ecg_peaks(ecg_clean, sampling_rate=fs_ecg)
            rpeaks = np.asarray(info_ecg.get("ECG_R_Peaks", []), dtype=int)

            ppg_clean = nk.ppg_clean(ppg, sampling_rate=fs_ppg)
            _, info_ppg = nk.ppg_peaks(ppg_clean, sampling_rate=fs_ppg)
            ppg_peaks = np.asarray(info_ppg.get("PPG_Peaks", []), dtype=int)

        if rpeaks.size < 2 or ppg_peaks.size < 2:
            return feats

        t_r = rpeaks / float(fs_ecg)
        t_p = ppg_peaks / float(fs_ppg)

        ptt = []
        j = 0
        for r in t_r:
            while j < len(t_p) and t_p[j] <= r:
                j += 1
            if j >= len(t_p):
                break
            ptt.append(t_p[j] - r)

        ptt = np.asarray(ptt, dtype=float)

        ptt = ptt[(ptt > 0.05) & (ptt < 0.6)] 

        if ptt.size == 0:
            return feats

        feats["PTT_mean"] = float(np.mean(ptt))
        feats["PTT_std"] = float(np.std(ptt))
        feats["PTT_median"] = float(np.median(ptt))

        return feats

    except Exception:
        return feats


def emgFeatures(emg: np.ndarray) -> dict:
    '''Extract EMG features related to muscle activation / tension'''
    emg = np.asarray(emg, dtype=float).squeeze()
    feats = {}
    
    fs = data_split.getSensorFrequency("chest", "EMG")
    emg_clean = nk.signal_filter(emg, 
                           sampling_rate=fs,
                           lowcut=20, highcut=250,
                           method="butterworth", order=4)
    
    rms = np.sqrt(np.mean(emg_clean ** 2))
    feats["chest_EMG_rms"] = float(rms)

    mean_abs = np.mean(np.abs(emg_clean))
    feats["chest_EMG_mean_abs"] = float(mean_abs)

    energy = np.sum(emg_clean ** 2)
    feats["chest_EMG_energy_nc"] = float(energy)

    return feats


def respFeatures(resp: np.ndarray) -> dict:
    """
    Minimal RESP features for 10-second windows.
    Keeps only stable signal-based features + peaks_count (quality).
    No rate / periods / regularity / peak amplitudes (they are often 0 in 10s windows).
    """

    resp = np.asarray(resp, dtype=float).squeeze()
    feats = {}

    fs = data_split.getSensorFrequency("chest", "Resp")

    def slope_feature(x: np.ndarray) -> float:
        if x.size < 2:
            return 0.0
        t = np.arange(x.size) / float(fs)
        a, _ = np.polyfit(t, x, 1)
        return float(a)

    # Defaults (always present, never crash)
    feats["chest_Resp_peaks_count_nc"] = 0
    feats["chest_Resp_std"] = 0.0
    feats["chest_Resp_range"] = 0.0
    feats["chest_Resp_energy"] = 0.0
    feats["chest_Resp_slope"] = 0.0

    try:
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            signals, info = nk.rsp_process(resp, sampling_rate=fs)

        rsp_clean = np.asarray(signals["RSP_Clean"].values, dtype=float)

        peaks = np.asarray(info.get("RSP_Peaks", []), dtype=int)
        peaks = peaks[peaks >= 0]
        feats["chest_Resp_peaks_count_nc"] = int(peaks.size)

    except Exception:
        # Fallback: still get a cleaned signal
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            rsp_clean = np.asarray(nk.rsp_clean(resp, sampling_rate=fs), dtype=float)

        # peaks_count unknown -> keep 0

    # Signal-only robust features (always computed)
    feats["chest_Resp_std"] = float(np.std(rsp_clean))
    feats["chest_Resp_range"] = float(np.max(rsp_clean) - np.min(rsp_clean))
    feats["chest_Resp_energy"] = float(np.sum(rsp_clean ** 2))
    feats["chest_Resp_slope"] = slope_feature(rsp_clean)

    return feats


def extractFeatures(subject: str, label: str, wrist_data: dict, chest_data: dict, index: int) -> dict:
    '''Extract features from wrist and chest data for a given subject, label, and window index.'''
    features: dict = {}
    features["subject"] = subject
    features["label"] = label

    # Store signals for PTT calculation
    ecg_signal = None
    bvp_signal = None

    for sensor in wrist_data.keys():
        window = wrist_data[sensor][label][index]

        if sensor == "ACC":
            features.update(accFeatures(window, "wrist_ACC", "wrist"))
        elif sensor == "BVP":
            bvp_signal = window 
            features.update(bvpFeatures(window))
        elif sensor == "EDA":
            features.update(basicStats(window, "wrist_EDA"))
            features.update(slopeFeature(window, "wrist_EDA", "wrist", "EDA"))
            features.update(edaScrFeatures(window, "wrist_EDA", "wrist"))
        elif sensor == "TEMP":
            features.update(basicStats(window, "wrist_TEMP"))
            features.update(slopeFeature(window, "wrist_TEMP", "wrist", "TEMP"))

    for sensor in chest_data.keys():
        window = chest_data[sensor][label][index]

        if sensor == "ACC":
            features.update(accFeatures(window, "chest_ACC", "chest"))
        elif sensor == "ECG":
            ecg_signal = window  
            features.update(ecgFeatures(window))
        elif sensor == "EMG":
            features.update(emgFeatures(window))
        elif sensor == "EDA":
            features.update(basicStats(window, "chest_EDA"))
            features.update(slopeFeature(window, "chest_EDA", "chest", "EDA"))
            features.update(edaScrFeatures(window, "chest_EDA", "chest"))
        elif sensor == "Temp":
            features.update(basicStats(window, "chest_Temp"))
            features.update(slopeFeature(window, "chest_Temp", "chest", "Temp"))
        elif sensor == "Resp":
            features.update(respFeatures(window))
    
    if ecg_signal is not None and bvp_signal is not None:
        features.update(pttFeatures(ecg_signal, bvp_signal))
    
    return features


def calibrateBySubjectBaseline(dataset: list[dict], eps=1e-6) -> list[dict]:
    """
    Calibrate features using z-score normalization per subject based on baseline.
    Returns: calibrated dataset with both original and calibrated features.
    """
    df = pd.DataFrame(dataset)
    
    # Split features: calibrate-able vs non-calibrate-able
    all_feat_cols = [c for c in df.columns if c not in ["subject", "label"]]
    feat_cols = [c for c in all_feat_cols if not c.endswith("_nc")]  # TO BE CALIBRATED
    nc_cols = [c for c in all_feat_cols if c.endswith("_nc")]        # NO CALIBRATION
    
    baseline_df = df[df["label"] == "baseline"].copy()
    
    # Calculate per-subject baseline statistics (only for calibrate-able features)
    baseline_mean = baseline_df.groupby("subject")[feat_cols].mean()
    baseline_std = baseline_df.groupby("subject")[feat_cols].std()
    
    # Global fallback for subjects with insufficient baseline data
    global_mean = baseline_df[feat_cols].mean()
    global_std = baseline_df[feat_cols].std()

    calibrated_features = []
    
    for subject in df["subject"].unique():
        subject_mask = df["subject"] == subject
        subject_data = df[subject_mask].copy()

        if subject in baseline_mean.index:
            mu = baseline_mean.loc[subject]
            sigma = baseline_std.loc[subject]
        else:
            mu = global_mean
            sigma = global_std
        
        # Replace zero std with global std to avoid division by zero
        sigma = sigma.replace(0, np.nan).fillna(global_std)
        
        # Z-score normalization (only for non-_nc features)
        calibrated = (subject_data[feat_cols] - mu) / (sigma + eps)
        calibrated = calibrated.replace([np.inf, -np.inf], np.nan).fillna(0)
        
        # Rename calibrated columns
        calibrated.columns = [f"{c}_cal" for c in feat_cols]
        
        # Combine: subject + label + _nc features + original features + calibrated features
        subject_calibrated = pd.concat([
            subject_data[["subject", "label"]],
            subject_data[nc_cols],      # _nc features (original, no calibration)
            subject_data[feat_cols],    # Original calibrate-able features
            calibrated                  # Calibrated features
        ], axis=1)
        
        calibrated_features.append(subject_calibrated)
    
    # Combine all subjects
    df_calibrated = pd.concat(calibrated_features, ignore_index=True)
    
    print(f"\n Calibrated {len(feat_cols)} features per subject using baseline")
    print(f" - Non-calibrated (_nc): {len(nc_cols)}")
    print(f" - Original: {len(feat_cols)}")
    print(f" - Calibrated: {len(feat_cols)}")
    print(f"  Total features: {len(df_calibrated.columns) - 2}")
    
    return df_calibrated.to_dict('records')


def saveToParquet(data, output_dir):
    '''Save extracted features and labels to Parquet files.'''
    os.makedirs(output_dir, exist_ok=True)

    df = pd.DataFrame(data)

    X = df.drop(columns=["label"])
    y = df[["label"]]

    X.to_parquet(os.path.join(output_dir, "X.parquet"), index=False)
    y.to_parquet(os.path.join(output_dir, "y.parquet"), index=False)


def getNumWindows(data_dict: dict, label: str) -> int:
    """Get minimum number of windows available across all sensors for a given label."""
    lengths = []
    for sensor in data_dict.keys():
        if label in data_dict[sensor]:
            lengths.append(len(data_dict[sensor][label]))
    return min(lengths) if lengths else 0


def main():
    dataset: list[dict] = []
    wrist_data, chest_data = data_split.main()

    # Calculate total number of samples based on minimum windows across all sensors
    total_samples = 0
    for subject in wrist_data.keys():  
        for label in data_split.VALID_LABELS.values():
            n_wrist = getNumWindows(wrist_data[subject], label)  
            n_chest = getNumWindows(chest_data[subject], label) 
            n = min(n_wrist, n_chest)
            total_samples += n

            if n_wrist != n_chest:
                print(f"Wrong number of windows for {subject}/{label}: wrist={n_wrist}, chest={n_chest}")
                print()
    
    current_sample = 0
    print(f"Output directory: {OUT_DIR}")
    print(f"Extracting features from {total_samples} samples...\n")

    for subject in wrist_data.keys():
        for label_category in data_split.VALID_LABELS.values():
            n_wrist = getNumWindows(wrist_data[subject], label_category)  
            n_chest = getNumWindows(chest_data[subject], label_category)  
            n_windows = min(n_wrist, n_chest)
            
            for i in range(n_windows):
                current_sample += 1
                progress = (current_sample / total_samples) * 100
                print(f"\rFeature extraction: {progress:.1f}% ({current_sample}/{total_samples}) - {subject}/{label_category}", end="", flush=True)
                
                extracted_features = extractFeatures(subject, label_category, wrist_data[subject], chest_data[subject], i)
                dataset.append(extracted_features)

    if CAL:
        dataset = calibrateBySubjectBaseline(dataset)

    print(f"Total samples extracted: {len(dataset)}")
    
    random.shuffle(dataset)

    print(f"First: {dataset[0]}")
    saveToParquet(dataset, output_dir=OUT_DIR)


if __name__ == "__main__":
    main()
