#pragma once

#include "GlobalConfig.hpp"

#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>
#include <cstddef>
#include <stddef.h>

class MPU6050
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

    MPU6050() = default;
    ~MPU6050();

    MPU6050(const MPU6050&) = delete;
    MPU6050& operator=(const MPU6050&) = delete;

    esp_err_t init();
    esp_err_t read(Data& data);

private:
    esp_err_t writeRegister(uint8_t reg, uint8_t value);
    esp_err_t readRegisters(uint8_t reg, uint8_t* data, size_t length);
    esp_err_t readRawData(RawData& raw_data);
    esp_err_t translateRawData(const RawData* data, Data& out);
    // TODO: Add calibration

    void deinit();

    i2c_master_bus_handle_t bus_handle_ = nullptr;
    i2c_master_dev_handle_t device_handle_ = nullptr;
};
