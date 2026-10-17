#include "TPiS1385.hpp"
#include "esp_err.h"


namespace
{
    constexpr char TAG[] = "TPiS1385";

    constexpr uint8_t REG_MEASUREMENT_START = 1;
    constexpr size_t MEASUREMENT_REGISTER_COUNT = 19;

    constexpr float KELVIN_OFFSET = 273.15f;
    // Power law exponent defined by LOOKUP# = 1 in the TPiS1385 datasheet.
    constexpr float EXPONENT = 3.8f;
}

esp_err_t TPiS1385::init()
{
    if (device_handle_ != nullptr)
    {
        return ESP_OK;
    }

    esp_err_t err = sendGeneralCall(bus_handle_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "General call failed: %s",
                esp_err_to_name(err));
        return err;
    }
    esp_rom_delay_us(1000);

    err = addDevice();
    if (err != ESP_OK)
    {
        return err;
    }

    ESP_LOGI(TAG, "Detected at 0x%02X", address_);
    ESP_LOGI(TAG, "Initialized successfully");
    return ESP_OK;
}

esp_err_t TPiS1385::read(Data& data)
{
    RawData raw_data{};

    const esp_err_t err = readRawData(raw_data);
    if (err != ESP_OK)
    {
        return err;
    }

    return translateRawData(&raw_data, data);
}

esp_err_t TPiS1385::readRawData(RawData& raw_data)
{
    if (device_handle_ == nullptr)
    {
        return ESP_ERR_INVALID_STATE;
    }

    // Read registers 1 through 19 sequentially.
    uint8_t reg = REG_MEASUREMENT_START;

    std::array<uint8_t, MEASUREMENT_REGISTER_COUNT> data{};

    const esp_err_t err = i2c_master_transmit_receive(
        device_handle_,
        &reg,
        sizeof(reg),
        data.data(),
        data.size(),
        I2C_TIMEOUT_MS);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Measurement read failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    // Object measurement: registers 1, 2, and bit 7 of register 3.
    raw_data.object_raw =
        (static_cast<uint32_t>(data[0]) << 9) |
        (static_cast<uint32_t>(data[1]) << 1) |
        ((data[2] >> 7) & 0x01);

    // Ambient measurement: bits 6:0 of register 3 and register 4.
    raw_data.ambient_raw =
        (static_cast<uint16_t>(data[2] & 0x7F) << 8) |
        data[3];

    // Registers 15 through 19.
    raw_data.presence = data[14];
    raw_data.motion = data[15];
    raw_data.ambient_shock = data[16];
    raw_data.interrupt_status = data[17];
    raw_data.chip_status = data[18];

    return ESP_OK;
}

esp_err_t TPiS1385::translateRawData(const RawData* data, Data& out)
{
    if (data == nullptr)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (cal_.m <= 0.0f ||
        cal_.skin_emissivity <= 0.0f ||
        cal_.fov_coverage <= 0.0f)
    {
        return ESP_ERR_INVALID_STATE;
    }

    // Calculate ambient temperature.
    const float ambient_k =
        cal_.reference_temp_k +
        (static_cast<float>(data->ambient_raw) - cal_.ptat25) / cal_.m;

    if (!std::isfinite(ambient_k) || ambient_k <= 0.0f)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Fixed TPiS1385 lookup function.
    const auto f = [](float temperature_k) -> float
    {
        return std::pow(temperature_k, EXPONENT);
    };

    const float reference_object_k =
        cal_.reference_object_temp_c + KELVIN_OFFSET;

    const float denominator =
        f(reference_object_k) - f(cal_.reference_temp_k);

    if (!std::isfinite(denominator) || denominator <= 0.0f)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Scale factor corrected for emissivity and FOV coverage.
    const float k =
        ((cal_.uout1 - cal_.u0) / denominator) *
        cal_.skin_emissivity *
        cal_.fov_coverage;

    if (!std::isfinite(k) || k <= 0.0f)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Calculate object temperature.
    const float object_term =
        (static_cast<float>(data->object_raw) - cal_.u0) / k +
        f(ambient_k);

    if (!std::isfinite(object_term) || object_term <= 0.0f)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const float skin_k =
        std::pow(object_term, 1.0f / EXPONENT);

    if (!std::isfinite(skin_k) || skin_k <= 0.0f)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    out.ambient_temperature = ambient_k - KELVIN_OFFSET;
    out.skin_temperature = skin_k - KELVIN_OFFSET;

    return ESP_OK;
}

esp_err_t TPiS1385::sendGeneralCall(i2c_master_bus_handle_t bus_handle)
{
    i2c_master_dev_handle_t general_call_handle = nullptr;

    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = 0x00;
    config.scl_speed_hz = I2C_FREQUENCY_HZ;

    esp_err_t err = i2c_master_bus_add_device(
        bus_handle,
        &config,
        &general_call_handle);

    ESP_LOGI(TAG, "General call add device: %s",
             esp_err_to_name(err));

    if (err != ESP_OK)
    {
        return err;
    }

    const uint8_t command = 0x04;

    err = i2c_master_transmit(
        general_call_handle,
        &command,
        sizeof(command),
        I2C_TIMEOUT_MS);

    ESP_LOGI(TAG, "General call transmit: %s",
             esp_err_to_name(err));

    esp_err_t rm_err = i2c_master_bus_rm_device(
        general_call_handle);

    ESP_LOGI(TAG, "General call remove device: %s",
             esp_err_to_name(rm_err));

    return err;
}
