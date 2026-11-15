#pragma once

#include "I2CDevice.hpp"
#include "PPGPeriodEstimator.hpp"
#include "PPGDecimator.hpp"
#include "PPGRateHistory.hpp"
#include <cstdint>

// Dual optical acquisition: red then IR in each FIFO record. HR uses IR.
// The upstream beat detector has shared state; use one instance from one task.
class MAX30102 : I2CDevice
{
public:
    struct RawData
    {
        uint32_t ir = 0;
    };

    struct Data
    {
        bool ppi_valid = false;
        bool hr_valid = false;
        bool hr_updated = false;
        uint32_t ppi_ms = 0;
        float hr = 0; // Waveform-period HR; valid only when hr_valid is true.
        uint8_t raw_count = 0;
        uint32_t raw_red[32]{}; // Diagnostic FIFO snapshot, chronological.
        uint32_t raw_ir[32]{};
        bool quality_valid = false;
        float periodicity = 0, relative_ac = 0;
        uint32_t processing_us = 0;
        uint8_t hr_window_size = 0;
        float detector_rate_hz = MAX30102_SAMPLE_RATE_HZ;
    };

    using I2CDevice::I2CDevice;
    esp_err_t init();
    esp_err_t read(Data &data);

private:
    esp_err_t translateRawData(const RawData *raw, Data &out);
    esp_err_t resetFIFO();
    esp_err_t recoverFIFO();
    void resetProcessing();

    uint64_t sample_index_ = 0;
    uint64_t last_beat_index_ = 0;
    uint32_t settling_samples_ = 0;
    bool have_previous_beat_ = false;
    PPGPeriodEstimator period_estimator_;
    PPGDecimator decimator_;
    PPGRateHistory waveform_history_;
    unsigned last_estimate_id_ = 0;
    int64_t fifo_epoch_us_ = 0;
    uint64_t fifo_clock_samples_ = 0;
    float effective_rate_hz_ = MAX30102_SAMPLE_RATE_HZ;
};
