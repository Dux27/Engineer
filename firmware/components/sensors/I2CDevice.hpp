#pragma once

#include <GlobalConfig.hpp>

#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2c_master.h"

#include <cstddef>
#include <cstdint>


class I2CDevice
{
public:
    I2CDevice(i2c_master_bus_handle_t bus, uint8_t address);
    virtual ~I2CDevice();

    I2CDevice(const I2CDevice&) = delete;
    I2CDevice& operator=(const I2CDevice&) = delete;

protected:
    esp_err_t addDevice();
    void deinit();

    esp_err_t writeRegister(uint8_t reg, uint8_t value);
    esp_err_t readRegisters( uint8_t reg, uint8_t* data, size_t length);

    i2c_master_bus_handle_t bus_handle_ = nullptr;
    i2c_master_dev_handle_t device_handle_ = nullptr;
    uint8_t address_;
};
