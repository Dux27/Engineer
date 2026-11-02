#include "GroveGSR.hpp"


namespace
{
    constexpr char TAG[] = "GroveGSR";
    constexpr adc_bitwidth_t BIT_WIDTH = ADC_BITWIDTH_12;
    // ESP32-S3's widest ADC range (~0..3100 mV); attenuation is not overvoltage protection.
    constexpr adc_atten_t ATTENUATION = ADC_ATTEN_DB_12;
    constexpr uint32_t SAMPLE_COUNT = 16;
    constexpr int ADC_MAX = (1 << 12) - 1;
}

GroveGSR::~GroveGSR()
{
    deinit();
}

esp_err_t GroveGSR::init()

{
    if (adc_ != nullptr) return ESP_OK;

    // Resolve the configured GPIO rather than assuming its ADC unit/channel.
    adc_unit_t unit;
    esp_err_t err = adc_oneshot_io_to_channel(pin_, &unit, &channel_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "GPIO %d is not an ADC input: %s", pin_, esp_err_to_name(err));
        return err;
    }

    adc_oneshot_unit_init_cfg_t unit_config = {};
    unit_config.unit_id = unit;
    unit_config.ulp_mode = ADC_ULP_MODE_DISABLE;
    err = adc_oneshot_new_unit(&unit_config, &adc_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create ADC unit: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t channel_config = {};
    channel_config.atten = ATTENUATION;
    channel_config.bitwidth = BIT_WIDTH;
    err = adc_oneshot_config_channel(adc_, channel_, &channel_config);
    if (err == ESP_OK)
    {
        adc_cali_curve_fitting_config_t config = {};
        config.unit_id = unit;
        config.chan = channel_;
        config.atten = ATTENUATION;
        config.bitwidth = BIT_WIDTH;
        err = adc_cali_create_scheme_curve_fitting(&config, &calibration_);
        if (err == ESP_ERR_NOT_SUPPORTED)
        {
            // Raw acquisition remains useful if factory calibration is unavailable.
            calibration_ = nullptr;
            ESP_LOGW(TAG, "ADC voltage calibration unavailable; reporting raw counts only");
            err = ESP_OK;
        }
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "ADC configuration failed: %s", esp_err_to_name(err));
        deinit();
        return err;
    }
    // This confirms ADC setup, not presence/contact of the analog sensor.
    ESP_LOGI(TAG, "Initialized successfully: GPIO=%d ADC%d channel=%d, 12-bit, average=%u",
             pin_, static_cast<int>(unit) + 1, channel_, static_cast<unsigned>(SAMPLE_COUNT));
    return ESP_OK;
}

esp_err_t GroveGSR::read(Data& data)
{
    data = {};
    RawData raw;
    esp_err_t err = readRawData(raw);
    if (err == ESP_OK) err = translateRawData(&raw, data);
    if (err != ESP_OK)
    {
        data = {};
        ESP_LOGE(TAG, "ADC measurement failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t GroveGSR::readRawData(RawData& raw)
{
    if (adc_ == nullptr) return ESP_ERR_INVALID_STATE;
    raw = {};
    uint32_t sum = 0;
    // Short burst only: no blocking delays that would starve the PPG FIFO.
    for (uint32_t i = 0; i < SAMPLE_COUNT; ++i)
    {
        int value = 0;
        const esp_err_t err = adc_oneshot_read(adc_, channel_, &value);
        if (err != ESP_OK) return err;
        if (value < 0 || value > ADC_MAX) return ESP_ERR_INVALID_RESPONSE;
        sum += value;
        raw.clipped |= value == 0 || value == ADC_MAX;
    }
    raw.adc = static_cast<uint16_t>((sum + SAMPLE_COUNT / 2) / SAMPLE_COUNT);
    return ESP_OK;
}

esp_err_t GroveGSR::translateRawData(const RawData* raw, Data& out)
{
    if (raw == nullptr) return ESP_ERR_INVALID_ARG;
    out.raw = raw->adc;
    out.clipped = raw->clipped;
    if (calibration_ != nullptr)
    {
        const esp_err_t err = adc_cali_raw_to_voltage(calibration_, raw->adc, &out.voltage_mv);
        if (err != ESP_OK) return err;
        out.voltage_valid = true;
    }
    return ESP_OK;
}

void GroveGSR::deinit()
{
    if (calibration_ != nullptr)
    {
        const esp_err_t err = adc_cali_delete_scheme_curve_fitting(calibration_);
        if (err != ESP_OK) ESP_LOGE(TAG, "Failed to release ADC calibration: %s", esp_err_to_name(err));
        calibration_ = nullptr;
    }
    if (adc_ != nullptr)
    {
        const esp_err_t err = adc_oneshot_del_unit(adc_);
        if (err != ESP_OK) ESP_LOGE(TAG, "Failed to release ADC unit: %s", esp_err_to_name(err));
        adc_ = nullptr;
    }
}
