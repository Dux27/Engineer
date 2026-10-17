#pragma once

#include "I2CDevice.hpp"

#include <array>
#include <math.h>
#include <stdint.h>


class TPiS1385 : I2CDevice
{
public:
    struct RawData
    {
        uint32_t object_raw = 0;      // Registers 1, 2, 3[7]
        uint16_t ambient_raw = 0;     // Registers 3[6:0], 4

        uint8_t presence = 0;         // Register 15
        uint8_t motion = 0;           // Register 16
        uint8_t ambient_shock = 0;    // Register 17
        uint8_t interrupt_status = 0; // Register 18
        uint8_t chip_status = 0;      // Register 19
    };

    struct Data
    {
        float skin_temperature = 0.0f;  // C
        float ambient_temperature = 0.0f; // C
    };

    using I2CDevice::I2CDevice;

    esp_err_t init();
    esp_err_t read(Data& data);

private:
    esp_err_t readRawData(RawData& raw_data);
    esp_err_t translateRawData(const RawData* data, Data& out);
    esp_err_t sendGeneralCall(i2c_master_bus_handle_t bus_handle);
};
