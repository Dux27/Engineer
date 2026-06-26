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
| **PPG** | MAX30105 | I2C | Heart Rate & HRV analysis |
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
