#include "TPiS1385.hpp"

namespace
{
    constexpr char TAG[] = "TPiS1385";

    constexpr uint8_t REG_SLAVE_ADDRESS = 63;
    constexpr uint8_t REG_MEASUREMENT_START = 1;
    constexpr size_t MEASUREMENT_REGISTER_COUNT = 19;

    // Placeholder calibration values for initial testing.
    // Replace with the sensor's factory calibration values.
    constexpr float PTAT25 = 13500.0f;
    constexpr float M = 172.0f;
    constexpr float U0 = 64500.0f;
    constexpr float UOUT1 = 71000.0f;
    constexpr float TOBJ1_C = 40.0f;

    constexpr uint8_t LOOKUP = 1;
    constexpr float SKIN_EMISSIVITY = 0.98f;
    constexpr float FOV_COVERAGE = 1.0f;

    constexpr float REFERENCE_TEMP_K = 298.15f;
    constexpr float KELVIN_OFFSET = 273.15f;
    constexpr float EXPONENT = 3.8f;
}

TPiS1385::TPiS1385(
    i2c_master_bus_handle_t bus,
    uint8_t address)
    : bus_handle_(bus),
      address_(address)
{
}

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

    esp_err_t err = sendGeneralCall(bus_handle_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "General call failed: %s",
                esp_err_to_name(err));
        return err;
    }

    esp_rom_delay_us(1000);

    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address_;
    config.scl_speed_hz = I2C_FREQUENCY_HZ;

    err = i2c_master_bus_add_device(
        bus_handle_,
        &config,
        &device_handle_);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add I2C device: %s",
                 esp_err_to_name(err));
        return err;
    }

    uint8_t slave_address_register = 0;

    err = readRegister(
        REG_SLAVE_ADDRESS,
        slave_address_register);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "Not responding at address 0x%02X: %s",
                 address_,
                 esp_err_to_name(err));

        deinit();
        return err;
    }

    ESP_LOGI(TAG,
             "Detected at 0x%02X; register %u = 0x%02X",
             address_,
             REG_SLAVE_ADDRESS,
             slave_address_register);

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

esp_err_t TPiS1385::writeRegister(
    uint8_t reg,
    uint8_t value)
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
        I2C_TIMEOUT_MS);
}

esp_err_t TPiS1385::readRegister(
    uint8_t reg,
    uint8_t& value)
{
    if (device_handle_ == nullptr)
    {
        return ESP_ERR_INVALID_STATE;
    }

    return i2c_master_transmit_receive(
        device_handle_,
        &reg,
        sizeof(reg),
        &value,
        sizeof(value),
        I2C_TIMEOUT_MS);
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

    if (M <= 0.0f || LOOKUP != 1)
    {
        return ESP_ERR_INVALID_STATE;
    }

    // Calculate ambient temperature.
    const float ambient_k =
        REFERENCE_TEMP_K +
        (static_cast<float>(data->ambient_raw) - PTAT25) / M;

    if (!std::isfinite(ambient_k) || ambient_k <= 0.0f)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Lookup function for LOOKUP# = 1.
    const auto f = [](float temperature_k) -> float
    {
        return std::pow(temperature_k, EXPONENT);
    };

    const float reference_object_k =
        TOBJ1_C + KELVIN_OFFSET;

    const float denominator =
        f(reference_object_k) - f(REFERENCE_TEMP_K);

    if (!std::isfinite(denominator) || denominator <= 0.0f)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Scale factor corrected for emissivity and FOV coverage.
    const float k =
        ((UOUT1 - U0) / denominator) *
        SKIN_EMISSIVITY *
        FOV_COVERAGE;

    if (!std::isfinite(k) || k <= 0.0f)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Calculate object temperature.
    const float object_term =
        (static_cast<float>(data->object_raw) - U0) / k +
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

    out.ambient_temperature =
        ambient_k - KELVIN_OFFSET;

    out.skin_temperature =
        skin_k - KELVIN_OFFSET;

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

void TPiS1385::deinit()
{
    if (device_handle_ != nullptr)
    {
        i2c_master_bus_rm_device(device_handle_);
        device_handle_ = nullptr;
    }
}
