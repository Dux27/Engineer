#include "Application.hpp"
#include "GlobalConfig.hpp"
#include "MAX30102.hpp"
#include "esp_err.h"
#include "freertos/projdefs.h"
#include <cstdio>
#include <initializer_list>
#include "esp_system.h"
#include "esp_timer.h"

static const char *TAG = "APP";

Application::~Application()
{
    acc_.reset();
    adxl_.reset();
    bvp_.reset();
    temp_.reset();
    eda_.reset();

    if (bus_handle_0_ != nullptr)
    {
        i2c_del_master_bus(bus_handle_0_);
        bus_handle_0_ = nullptr;
    }
    if (bus_handle_1_ != nullptr)
    {
        i2c_del_master_bus(bus_handle_1_);
        bus_handle_1_ = nullptr;
    }
}

esp_err_t Application::init()
{
    i2c_master_bus_config_t bus_config_0 = {};
    bus_config_0.i2c_port = I2C_0_PORT;
    bus_config_0.sda_io_num = I2C_0_SDA_GPIO;
    bus_config_0.scl_io_num = I2C_0_SCL_GPIO;
    bus_config_0.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config_0.glitch_ignore_cnt = 7;
    bus_config_0.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&bus_config_0, &bus_handle_0_);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize I2C bus 0: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C bus 0 handle: %p", bus_handle_0_);
    scanI2CBus(bus_handle_0_);

    i2c_master_bus_config_t bus_config_1 = {};
    bus_config_1.i2c_port = I2C_1_PORT;
    bus_config_1.sda_io_num = I2C_1_SDA_GPIO;
    bus_config_1.scl_io_num = I2C_1_SCL_GPIO;
    bus_config_1.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config_1.glitch_ignore_cnt = 7;
    bus_config_1.flags.enable_internal_pullup = true;

    err = i2c_new_master_bus(&bus_config_1, &bus_handle_1_);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize I2C bus 1: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C bus 1 handle: %p", bus_handle_1_);
    scanI2CBus(bus_handle_1_);

    acc_initialized_ = bvp_initialized_ = temp_initialized_ = eda_initialized_ = false;
    // Prefer the replacement ADXL345; retain the original MPU driver as fallback.
    for (const uint8_t address : {ADXL345_I2C_ADDRESS, ADXL345_ALT_I2C_ADDRESS})
    {
        if (i2c_master_probe(bus_handle_0_, address, 20) != ESP_OK)
            continue;
        adxl_ = std::make_unique<ADXL345>(bus_handle_0_, address);
        if (adxl_->init() == ESP_OK)
            break;
        adxl_.reset();
    }
    if (adxl_ == nullptr)
    {
        acc_ = std::make_unique<MPU6050>(bus_handle_0_, MPU6050_I2C_ADDRESS);

        err = acc_->init();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to initialize MPU6050: %s", esp_err_to_name(err));
            acc_.reset();
        }
        else
        {
            acc_initialized_ = true;
        }
    }

    bvp_ = std::make_unique<MAX30102>(bus_handle_0_, MAX30102_I2C_ADDRESS);

    err = bvp_->init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize MAX30102: %s", esp_err_to_name(err));
        bvp_.reset();
    }
    else
    {
        bvp_initialized_ = true;
    }

    temp_ = std::make_unique<TPiS1385>(bus_handle_1_, TPIS1385_I2C_ADDRESS);

    err = temp_->init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize TPiS1385: %s", esp_err_to_name(err));
        temp_.reset();
    }
    else
    {
        temp_initialized_ = true;
    }

    eda_ = std::make_unique<GroveGSR>(GROVEGSR_ADC_PIN);
    err = eda_->init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize GroveGSR: %s", esp_err_to_name(err));
        eda_.reset();
    }
    else
    {
        eda_initialized_ = true;
    }

    ESP_LOGI(TAG, "Sensors ready: ADXL345=%s MPU6050=%s MAX30102=%s TEMP=%s EDA_ADC=%s",
             adxl_ ? "yes" : "no", acc_initialized_ ? "yes" : "no", bvp_initialized_ ? "yes" : "no",
             temp_initialized_ ? "yes" : "no", eda_initialized_ ? "yes" : "no");
    // Missing sensors must not block acquisition from sensors that did initialize.
    // Reboot after connecting a missing sensor with power off.

    return ESP_OK;
}

void Application::loop()
{
    TickType_t wake_time = xTaskGetTickCount();
    TickType_t last_slow_read = wake_time - pdMS_TO_TICKS(1000 / LOG_FREQUENCY_HZ);
    TickType_t last_quality_log = wake_time;
    TickType_t last_diagnostic_log = wake_time;
    uint32_t bvp_samples_since_log = 0;
    uint32_t max_bvp_processing_us = 0, max_bvp_gap_us = 0;
    int64_t last_bvp_poll_us = esp_timer_get_time();
    while (true)
    {
        const TickType_t now = xTaskGetTickCount();
        if (now - last_diagnostic_log >= pdMS_TO_TICKS(10000))
        {
            last_diagnostic_log = now;
            ESP_LOGI(TAG,
                     "[DIAG] stack_free_min=%u heap_free=%lu heap_min=%lu "
                     "bvp_processing_max_us=%lu bvp_poll_gap_max_us=%lu",
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                     static_cast<unsigned long>(esp_get_free_heap_size()),
                     static_cast<unsigned long>(esp_get_minimum_free_heap_size()),
                     static_cast<unsigned long>(max_bvp_processing_us),
                     static_cast<unsigned long>(max_bvp_gap_us));
        }
        const bool read_slow_sensors = now - last_slow_read >= pdMS_TO_TICKS(1000 / LOG_FREQUENCY_HZ);
        if (read_slow_sensors)
            last_slow_read = now;
        if (read_slow_sensors && adxl_ != nullptr)
        {
            const esp_err_t err = adxl_->read(adxl_data_);
            if (err != ESP_OK)
                ESP_LOGE(TAG, "Failed to read ADXL345: %s", esp_err_to_name(err));
            else
                ESP_LOGI(TAG, "[ACC] ACC: X=%.3f Y=%.3f Z=%.3f | SENSOR=ADXL345", adxl_data_.acc_x,
                         adxl_data_.acc_y, adxl_data_.acc_z);
        }
        if (read_slow_sensors && acc_initialized_ && acc_ != nullptr)
        {
            const esp_err_t err = acc_->read(acc_data_);

            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to read MPU6050: %s", esp_err_to_name(err));
            }
            else
            {
                ESP_LOGI(TAG, "[ACC] ACC: X=%.3f Y=%.3f Z=%.3f | DEVICE-TEMP=%.2f C",
                         acc_data_.acc_x, acc_data_.acc_y, acc_data_.acc_z, acc_data_.temperature);
            }
        }

        if (bvp_initialized_ && bvp_ != nullptr)
        {
            const int64_t poll_us = esp_timer_get_time();
            const uint32_t gap_us = static_cast<uint32_t>(poll_us - last_bvp_poll_us);
            if (gap_us > max_bvp_gap_us)
                max_bvp_gap_us = gap_us;
            last_bvp_poll_us = poll_us;
            const esp_err_t err = bvp_->read(bvp_data_);
            if (bvp_data_.processing_us > max_bvp_processing_us)
                max_bvp_processing_us = bvp_data_.processing_us;

            if (err != ESP_OK && err != ESP_ERR_NOT_FOUND)
            {
                ESP_LOGE(TAG, "Failed to read MAX30102: %s", esp_err_to_name(err));
            }
            else if (err == ESP_OK)
            {
                bvp_samples_since_log += bvp_data_.raw_count;
                if (now - last_quality_log >= pdMS_TO_TICKS(1000))
                {
                    const float measured_rate = static_cast<float>(bvp_samples_since_log) *
                                                configTICK_RATE_HZ /
                                                static_cast<float>(now - last_quality_log);
                    bvp_samples_since_log = 0;
                    last_quality_log = now;
                    ESP_LOGI(TAG,
                             "[BVP_QUALITY] status=%s periodicity=%.3f relative_ac=%.6f channel=IR "
                             "rate=%.1f Hz detector_rate=%.3f Hz",
                             bvp_data_.quality_valid ? "periodic" : "unreliable",
                             bvp_data_.periodicity, bvp_data_.relative_ac, measured_rate,
                             bvp_data_.detector_rate_hz);
                }
                if (BVP_RAW_DIAGNOSTICS && bvp_data_.raw_count)
                {
                    char line[32 * 8 + 1]{};
                    size_t used = 0;
                    for (unsigned i = 0; i < bvp_data_.raw_count; ++i)
                    {
                        const int length =
                            snprintf(line + used, sizeof(line) - used, "%s%lu", i ? "," : "",
                                     static_cast<unsigned long>(bvp_data_.raw_red[i]));
                        if (length < 0 || static_cast<size_t>(length) >= sizeof(line) - used)
                            break;
                        used += static_cast<size_t>(length);
                    }
                    ESP_LOGI(TAG, "[BVP_RAW] %s", line);
                    used = 0;
                    for (unsigned i = 0; i < bvp_data_.raw_count; ++i)
                    {
                        const int length =
                            snprintf(line + used, sizeof(line) - used, "%s%lu", i ? "," : "",
                                     static_cast<unsigned long>(bvp_data_.raw_ir[i]));
                        if (length < 0 || static_cast<size_t>(length) >= sizeof(line) - used)
                            break;
                        used += static_cast<size_t>(length);
                    }
                    ESP_LOGI(TAG, "[BVP_IR] %s", line);
                }
                if (bvp_data_.hr_updated && bvp_data_.hr_valid)
                {
                    ESP_LOGI(TAG, "[BVP_HR] HR=%.1f bpm | SOURCE=IR-periodicity | MEDIAN=%u",
                             bvp_data_.hr, bvp_data_.hr_window_size);
                }
                if (bvp_data_.ppi_valid)
                {
                    ESP_LOGI(TAG, "[BVP_PPI] PPI=%u ms", bvp_data_.ppi_ms);
                }
            }
        }

        if (read_slow_sensors && temp_initialized_ && temp_ != nullptr)
        {
            const esp_err_t err = temp_->read(temp_data_);

            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to read TPiS1385: %s", esp_err_to_name(err));
            }
            else
            {
                ESP_LOGI(TAG, "[TEMP] SKIN-TEMP=%.2f C | DEVICE-TEMP: %.2f C",
                         temp_data_.skin_temperature, temp_data_.ambient_temperature);
            }
        }

        if (read_slow_sensors && eda_initialized_ && eda_ != nullptr)
        {
            const esp_err_t err = eda_->read(eda_data_);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to read GroveGSR: %s", esp_err_to_name(err));
            }
            else if (eda_data_.voltage_valid)
            {
                ESP_LOGI(TAG, "[EDA] Raw=%u | Voltage=%d mV | Clipped=%s",
                         static_cast<unsigned>(eda_data_.raw), eda_data_.voltage_mv,
                         eda_data_.clipped ? "yes" : "no");
            }
            else
            {
                ESP_LOGI(TAG, "[EDA] Raw=%u | Voltage=unavailable | Clipped=%s",
                         static_cast<unsigned>(eda_data_.raw), eda_data_.clipped ? "yes" : "no");
            }
        }

        // Service PPG at 50 Hz; ACC/TEMP/EDA logging stays at approximately 10 Hz.
        vTaskDelayUntil(&wake_time, pdMS_TO_TICKS(1000 / BVP_LOG_FREQUENCY_HZ));
    }
}

void Application::scanI2CBus(i2c_master_bus_handle_t bus)
{
    if (bus == nullptr)
    {
        ESP_LOGE(TAG, "[%p] Invalid I2C bus handle", bus);
        return;
    }

    int found = 0;

    ESP_LOGI(TAG, "[%p] Scanning I2C bus...", bus);

    for (uint8_t address = 0x08; address <= 0x77; ++address)
    {
        esp_err_t err = i2c_master_probe(bus, address, 100);
        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "[%p] Found device at 0x%02X", bus, address);
            ++found;
        }
    }

    ESP_LOGI(TAG, "[%p] Scan complete: %d device(s) found", bus, found);
}
