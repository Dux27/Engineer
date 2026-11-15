#pragma once

#include "I2CDevice.hpp"

#include <cstdint>

class TPiS1385 : I2CDevice
{
public:
    struct RawData
    {
        uint32_t object_raw = 0;  // Registers 1, 2, 3[7]
        uint16_t ambient_raw = 0; // Registers 3[6:0], 4
    };

    struct Data
    {
        float skin_temperature = 0.0f;    // C
        float ambient_temperature = 0.0f; // C
    };

    struct Calibration
    {
        // Sensor conversion coefficients
        float ptat25 = 0.0f;
        float m = 0.0f;
        float u0 = 0.0f;
        float uout1 = 0.0f;
        float reference_object_temp_c = 0.0f;
        float reference_temp_k = 298.15f;

        // Measurement corrections
        float skin_emissivity = 0.98f;
        float fov_coverage = 1.0f;
    };

    using I2CDevice::I2CDevice;

    esp_err_t init();
    esp_err_t read(Data &data);

private:
    esp_err_t loadCalibration();
    esp_err_t readRawData(RawData &raw_data);
    esp_err_t translateRawData(const RawData *data, Data &out);
    esp_err_t sendGeneralCall(i2c_master_bus_handle_t bus_handle);

    Calibration cal_;
};
