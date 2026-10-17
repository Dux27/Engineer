#pragma once

#include "I2CDevice.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>
#include <cstddef>
#include <stddef.h>


class MPU6050 : I2CDevice
{
public:
    struct RawData
    {
        int16_t acc_x;
        int16_t acc_y;
        int16_t acc_z;
        int16_t temperature;
    };
    struct Data
    {
        float acc_x;        // m/s2
        float acc_y;        // m/s2
        float acc_z;        // m/s2
        float temperature;  // C
    };

    using I2CDevice::I2CDevice;

    esp_err_t init();
    esp_err_t read(Data& data);

private:
    esp_err_t readRawData(RawData& raw_data);
    esp_err_t translateRawData(const RawData* data, Data& out);
    // TODO: Add calibration

    static constexpr float ACCEL_SCALE = 16384.0f;
    static constexpr float GRAVITY = 9.80665f;
};
