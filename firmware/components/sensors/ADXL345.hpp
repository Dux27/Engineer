#pragma once

#include "I2CDevice.hpp"

class ADXL345 : public I2CDevice
{
public:
    struct Data
    {
        float acc_x = 0.0f; // m/s2
        float acc_y = 0.0f;
        float acc_z = 0.0f;
    };

    using I2CDevice::I2CDevice;
    esp_err_t init();
    esp_err_t read(Data &data);
};
