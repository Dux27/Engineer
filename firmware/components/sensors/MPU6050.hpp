#pragma once

#include "esp_log.h"
#include "driver/i2c_master.h"

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
        float acc_x;      // m/s2
        float acc_y;      // m/s2
        float acc_z;      // m/s2
        float temperature;  // °C
    };

    MPU6050() = default;
    ~MPU6050();

    MPU6050(const MPU6050&) = delete;
    MPU6050& operator=(const MPU6050&) = delete;

    bool init();
    bool read(Data& data);

private:
    static constexpr float ACCEL_SCALE = 16384.0f; // LSB/g, +/- 2 g
    static constexpr float GRAVITY = 9.80665;

    bool writeRegister(uint8_t reg, uint8_t value);
    bool readRegisters(uint8_t reg, uint8_t* data, size_t length);
    bool translateRawData(const RawData* data, Data& out);
    bool readRawData(RawData& data);
    // TODO: Add calibration

    void deinit();

    i2c_master_bus_handle_t bus_handle_ = nullptr;
    i2c_master_dev_handle_t device_handle_ = nullptr;
};
