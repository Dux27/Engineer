#pragma once

#include "GlobalConfig.hpp"

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/i2c_types.h"
#include "driver/i2c_master.h"

#include <array>
#include <math.h>
#include <stdint.h>


class TPiS1385
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

    TPiS1385(i2c_master_bus_handle_t bus, uint8_t address);
    ~TPiS1385();

    TPiS1385(const TPiS1385&) = delete;
    TPiS1385& operator=(const TPiS1385&) = delete;

    esp_err_t init();
    esp_err_t read(Data& data);

private:
    esp_err_t writeRegister(uint8_t reg, uint8_t value);
    esp_err_t readRegister(uint8_t reg, uint8_t& value);
    esp_err_t readRawData(RawData& raw_data);
    esp_err_t translateRawData(const RawData* data, Data& out);
    esp_err_t sendGeneralCall(i2c_master_bus_handle_t bus_handle);

    void deinit();

    i2c_master_bus_handle_t bus_handle_;
    i2c_master_dev_handle_t device_handle_ = nullptr;
    uint8_t address_;
};
