#include "MAX30102.hpp"
#include "HeartRate.h"
#include <utility>
#include <initializer_list>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{
    constexpr char TAG[] = "MAX30102";

    constexpr uint8_t REG_INT_STATUS = 0x00; // Reading acknowledges latched flags.
    constexpr uint8_t REG_FIFO_WR_PTR = 0x04; // Next FIFO write position.
    constexpr uint8_t REG_FIFO_OVF = 0x05; // Lost-record counter.
    constexpr uint8_t REG_FIFO_RD_PTR = 0x06; // Next unread FIFO position.
    constexpr uint8_t REG_FIFO_DATA = 0x07; // Burst-read optical records here.
    constexpr uint8_t REG_FIFO_CONFIG = 0x08; // Averaging, rollover, full threshold.
    constexpr uint8_t REG_MODE_CONFIG = 0x09; // Reset, shutdown and measurement mode.
    constexpr uint8_t REG_MEASUREMENT_CONFIG = 0x0A; // ADC range, sample rate, pulse width.
    constexpr uint8_t REG_RED_CURRENT = 0x0C; // Red LED pulse current.
    constexpr uint8_t REG_IR_CURRENT = 0x0D; // IR LED pulse current (unused in HR mode).
    constexpr uint8_t REG_PART_ID = 0xFF;
    // Shared by other MAX3010x parts; this alone cannot identify the exact model.
    constexpr uint8_t EXPECTED_PART_ID = 0x15;

    constexpr uint8_t MODE_RESET = 0x40;
    constexpr uint8_t MODE_SHUTDOWN = 0x80;
    constexpr uint8_t MODE_HR = 0x02; // Dedicated HR mode, red LED only.
    // Bits 7:5 = 010: average 4 conversions; bit 4 = rollover enabled.
    // Bits 3:0 = 15: almost-full at 17 records; interrupts remain disabled.
    constexpr uint8_t FIFO_CONFIG = 0x5F;
    // ADC range 4096 nA (bits 6:5=01), 400 conversions/s (4:2=011),
    // 411 us pulse / 18-bit ADC (1:0=11). Four-sample averaging yields 100 Hz.
    // Allowed ranges: 2048/4096/8192/16384 nA; longer pulses trade power for SNR.
    constexpr uint8_t MEASUREMENT_CONFIG = 0x2F;
    constexpr uint8_t RED_CURRENT = 0x24; // About 7.2 mA; tune using real skin recordings.
    constexpr uint8_t FIFO_POINTER_MASK = 0x1F;
    constexpr size_t FIFO_DEPTH = 32;
    constexpr size_t BYTES_PER_SAMPLE = 3;
    constexpr uint32_t SAMPLE_MASK = 0x3FFFF;
    // Heuristic guards, not calibrated contact or signal-quality measurements.
    constexpr uint32_t MIN_SIGNAL = 5000;
    constexpr uint32_t CLIPPING_LEVEL = SAMPLE_MASK * 98 / 100;

    // FIFO output rate: 400 ADC conversions/s divided by 4-sample averaging.
    // Changing this also requires matching register settings and filter design.
    constexpr uint32_t MAX30102_SAMPLE_RATE_HZ = 100;
}

esp_err_t MAX30102::init()
{
    if (device_handle_ != nullptr) return ESP_OK;
    esp_err_t err = addDevice();
    if (err != ESP_OK) return err;

    uint8_t part_id = 0;
    err = readRegisters(REG_PART_ID, &part_id, 1);
    if (err == ESP_OK && part_id != EXPECTED_PART_ID) err = ESP_ERR_NOT_FOUND;
    if (err == ESP_OK) err = writeRegister(REG_MODE_CONFIG, MODE_RESET);
    const int64_t deadline = esp_timer_get_time() + 100000;
    while (err == ESP_OK)
    {
        uint8_t mode = 0;
        err = readRegisters(REG_MODE_CONFIG, &mode, 1);
        if (err != ESP_OK || !(mode & MODE_RESET)) break;
        if (esp_timer_get_time() >= deadline) { err = ESP_ERR_TIMEOUT; break; }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Configure while stopped, then start with an empty FIFO.
    if (err == ESP_OK) err = writeRegister(REG_FIFO_CONFIG, FIFO_CONFIG);
    if (err == ESP_OK) err = writeRegister(REG_MEASUREMENT_CONFIG, MEASUREMENT_CONFIG);
    if (err == ESP_OK) err = writeRegister(REG_RED_CURRENT, RED_CURRENT);
    if (err == ESP_OK) err = writeRegister(REG_IR_CURRENT, 0);
    if (err == ESP_OK) err = resetFIFO();
    if (err == ESP_OK) err = writeRegister(REG_MODE_CONFIG, MODE_HR);

    // Detect rejected settings rather than silently processing at the wrong rate.
    for (const auto& setting : {std::pair{REG_FIFO_CONFIG, FIFO_CONFIG},
                               std::pair{REG_MEASUREMENT_CONFIG, MEASUREMENT_CONFIG},
                               std::pair{REG_MODE_CONFIG, MODE_HR}})
    {
        if (err != ESP_OK) break;
        uint8_t actual = 0;
        err = readRegisters(setting.first, &actual, 1);
        if (err == ESP_OK && actual != setting.second) err = ESP_ERR_INVALID_RESPONSE;
    }
    uint8_t status = 0;
    if (err == ESP_OK) err = readRegisters(REG_INT_STATUS, &status, 1);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Initialization failed: %s", esp_err_to_name(err));
        deinit();
        return err;
    }
    resetProcessing();
    ESP_LOGI(TAG, "Initialized successfully: red HR mode, 400 Hz / 4 = 100 Hz, 18-bit");
    return ESP_OK;
}

esp_err_t MAX30102::read(Data& data)
{
    data = {};
    uint8_t interrupt_status = 0;
    esp_err_t err = readRegisters(REG_INT_STATUS, &interrupt_status, 1);
    if (err != ESP_OK) return err;
    // ALC_OVF: ambient-light cancellation exceeded its range. Do not interpret
    // these optical values as beats; retain the configured mode when recovering.
    if (interrupt_status & 0x20)
    {
        ESP_LOGW(TAG, "Excess ambient light; shield sensor from external light");
        err = recoverFIFO();
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }
    if (interrupt_status & 0x01) // PWR_RDY after initial acknowledgement = supply reset.
    {
        ESP_LOGW(TAG, "Sensor power reset; reinitializing configuration");
        resetProcessing();
        deinit();
        err = init();
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }

    // Read RD first so an asynchronously arriving record cannot undercount the
    // available queue. Only the sensor changes WR, only this task consumes RD.
    uint8_t rd = 0, wr = 0, overflow = 0;
    err = readRegisters(REG_FIFO_RD_PTR, &rd, 1);
    if (err == ESP_OK) err = readRegisters(REG_FIFO_WR_PTR, &wr, 1);
    if (err == ESP_OK) err = readRegisters(REG_FIFO_OVF, &overflow, 1);
    if (err != ESP_OK) return err;
    if (overflow & FIFO_POINTER_MASK)
    {
        ESP_LOGW(TAG, "FIFO overflow; discarding discontinuous samples");
        err = recoverFIFO();
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }
    const size_t pending = (wr - rd) & FIFO_POINTER_MASK;
    if (!pending) return ESP_ERR_NOT_FOUND;

    // One burst drains the snapshot, rather than one I2C transaction per sample.
    uint8_t bytes[FIFO_DEPTH * BYTES_PER_SAMPLE]{};
    err = readRegisters(REG_FIFO_DATA, bytes, pending * BYTES_PER_SAMPLE);
    if (err != ESP_OK)
    {
        // A partial transfer may have advanced RD; never bridge that time gap.
        recoverFIFO();
        return err;
    }
    for (size_t i = 0; i < pending; ++i)
    {
        const uint8_t* p = bytes + i * BYTES_PER_SAMPLE;
        const RawData raw{((uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2]) & SAMPLE_MASK};
        translateRawData(&raw, data);
    }
    return ESP_OK;
}

esp_err_t MAX30102::translateRawData(const RawData* raw, Data& out)
{
    if (!raw) return ESP_ERR_INVALID_ARG;

    // Reject weak/saturated samples. Reset the detector on contact changes
    // instead of treating large optical steps as pulse beats.
    if (raw->red < MIN_SIGNAL || raw->red >= CLIPPING_LEVEL)
    {
        resetProcessing();
        out = {};
        return ESP_OK;
    }

    ++sample_index_;
    const bool beat = checkForBeat(raw->red);
    if (settling_samples_ < MAX30102_SAMPLE_RATE_HZ * 2)
    {
        ++settling_samples_;
        return ESP_OK;
    }

    if (!beat) return ESP_OK;

    if (have_previous_beat_)
    {
        const uint64_t interval = (sample_index_ - last_beat_index_) * 1000 / MAX30102_SAMPLE_RATE_HZ;
        // Refractory period: do not let a secondary peak move the previous beat.
        if (interval < 300) return ESP_OK;
        out.ppi_valid = interval <= 2000;
        out.ppi_ms = out.ppi_valid ? static_cast<uint32_t>(interval) : 0;
        out.hr = out.ppi_valid ? 60000.0f / out.ppi_ms : 0;
    }

    out.beat_detected = true;
    last_beat_index_ = sample_index_;
    have_previous_beat_ = true;
    return ESP_OK;
}

esp_err_t MAX30102::resetFIFO()
{
    esp_err_t err = writeRegister(REG_FIFO_WR_PTR, 0);
    if (err == ESP_OK) err = writeRegister(REG_FIFO_RD_PTR, 0);
    if (err == ESP_OK) err = writeRegister(REG_FIFO_OVF, 0);
    return err;
}

esp_err_t MAX30102::recoverFIFO()
{
    resetProcessing();
    // Stop production before clearing pointers; consume one complete record to
    // acknowledge any latched overflow counter before returning to an empty FIFO.
    esp_err_t err = writeRegister(REG_MODE_CONFIG, MODE_SHUTDOWN | MODE_HR);
    uint8_t discard[BYTES_PER_SAMPLE]{};
    if (err == ESP_OK) err = readRegisters(REG_FIFO_DATA, discard, sizeof(discard));
    if (err == ESP_OK) err = resetFIFO();
    // Always attempt to resume, including after a failed recovery transaction.
    const esp_err_t resume = writeRegister(REG_MODE_CONFIG, MODE_HR);
    return err == ESP_OK ? resume : err;
}

void MAX30102::resetProcessing()
{
    sample_index_ = last_beat_index_ = 0;
    settling_samples_ = 0;
    have_previous_beat_ = false;
    resetHeartRate();
}
