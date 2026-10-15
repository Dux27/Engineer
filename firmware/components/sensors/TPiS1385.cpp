#include "TPiS1385.hpp"

#include "GlobalConfig.hpp"
#include "esp_log.h"


static const char* TAG = "TPiS1385";

TPiS1385::~TPiS1385()
{
    deinit();
}

esp_err_t TPiS1385::init()
{
    if (bus_handle_ == nullptr)
    {
        ESP_LOGE(TAG, "I2C bus handle is null");
        return ESP_ERR_INVALID_ARG;
    }

    if (device_handle_ != nullptr)
    {
        return ESP_OK;
    }

    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = TPIS1385_I2C_ADDRESS;
    config.scl_speed_hz = 100000;

    esp_err_t err = i2c_master_bus_add_device(
        bus_handle_, &config, &device_handle_);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add I2C device: %s",
                 esp_err_to_name(err));
        deinit();
        return err;
    }

    uint8_t slave_address = 0;

    err = readRegister(63, slave_address);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "TPiS not responding at address 0x%02X: %s",
                 TPIS1385_I2C_ADDRESS, esp_err_to_name(err));
        deinit();
        return err;
    }

    ESP_LOGI(TAG,
             "TPiS detected at 0x%02X; register 63 = 0x%02X",
             TPIS1385_I2C_ADDRESS, slave_address);

    return ESP_OK;
}

esp_err_t TPiS1385::readRegister(uint8_t reg, uint8_t& value) 
{
    if (device_handle_ == nullptr)
    {
        return ESP_ERR_INVALID_STATE;
    }

    return i2c_master_transmit_receive(
        device_handle_, &reg, sizeof(reg),
        &value, sizeof(value), 1000);
}

esp_err_t TPiS1385::writeRegister(
    uint8_t reg, uint8_t value)
{
    if (device_handle_ == nullptr)
    {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t data[] = {reg, value};

    return i2c_master_transmit(
        device_handle_,
        data,
        sizeof(data),
        1000);
}

esp_err_t TPiS1385::readRawData(RawData& raw_data)
{
    if (device_handle_ == nullptr)
    {
        return ESP_ERR_INVALID_STATE;
    }

    // Registers 1 through 19 are read sequentially.
    uint8_t reg = 1;
    uint8_t data[19] = {};

    esp_err_t err = i2c_master_transmit_receive(
        device_handle_, &reg, sizeof(reg),
        data, sizeof(data), 1000);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Measurement read failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    // Register 1 = bits 16:9
    // Register 2 = bits 8:1
    // Register 3 bit 7 = bit 0
    raw_data.object_raw =
        (static_cast<uint32_t>(data[0]) << 9) |
        (static_cast<uint32_t>(data[1]) << 1) |
        ((data[2] >> 7) & 0x01);

    // Register 3 bits 6:0 and register 4.
    raw_data.ambient_raw =
        (static_cast<uint16_t>(data[2] & 0x7F) << 8) |
        data[3];

    raw_data.presence = data[14];
    raw_data.motion = data[15];
    raw_data.ambient_shock = data[16];
    raw_data.interrupt_status = data[17];
    raw_data.chip_status = data[18];

    return ESP_OK;
}

void TPiS1385::deinit()
{
    if (device_handle_ != nullptr)
    {
        i2c_master_bus_rm_device(device_handle_);
        device_handle_ = nullptr;
    }

    if (bus_handle_ != nullptr)
    {
        i2c_del_master_bus(bus_handle_);
        bus_handle_ = nullptr;
    }
}
