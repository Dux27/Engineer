#pragma once

#include "I2CDevice.hpp"
#include <cstdint>


class MAX30105 : I2CDevice
{
public:
    struct RawData
    {
        uint32_t green;
        uint32_t red;
        uint32_t infrared;
    };

    struct Data
    {
        // Beat/PPI events encountered while draining this batch.
        bool beat_detected = false, ppi_valid = false;
        uint32_t ppi_ms = 0;
        float hr = 0; // Beats/min; valid when ppi_valid is true.
    };

    using I2CDevice::I2CDevice;

    esp_err_t init();
    esp_err_t read(Data& data);

private:
    esp_err_t readRawData(RawData& raw_data);
    esp_err_t translateRawData(const RawData* data, Data& out);

    esp_err_t resetFIFO();
    void resetProcessing();

    uint64_t sample_index_ = 0;
    uint64_t last_beat_index_ = 0;
    bool have_previous_beat_ = false;
};
