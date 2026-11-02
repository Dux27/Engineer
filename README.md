# Low-Cost Wearable Emotion Detection System

This repository contains the software and hardware documentation for my engineering thesis project. The goal is to develop a low-cost, open-source alternative to research-grade physiological monitoring devices (e.g., Empatica E4, RespiBAN) for the purpose of detecting stress and emotional states.

The project uses the **WESAD (Wearable Stress and Affect Detection)** dataset as a benchmark for training a machine learning model, which is then adapted to process real-time data from custom hardware.

## Project Overview
* **Objective:** Create an accessible, modular wearable device capable of gathering physiological data (ECG/PPG, EDA, Temp, Motion) to detect affective states.
* **Benchmark:** The model is trained using the **WESAD dataset** (using XGBoost).
* **Hardware:** Built around the **ESP32-S3** microcontroller.

## Hardware Architecture
The device collects multimodal data through the following sensors:

| Sensor | Component | Protocol | Function |
| :--- | :--- | :--- | :--- |
| **ACC** | MPU6050 | I2C | Motion & Artifact detection |
| **PPG** | MAX30102 | I2C | Heart Rate & HRV analysis |
| **TEMP** | Excelitas (TPIS 1S 1385) | I2C | Skin temperature monitoring |
| **GSR** | Grove GSR | Analog (ADC) | Electrodermal Activity (Stress level) |

## Software Pipeline
1. **Data Acquisition:** Real-time collection via ESP32-S3.
2. **Signal Preprocessing:** Filtering and feature extraction
3. **Machine Learning:** XGBoost classification model trained on the WESAD dataset, optimized for performance and generalization.

## 📝 Status
*   **Status:** Work in Progress (Engineering Thesis Phase).
*   **Current Milestone:** Hardware prototype assembly and signal quality validation against WESAD benchmarks.

---
*Developed as part of the Biomedical Engineering degree at Gdańsk University of Technology (Politechnika Gdańska).*

## MAX30102 heart-rate acquisition

The driver uses the MAX30102 dedicated **red-only HR mode**, not SpO2 or
multi-LED mode. The device has no green LED. Its HR mode provides optical
samples; firmware still has to detect beats and calculate PPI/HR.
See the [manufacturer datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX30102.pdf).

Current baseline: 400 ADC conversions/s, four-sample hardware averaging,
**100 FIFO records/s**, 411 us pulses (18-bit), 4096 nA ADC range and about
7.2 mA red LED current. IR is off. Three-byte records are drained in a burst;
the application checks PPG every 20 ms and reads/logs ACC/TEMP about every 100 ms.
Beat timing uses sample indices, not the time a batch is downloaded. No SpO2
calculation is included.

The detector has a two-second settling period and amplitude-relative limits.
Intervals under 300 ms are ignored without moving the previous-beat reference;
intervals over 2000 ms invalidate PPI and restart its reference. Weak samples
(below 5000 counts), samples near ADC clipping (98% full scale), excess ambient
light, FIFO gaps and detected power resets invalidate processing history.
These are heuristic guards, not calibrated contact/motion classifiers.

The LED current, ADC range and thresholds are starting values, not universal
optimal settings. Validate red PPG on the actual wrist mounting against a
reference before using beat intervals as model inputs. Increasing every setting
can increase saturation, power use and latency. The driver intentionally avoids
proximity mode, unused LEDs and temperature conversions, which do not improve
this HR-only acquisition. The part ID 0x15 is shared by multiple MAX3010x parts
and cannot independently confirm the exact physical model.

Build: `cd firmware && idf.py build`. The build does not flash the board.
The terminal dashboard now labels PPG as MAX30102:
`python3 tools/sensor_monitor.py --port /dev/ttyACM0`.

Host driver/algorithm regression tests (no board required):

```sh
g++ -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I tests/max30102/stubs -I firmware/components/sensors \
  -I firmware/components/ppg_processing tests/max30102/driver_test.cpp \
  firmware/components/sensors/MAX30102.cpp \
  firmware/components/ppg_processing/HeartRate.cpp -o /tmp/engineer-max30102-test
ASAN_OPTIONS=detect_leaks=0 /tmp/engineer-max30102-test
```

The historical MAX30105 PDF in `docs/Datasheets` describes the old device; it
has not been renamed or presented as a MAX30102 datasheet.
