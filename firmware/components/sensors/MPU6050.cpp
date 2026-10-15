#include "MPU6050.hpp"
#include "esp_err.h"

static const char* TAG = "MPU6050";

namespace
{
    constexpr uint8_t REG_SMPLRT_DIV   = 0x19;
    constexpr uint8_t REG_CONFIG       = 0x1A;
    constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;
    constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;
    constexpr uint8_t REG_PWR_MGMT_1   = 0x6B;
    constexpr uint8_t REG_WHO_AM_I     = 0x75;

    constexpr uint8_t WHO_AM_I_VALUE = 0x68;
    constexpr int I2C_TIMEOUT_MS = 100;
}

MPU6050::~MPU6050()
{
    deinit();
}

esp_err_t MPU6050::init()
{
    if (device_handle_ != nullptr)
    {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_PORT;
    bus_config.sda_io_num = I2C_SDA_GPIO;
    bus_config.scl_io_num = I2C_SCL_GPIO;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;

    esp_err_t err = i2c_new_master_bus(
        &bus_config,
        &bus_handle_);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s",
                 esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t device_config = {};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = MPU6050_I2C_ADDRESS;
    device_config.scl_speed_hz = I2C_FREQUENCY_HZ;

    err = i2c_master_bus_add_device(
        bus_handle_,
        &device_config,
        &device_handle_);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add MPU6050 to I2C bus: %s",
                 esp_err_to_name(err));
        deinit();
        return err;
    }

    uint8_t who_am_i = 0;

    if (!readRegisters(REG_WHO_AM_I, &who_am_i, 1))
    {
        ESP_LOGE(TAG, "Failed to read WHO_AM_I");
        deinit();
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (who_am_i != WHO_AM_I_VALUE)
    {
        ESP_LOGE(TAG, "Unexpected WHO_AM_I: 0x%02X", who_am_i);
        deinit();
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Wake the sensor from sleep mode.
    if (!writeRegister(REG_PWR_MGMT_1, 0x00))
    {
        ESP_LOGE(TAG, "Failed to wake MPU6050");
        deinit();
        return ESP_ERR_INVALID_RESPONSE;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    // Enable the digital low-pass filter.
    if (!writeRegister(REG_CONFIG, 0x03))
    {
        deinit();
        return ESP_ERR_INVALID_RESPONSE;
    }

    // 1 kHz internal rate / (1 + 9) = 100 Hz.
    if (!writeRegister(REG_SMPLRT_DIV, 9))
    {
        deinit();
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Accelerometer: +/- 2 g.
    if (!writeRegister(REG_ACCEL_CONFIG, 0x00))
    {
        deinit();
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG, "MPU6050 initialized successfully");

    return ESP_OK;
}

esp_err_t MPU6050::read(Data& data)
{
    RawData raw{};

    if (!readRawData(raw))
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    return translateRawData(&raw, data);
}

esp_err_t MPU6050::writeRegister(uint8_t reg, uint8_t value)
{
    const uint8_t buffer[] = {reg, value};

    const esp_err_t err = i2c_master_transmit(
        device_handle_,
        buffer,
        sizeof(buffer),
        I2C_TIMEOUT_MS);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Register write 0x%02X failed: %s",
                 reg, esp_err_to_name(err));
        return err;
    }

    return err;
}

esp_err_t MPU6050::readRegisters( uint8_t reg, uint8_t* data, size_t length)
{
    const esp_err_t err = i2c_master_transmit_receive(
        device_handle_,
        &reg,
        1,
        data,
        length,
        I2C_TIMEOUT_MS);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Register read 0x%02X failed: %s",
                 reg, esp_err_to_name(err));
        return err;
    }

    return err;
}

esp_err_t MPU6050::readRawData(RawData& data)
{
    std::array<uint8_t, 14> buffer{};

    if (!readRegisters(
            REG_ACCEL_XOUT_H,
            buffer.data(),
            buffer.size()))
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const auto readInt16 = [&buffer](size_t index) -> int16_t
    {
        const uint16_t value =
            (static_cast<uint16_t>(buffer[index]) << 8) |
             static_cast<uint16_t>(buffer[index + 1]);

        return static_cast<int16_t>(value);
    };

    data.acc_x = readInt16(0);
    data.acc_y = readInt16(2);
    data.acc_z = readInt16(4);

    data.temperature = readInt16(6);

    return ESP_OK;
}

esp_err_t MPU6050::translateRawData(const RawData* data, Data& out)
{
    if (data == nullptr)
    {
        return ESP_ERR_INVALID_ARG;
    }

    out.acc_x = static_cast<float>(data->acc_x) / ACCEL_SCALE * GRAVITY;
    out.acc_y = static_cast<float>(data->acc_y) / ACCEL_SCALE * GRAVITY;
    out.acc_z = static_cast<float>(data->acc_z) / ACCEL_SCALE * GRAVITY;

    out.temperature = static_cast<float>(data->temperature) / 340.0f + 36.53f;

    return ESP_OK;
}

void MPU6050::deinit()
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
