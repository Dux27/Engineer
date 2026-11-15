#include "GroveGSR.hpp"


namespace
{
    constexpr char TAG[] = "GroveGSR";

    // ADC resolution: 12 bits, 0..4095 counts.
    constexpr adc_bitwidth_t BIT_WIDTH = ADC_BITWIDTH_12;
    constexpr int ADC_MAX = (1 << BIT_WIDTH) - 1;

    // ADC attenuation: 12 dB, approximately 0..3100 mV on ESP32-S3.
    constexpr adc_atten_t ATTENUATION = ADC_ATTEN_DB_12;

    // Number of consecutive readings averaged per measurement.
    constexpr uint32_t SAMPLE_COUNT = 16;
}

GroveGSR::GroveGSR(gpio_num_t pin)
    : pin_(pin) {}

GroveGSR::~GroveGSR()
{
    deinit();
}

esp_err_t GroveGSR::init()
{
    if (adc_ != nullptr)
    {
        return ESP_OK;
    }

    // Find the ADC unit and channel assigned to the GPIO.
    adc_unit_t unit;
    esp_err_t err = adc_oneshot_io_to_channel(pin_, &unit, &channel_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "GPIO %d is not an ADC input: %s",
                 pin_, esp_err_to_name(err));
        return err;
    }

    // Create the ADC unit in oneshot mode.
    adc_oneshot_unit_init_cfg_t unit_config = {};
    unit_config.unit_id = unit;
    unit_config.ulp_mode = ADC_ULP_MODE_DISABLE;

    err = adc_oneshot_new_unit(&unit_config, &adc_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create ADC unit: %s",
                 esp_err_to_name(err));
        return err;
    }

    // Configure ADC resolution and input attenuation.
    adc_oneshot_chan_cfg_t channel_config = {};
    channel_config.atten = ATTENUATION;
    channel_config.bitwidth = BIT_WIDTH;

    err = adc_oneshot_config_channel(adc_, channel_, &channel_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure ADC channel: %s",
                 esp_err_to_name(err));
        deinit();
        return err;
    }

    // Use factory calibration to convert ADC counts to millivolts.
    adc_cali_curve_fitting_config_t calibration_config = {};
    calibration_config.unit_id = unit;
    calibration_config.chan = channel_;
    calibration_config.atten = ATTENUATION;
    calibration_config.bitwidth = BIT_WIDTH;

    err = adc_cali_create_scheme_curve_fitting(
        &calibration_config, &calibration_);

    if (err == ESP_ERR_NOT_SUPPORTED)
    {
        // Keep raw readings available when voltage calibration is unsupported.
        calibration_ = nullptr;
        ESP_LOGW(TAG, "ADC voltage calibration unavailable; reporting raw counts only");
    }
    else if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure ADC calibration: %s",
                 esp_err_to_name(err));
        deinit();
        return err;
    }

    ESP_LOGI(TAG, "Initialized successfully: GPIO=%d ADC%d channel=%d, 12-bit, average=%u",
             pin_, static_cast<int>(unit) + 1, channel_,
             static_cast<unsigned>(SAMPLE_COUNT));
    return ESP_OK;
}

esp_err_t GroveGSR::read(Data& data)
{
    data = {};
    RawData raw_data{};

    esp_err_t err = readRawData(raw_data);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "ADC measurement failed: %s", esp_err_to_name(err));
        return err;
    }

    err = translateRawData(&raw_data, data);
    if (err != ESP_OK)
    {
        data = {};
        ESP_LOGE(TAG, "ADC measurement failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t GroveGSR::readRawData(RawData& raw_data)
{
    if (adc_ == nullptr)
    {
        return ESP_ERR_INVALID_STATE;
    }

    raw_data = {};
    uint32_t sum = 0;

    // Average consecutive conversions to reduce ADC noise.
    for (uint32_t i = 0; i < SAMPLE_COUNT; ++i)
    {
        int value = 0;
        const esp_err_t err = adc_oneshot_read(adc_, channel_, &value);
        if (err != ESP_OK)
        {
            return err;
        }

        if (value < 0 || value > ADC_MAX)
        {
            return ESP_ERR_INVALID_RESPONSE;
        }

        sum += value;
        raw_data.clipped |= value == 0 || value == ADC_MAX;
    }

    // Round the mean to the nearest ADC count.
    raw_data.adc = static_cast<uint16_t>(
        (sum + SAMPLE_COUNT / 2) / SAMPLE_COUNT);

    return ESP_OK;
}

esp_err_t GroveGSR::translateRawData(const RawData* data, Data& out)
{
    if (data == nullptr)
    {
        return ESP_ERR_INVALID_ARG;
    }

    out.raw = data->adc;
    out.clipped = data->clipped;

    // Convert the averaged reading to millivolts when calibration is available.
    if (calibration_ != nullptr)
    {
        const esp_err_t err = adc_cali_raw_to_voltage(
            calibration_, data->adc, &out.voltage_mv);

        if (err != ESP_OK)
        {
            return err;
        }

        out.voltage_valid = true;
    }

    return ESP_OK;
}

void GroveGSR::deinit()
{
    if (calibration_ != nullptr)
    {
        const esp_err_t err = adc_cali_delete_scheme_curve_fitting(calibration_);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to release ADC calibration: %s",
                     esp_err_to_name(err));
        }
        calibration_ = nullptr;
    }

    if (adc_ != nullptr)
    {
        const esp_err_t err = adc_oneshot_del_unit(adc_);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to release ADC unit: %s",
                     esp_err_to_name(err));
        }
        adc_ = nullptr;
    }
}
