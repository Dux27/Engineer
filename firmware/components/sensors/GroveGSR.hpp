#pragma once

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "esp_err.h"

#include <cstdint>


class GroveGSR
{
public:
    struct RawData
    {
        uint16_t adc = 0; // Mean of a short burst, 0..4095 with a 12-bit ADC.
        bool clipped = false; // At least one conversion reached either ADC rail.
    };

    struct Data
    {
        uint16_t raw = 0;
        int voltage_mv = 0; // Only valid when voltage_valid is true.
        bool voltage_valid = false;
        bool clipped = false;
    };

    explicit GroveGSR(gpio_num_t pin) : pin_(pin) {}
    ~GroveGSR();
    GroveGSR(const GroveGSR&) = delete;
    GroveGSR& operator=(const GroveGSR&) = delete;

    esp_err_t init();
    esp_err_t read(Data& data);

private:
    esp_err_t readRawData(RawData& raw);
    esp_err_t translateRawData(const RawData* raw, Data& out);
    void deinit();

    gpio_num_t pin_;
    adc_channel_t channel_ = ADC_CHANNEL_0;
    adc_oneshot_unit_handle_t adc_ = nullptr;
    adc_cali_handle_t calibration_ = nullptr;
};
