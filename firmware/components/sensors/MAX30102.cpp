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
    constexpr uint8_t REG_INT_STATUS = 0x00;         // Reading acknowledges latched flags.
    constexpr uint8_t REG_FIFO_WR_PTR = 0x04;        // Next FIFO write position.
    constexpr uint8_t REG_FIFO_OVF = 0x05;           // Lost-record counter.
    constexpr uint8_t REG_FIFO_RD_PTR = 0x06;        // Next unread FIFO position.
    constexpr uint8_t REG_FIFO_DATA = 0x07;          // Burst-read optical records here.
    constexpr uint8_t REG_FIFO_CONFIG = 0x08;        // Averaging, rollover, full threshold.
    constexpr uint8_t REG_MODE_CONFIG = 0x09;        // Reset, shutdown and measurement mode.
    constexpr uint8_t REG_MEASUREMENT_CONFIG = 0x0A; // ADC range, sample rate, pulse width.
    constexpr uint8_t REG_RED_CURRENT = 0x0C;        // Red LED pulse current.
    constexpr uint8_t REG_IR_CURRENT = 0x0D;
    constexpr uint8_t REG_PART_ID = 0xFF;
    // Shared by other MAX3010x parts; this alone cannot identify the exact model.
    constexpr uint8_t EXPECTED_PART_ID = 0x15;

    constexpr uint8_t MODE_RESET = 0x40;
    constexpr uint8_t MODE_SHUTDOWN = 0x80;
    constexpr uint8_t MODE_HR = 0x03; // Red + IR acquisition (no SpO2 computation).
    // Bits 7:5 = 001: average 2 conversions; bit 4 = rollover enabled.
    // Bits 3:0 = 15: almost-full at 17 records; interrupts remain disabled.
    constexpr uint8_t FIFO_CONFIG = 0x3F;
    // ADC range 4096 nA (bits 6:5=01), 400 conversions/s (4:2=011),
    // 411 us pulse / 18-bit ADC (1:0=11). Two-sample averaging yields 200 Hz.
    // Allowed ranges: 2048/4096/8192/16384 nA; longer pulses trade power for SNR.
    constexpr uint8_t MEASUREMENT_CONFIG = 0x2F;
    constexpr uint8_t RED_CURRENT = 0x24; // About 7.2 mA; tune using real skin recordings.
    constexpr uint8_t IR_CURRENT = 0x36;  // 10.8 mA; wrist trial, monitor ADC clipping.
    constexpr uint8_t FIFO_POINTER_MASK = 0x1F;
    constexpr size_t FIFO_DEPTH = 32;
    constexpr size_t BYTES_PER_SAMPLE = 6;
    constexpr uint32_t SAMPLE_MASK = 0x3FFFF;
    // Heuristic guards, not calibrated contact or signal-quality measurements.
    constexpr uint32_t MIN_SIGNAL = 5000;
    constexpr uint32_t CLIPPING_LEVEL = SAMPLE_MASK * 98 / 100;
    static_assert(MAX30102_SAMPLE_RATE_HZ == 100, "Beat detector requires decimated 100 Hz input");
    static_assert(MAX30102_FIFO_RATE_HZ == 200, "FIR is designed for 200 Hz FIFO");
} // namespace

esp_err_t MAX30102::init()
{
    if (device_handle_ != nullptr)
        return ESP_OK;
    esp_err_t err = addDevice();
    if (err != ESP_OK)
        return err;

    uint8_t part_id = 0;
    err = readRegisters(REG_PART_ID, &part_id, 1);
    if (err == ESP_OK && part_id != EXPECTED_PART_ID)
        err = ESP_ERR_NOT_FOUND;
    if (err == ESP_OK)
        err = writeRegister(REG_MODE_CONFIG, MODE_RESET);
    const int64_t deadline = esp_timer_get_time() + 100000;
    while (err == ESP_OK)
    {
        uint8_t mode = 0;
        err = readRegisters(REG_MODE_CONFIG, &mode, 1);
        if (err != ESP_OK || !(mode & MODE_RESET))
            break;
        if (esp_timer_get_time() >= deadline)
        {
            err = ESP_ERR_TIMEOUT;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Configure while stopped, then start with an empty FIFO.
    if (err == ESP_OK)
        err = writeRegister(REG_FIFO_CONFIG, FIFO_CONFIG);
    if (err == ESP_OK)
        err = writeRegister(REG_MEASUREMENT_CONFIG, MEASUREMENT_CONFIG);
    if (err == ESP_OK)
        err = writeRegister(REG_RED_CURRENT, RED_CURRENT);
    if (err == ESP_OK)
        err = writeRegister(REG_IR_CURRENT, IR_CURRENT);
    if (err == ESP_OK)
        err = resetFIFO();
    if (err == ESP_OK)
        err = writeRegister(REG_MODE_CONFIG, MODE_HR);

    // Detect rejected settings rather than silently processing at the wrong rate.
    for (const auto &setting : {std::pair{REG_FIFO_CONFIG, FIFO_CONFIG},
                                std::pair{REG_MEASUREMENT_CONFIG, MEASUREMENT_CONFIG},
                                std::pair{REG_MODE_CONFIG, MODE_HR}})
    {
        if (err != ESP_OK)
            break;
        uint8_t actual = 0;
        err = readRegisters(setting.first, &actual, 1);
        if (err == ESP_OK && actual != setting.second)
            err = ESP_ERR_INVALID_RESPONSE;
    }
    uint8_t status = 0;
    if (err == ESP_OK)
        err = readRegisters(REG_INT_STATUS, &status, 1);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Initialization failed: %s", esp_err_to_name(err));
        deinit();
        return err;
    }
    resetProcessing();
    ESP_LOGI(TAG,
             "Initialized successfully: red + IR, FIFO 400/2=200 Hz; FIR -> HR 100 Hz, 18-bit; "
             "RED=0x%02X IR=0x%02X HR=IR",
             RED_CURRENT, IR_CURRENT);
    return ESP_OK;
}

esp_err_t MAX30102::read(Data &data)
{
    data = {};
    uint8_t interrupt_status = 0;
    esp_err_t err = readRegisters(REG_INT_STATUS, &interrupt_status, 1);
    if (err != ESP_OK)
        return err;
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
    if (err == ESP_OK)
        err = readRegisters(REG_FIFO_WR_PTR, &wr, 1);
    if (err == ESP_OK)
        err = readRegisters(REG_FIFO_OVF, &overflow, 1);
    if (err != ESP_OK)
        return err;
    if (overflow & FIFO_POINTER_MASK)
    {
        ESP_LOGW(TAG, "FIFO overflow; discarding discontinuous samples");
        err = recoverFIFO();
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }
    const size_t pending = (wr - rd) & FIFO_POINTER_MASK;
    if (!pending)
        return ESP_ERR_NOT_FOUND;

    // One burst drains the snapshot, rather than one I2C transaction per sample.
    uint8_t bytes[FIFO_DEPTH * BYTES_PER_SAMPLE]{};
    err = readRegisters(REG_FIFO_DATA, bytes, pending * BYTES_PER_SAMPLE);
    if (err != ESP_OK)
    {
        // A partial transfer may have advanced RD; never bridge that time gap.
        recoverFIFO();
        return err;
    }
    const int64_t processing_start = esp_timer_get_time();
    if (!fifo_epoch_us_)
        fifo_epoch_us_ = processing_start;
    else
    {
        fifo_clock_samples_ += pending;
        const int64_t elapsed = processing_start - fifo_epoch_us_;
        if (elapsed >= 10000000)
        {
            const float observed = static_cast<float>(fifo_clock_samples_) * 1000000.0f / elapsed;
            if (observed > 0.9f * MAX30102_FIFO_RATE_HZ && observed < 1.1f * MAX30102_FIFO_RATE_HZ)
                effective_rate_hz_ = observed * 0.5f;
        }
    }
    for (size_t i = 0; i < pending; ++i)
    {
        const uint8_t *p = bytes + i * BYTES_PER_SAMPLE;
        const uint32_t ir = ((uint32_t(p[3]) << 16) | (uint32_t(p[4]) << 8) | p[5]) & SAMPLE_MASK;
        if (ir < MIN_SIGNAL || ir >= CLIPPING_LEVEL)
        {
            resetProcessing();
            data = {};
            continue;
        }
        float filtered = 0;
        if (decimator_.push(static_cast<float>(ir), filtered))
        {
            const RawData raw{static_cast<uint32_t>(
                std::fmax(0.0f, std::fmin(float(SAMPLE_MASK), filtered)) + 0.5f)};
            translateRawData(&raw, data);
        }
    }
    // Populate diagnostic fields after processing, which can reset beat output.
    data.raw_count = static_cast<uint8_t>(pending);
    data.quality_valid = period_estimator_.valid;
    data.periodicity = period_estimator_.correlation;
    data.relative_ac = period_estimator_.relative_ac;
    data.detector_rate_hz = effective_rate_hz_;
    if (period_estimator_.estimate_id != last_estimate_id_)
    {
        last_estimate_id_ = period_estimator_.estimate_id;
        waveform_history_.update(period_estimator_.candidate_bpm, sample_index_,
                                 period_estimator_.valid);
        data.hr_updated = waveform_history_.valid;
    }
    data.hr_valid = period_estimator_.valid && waveform_history_.valid;
    if (data.hr_valid)
    {
        data.hr = waveform_history_.bpm * effective_rate_hz_ / MAX30102_SAMPLE_RATE_HZ;
        data.hr_window_size = 3;
    }
    for (size_t i = 0; i < pending; ++i)
    {
        const uint8_t *p = bytes + i * BYTES_PER_SAMPLE;
        data.raw_red[i] = ((uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2]) & SAMPLE_MASK;
        data.raw_ir[i] = ((uint32_t(p[3]) << 16) | (uint32_t(p[4]) << 8) | p[5]) & SAMPLE_MASK;
    }
    data.processing_us = static_cast<uint32_t>(esp_timer_get_time() - processing_start);
    return ESP_OK;
}

esp_err_t MAX30102::translateRawData(const RawData *raw, Data &out)
{
    if (!raw)
        return ESP_ERR_INVALID_ARG;
    // Reject weak/near-saturated samples. Reset the detector on contact changes
    // instead of treating large optical steps as pulse beats.
    if (raw->ir < MIN_SIGNAL || raw->ir >= CLIPPING_LEVEL)
    {
        resetProcessing();
        out = {};
        return ESP_OK;
    }
    ++sample_index_;
    period_estimator_.push(raw->ir);
    const bool beat = checkForBeat(raw->ir);
    if (settling_samples_ < MAX30102_SAMPLE_RATE_HZ * 2)
    {
        ++settling_samples_;
        return ESP_OK;
    }
    if (!beat)
        return ESP_OK;
    if (have_previous_beat_)
    {
        const uint64_t interval_samples = sample_index_ - last_beat_index_;
        const float interval = interval_samples * 1000.0f / effective_rate_hz_;
        const float period_bpm =
            period_estimator_.candidate_bpm * effective_rate_hz_ / MAX30102_SAMPLE_RATE_HZ;
        // Refractory period: do not let a secondary peak move the previous beat.
        const float refractory =
            period_estimator_.valid ? std::fmax(300.0f, 0.6f * 60000.0f / period_bpm) : 300.0f;
        if (interval < refractory)
            return ESP_OK;
        const float candidate_hr = interval ? 60000.0f / interval : 0;
        // An early noise peak rejected by the period-consistency gate must NOT
        // become the reference beat: doing so hides the following real beat
        // behind the refractory period and biases HR downwards.
        if (period_estimator_.valid && candidate_hr > 1.20f * period_bpm)
            return ESP_OK;
        out.ppi_valid = interval <= 2000 && period_estimator_.valid &&
                        std::fabs(candidate_hr - period_bpm) <= 0.20f * period_bpm;
        out.ppi_ms = out.ppi_valid ? static_cast<uint32_t>(std::lround(interval)) : 0;
    }
    last_beat_index_ = sample_index_;
    have_previous_beat_ = true;
    return ESP_OK;
}

esp_err_t MAX30102::resetFIFO()
{
    esp_err_t err = writeRegister(REG_FIFO_WR_PTR, 0);
    if (err == ESP_OK)
        err = writeRegister(REG_FIFO_RD_PTR, 0);
    if (err == ESP_OK)
        err = writeRegister(REG_FIFO_OVF, 0);
    return err;
}

esp_err_t MAX30102::recoverFIFO()
{
    resetProcessing();
    // Stop production before clearing pointers; consume one complete record to
    // acknowledge any latched overflow counter before returning to an empty FIFO.
    esp_err_t err = writeRegister(REG_MODE_CONFIG, MODE_SHUTDOWN | MODE_HR);
    uint8_t discard[BYTES_PER_SAMPLE]{};
    if (err == ESP_OK)
        err = readRegisters(REG_FIFO_DATA, discard, sizeof(discard));
    if (err == ESP_OK)
        err = resetFIFO();
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
    period_estimator_.reset();
    decimator_.reset();
    last_estimate_id_ = 0;
    waveform_history_.reset();
    fifo_epoch_us_ = 0;
    fifo_clock_samples_ = 0;
    effective_rate_hz_ = MAX30102_SAMPLE_RATE_HZ;
}
