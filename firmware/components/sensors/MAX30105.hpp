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
        uint32_t green;
        uint32_t red;
        uint32_t infrared;
    };

    struct Calibration
    {
        float todo;
    };

    using I2CDevice::I2CDevice;

    esp_err_t init();
    esp_err_t read(Data& data);

private:
    esp_err_t readRawData(RawData& raw_data);
    esp_err_t translateRawData(const RawData* data, Data& out);

    esp_err_t resetFIFO();
    // TODO: add calibrateDevice() function

    Calibration cal_;
};

