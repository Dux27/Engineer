#pragma once

#include "I2CDevice.hpp"
#include <cstdint>

// HR-only acquisition: one red optical sample per FIFO record.
// The upstream beat detector has shared state; use one instance from one task.
class MAX30102 : I2CDevice
{
public:
    struct RawData
    {
        uint32_t red = 0;
    };

    struct Data
    {
        bool beat_detected = false;
        bool ppi_valid = false;
        uint32_t ppi_ms = 0;
        float hr = 0; // Beats/min; valid only when ppi_valid is true.
    };

    using I2CDevice::I2CDevice;
    esp_err_t init();
    esp_err_t read(Data& data);

private:
    esp_err_t translateRawData(const RawData* raw, Data& out);
    esp_err_t resetFIFO();
    esp_err_t recoverFIFO();
    void resetProcessing();

    uint64_t sample_index_ = 0;
    uint64_t last_beat_index_ = 0;
    uint32_t settling_samples_ = 0;
    bool have_previous_beat_ = false;
};
