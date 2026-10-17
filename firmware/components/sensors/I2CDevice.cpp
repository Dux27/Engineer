#include "I2CDevice.hpp"

#include "GlobalConfig.hpp"
#include "esp_log.h"


static const char TAG[] = "I2CDevice";

I2CDevice::I2CDevice(i2c_master_bus_handle_t bus, uint8_t address)
    : bus_handle_(bus), address_(address) {}

I2CDevice::~I2CDevice()
{
    deinit();
}

esp_err_t I2CDevice::addDevice()
{
    if (device_handle_ != nullptr)
    {
        return ESP_OK;
    }

    if (bus_handle_ == nullptr)
    {
        ESP_LOGE(TAG, "I2C bus handle is null");
        return ESP_ERR_INVALID_ARG;
    }

    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address_;
    config.scl_speed_hz = I2C_FREQUENCY_HZ;

    const esp_err_t err = i2c_master_bus_add_device(
        bus_handle_,
        &config,
        &device_handle_);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to add device at 0x%02X: %s",
            address_,
            esp_err_to_name(err));
    }

    return err;
}

esp_err_t I2CDevice::writeRegister(uint8_t reg, uint8_t value)
{
    if (device_handle_ == nullptr)
    {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t buffer[] = {reg, value};

    const esp_err_t err = i2c_master_transmit(
        device_handle_,
        buffer,
        sizeof(buffer),
        I2C_TIMEOUT_MS);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Register write 0x%02X to device 0x%02X failed: %s",
            reg,
            address_,
            esp_err_to_name(err));
    }

    return err;
}

esp_err_t I2CDevice::readRegisters(
    uint8_t reg,
    uint8_t* data,
    size_t length)
{
    if (device_handle_ == nullptr)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (data == nullptr || length == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t err = i2c_master_transmit_receive(
        device_handle_,
        &reg,
        sizeof(reg),
        data,
        length,
        I2C_TIMEOUT_MS);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Register read 0x%02X from device 0x%02X failed: %s",
            reg,
            address_,
            esp_err_to_name(err));
    }

    return err;
}

void I2CDevice::deinit()
{
    if (device_handle_ != nullptr)
    {
        const esp_err_t err = i2c_master_bus_rm_device(device_handle_);

        if (err != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "Failed to remove device at 0x%02X: %s",
                address_,
                esp_err_to_name(err));
        }
        else
        {
            device_handle_ = nullptr;
        }
    }
}
