#include "MPU6050.hpp"


namespace
{
    constexpr char TAG[] = "MPU6050";

    constexpr uint8_t REG_SMPLRT_DIV   = 0x19;
    constexpr uint8_t REG_CONFIG       = 0x1A;
    constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;
    constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;
    constexpr uint8_t REG_PWR_MGMT_1   = 0x6B;

    constexpr float ACCEL_SCALE = 16384.0f;
    constexpr float GRAVITY = 9.80665f;
}

esp_err_t MPU6050::init()
{
    if (device_handle_ != nullptr)
    {
        return ESP_OK;
    }

    esp_err_t err = addDevice();
    if (err != ESP_OK)
    {
        return err;
    }

    // Wake the sensor from sleep mode.
    err = writeRegister(REG_PWR_MGMT_1, 0x00);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to wake up: %s", esp_err_to_name(err));
        deinit();
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    // Enable the digital low-pass filter.
    err = writeRegister(REG_CONFIG, 0x03);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure DLPF: %s", esp_err_to_name(err));
        deinit();
        return err;
    }

    // Sample rate: 1 kHz / (1 + 9) = 100 Hz.
    err = writeRegister(REG_SMPLRT_DIV, 9);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure sample rate: %s", esp_err_to_name(err));
        deinit();
        return err;
    }

    // Accelerometer range: +/- 2 g.
    err = writeRegister(REG_ACCEL_CONFIG, 0x00);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure accelerometer: %s", esp_err_to_name(err));
        deinit();
        return err;
    }

    ESP_LOGI(TAG, "Detected at 0x%02X", address_);
    ESP_LOGI(TAG, "initialized successfully");
    return ESP_OK;
}

esp_err_t MPU6050::read(Data& data)
{
    RawData raw{};

    const esp_err_t err = readRawData(raw);
    if (err != ESP_OK)
    {
        return err;
    }

    return translateRawData(&raw, data);
}

esp_err_t MPU6050::readRawData(RawData& data)
{
    std::array<uint8_t, 14> buffer{};

    const esp_err_t err = readRegisters(
        REG_ACCEL_XOUT_H, buffer.data(), buffer.size());

    if (err != ESP_OK)
    {
        return err;
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

esp_err_t MPU6050::translateRawData(
    const RawData* data, Data& out)
{
    if (data == nullptr)
    {
        return ESP_ERR_INVALID_ARG;
    }

    out.acc_x = static_cast<float>(data->acc_x)
                / ACCEL_SCALE * GRAVITY - cal_.x_bias;
    out.acc_y = static_cast<float>(data->acc_y)
                / ACCEL_SCALE * GRAVITY - cal_.y_bias;
    out.acc_z = static_cast<float>(data->acc_z)
                / ACCEL_SCALE * GRAVITY - cal_.z_bias;
    out.temperature = static_cast<float>(data->temperature) / 340.0f + 36.53f;

    return ESP_OK;
}
