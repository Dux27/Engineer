#include "MAX30105.hpp"


namespace
{
    constexpr char TAG[] = "MAX30105";

    constexpr uint8_t REG_FIFO_WR_PTR  = 0x04; // FIFO position where the sensor writes the next sample.
    constexpr uint8_t REG_FIFO_OVF     = 0x05; // Counts samples lost when the FIFO is full.
    constexpr uint8_t REG_FIFO_RD_PTR  = 0x06; // FIFO position of the next sample to read.
    constexpr uint8_t REG_FIFO_DATA    = 0x07; // Read here to retrieve queued measurement bytes.
    constexpr uint8_t REG_FIFO_CONFIG  = 0x08; // Controls averaging, rollover, and almost-full threshold.
    constexpr uint8_t REG_MODE_CONFIG  = 0x09; // Controls operating mode, shutdown, and reset.
    constexpr uint8_t REG_SPO2_CONFIG  = 0x0A; // Sets ADC range, sampling rate, and LED pulse width.
    constexpr uint8_t REG_LED1_PA      = 0x0C; // Sets red LED pulse current (brightness).
    constexpr uint8_t REG_LED2_PA      = 0x0D; // Sets infrared LED pulse current (brightness).
    constexpr uint8_t REG_LED3_PA      = 0x0E; // Sets green LED pulse current (brightness).
    constexpr uint8_t REG_MULTI_LED_1  = 0x11; // Selects LEDs for measurement slots 1 and 2.
    constexpr uint8_t REG_MULTI_LED_2  = 0x12; // Selects LEDs for measurement slots 3 and 4.
    constexpr uint8_t REG_PART_ID      = 0xFF; // Read-only register containing the sensor part ID.

    constexpr uint8_t EXPECTED_PART_ID = 0x15; // Expected value used to check the connected sensor.

    // Modes: 0x02 = Red, 0x03 = Red + IR, 0x07 = Multi-LED.
    constexpr uint8_t MODE_RESET     = 0x40;
    constexpr uint8_t MODE_MULTI_LED = 0x07;

    // Bits 7:5 - Averaging: 1, 2, 4, 8, 16, 32 samples.
    // Bit 4    - FIFO rollover: 0 = Off, 1 = On.
    // Bits 3:0 - Almost-full threshold: 0–15 empty slots.
    // Selected: 4 samples, rollover On, threshold 15.
    constexpr uint8_t FIFO_CONFIG =             // -> 00000000
        (0b010 << 5) |  // Average 4 samples    // -> 01000000
        (0b1 << 4)   |  // Enable rollover      // -> 01010000
        0b1111;         // Threshold = 15       // -> 01011111

    // Bits 6:5 - ADC range: 2048, 4096, 8192, 16384 nA.
    // Bits 4:2 - Sample rate: 50–3200 Hz (8 options).
    // Bits 1:0 - Pulse width and resolution:
    //            69/118/215/411 us = 15/16/17/18 bits.
    // Selected: 16384 nA, 400 Hz, 411 us (18-bit).
    constexpr uint8_t SPO2_CONFIG = 0x6F;

    // LED current: 0–255 (~0–50 mA).
    // Selected: 36 (~7.2 mA).
    constexpr uint8_t LED_CURRENT = 0x24;

    // Slot options: 0 = Off, 1 = Red, 2 = IR, 3 = Green.
    // Selected: Red → IR → Green.
    constexpr uint8_t SLOTS_1_2 = 0x21; // Slot 1 = Red, Slot 2 = IR.
    constexpr uint8_t SLOTS_3_4 = 0x03; // Slot 3 = Green, Slot 4 = Off.

    // FIFO: 32 positions, pointers use 5 bits.
    constexpr uint8_t FIFO_POINTER_MASK = 0x1F;
    constexpr uint8_t FIFO_RESET_VALUE = 0;

    // 3 active LEDs × 3 bytes per sample.
    constexpr size_t FIFO_SAMPLE_SIZE = 9;

    // Keep only the lowest 18 bits of the 24-bit FIFO sample.
    constexpr uint32_t SAMPLE_MASK = 0x3FFFF;
}

esp_err_t MAX30105::init()
{
    if (device_handle_ != nullptr)
    {
        return ESP_OK;
    }

    esp_err_t err = addDevice();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add I2C device: %s",
                 esp_err_to_name(err));
        return err;
    }

    uint8_t part_id = 0;

    err = readRegisters(REG_PART_ID, &part_id, 1);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read part ID: %s", esp_err_to_name(err));
        deinit();
        return err;
    }

    if (part_id != EXPECTED_PART_ID)
    {
        ESP_LOGE(TAG, "Unexpected part ID: 0x%02X", part_id);
        deinit();
        return ESP_ERR_NOT_FOUND;
    }

    // Reset the sensor.
    err = writeRegister(REG_MODE_CONFIG, MODE_RESET);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to reset sensor: %s", esp_err_to_name(err));
        deinit();
        return err;
    }

    // Clear FIFO pointers and overflow counter.
    err = resetFIFO();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to reset FIFO: %s", esp_err_to_name(err));
        deinit();
        return err;
    }

    // FIFO averaging: 4 samples; enable FIFO rollover.
    err = writeRegister(REG_FIFO_CONFIG, FIFO_CONFIG);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure FIFO: %s", esp_err_to_name(err));
        deinit();
        return err;
    }

    // ADC range: 16384 nA, requested sample rate: 400 samples/s,
    // LED pulse width: 411 us.
    err = writeRegister(REG_SPO2_CONFIG, SPO2_CONFIG);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure ADC and sample timing: %s", esp_err_to_name(err));
        deinit();
        return err;
    }

    // Configure red, infrared, and green LED currents.
    err = writeRegister(REG_LED1_PA, LED_CURRENT);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure red LED current: %s", esp_err_to_name(err));
        deinit();
        return err;
    }
    err = writeRegister(REG_LED2_PA, LED_CURRENT);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure infrared LED current: %s", esp_err_to_name(err));
        deinit();
        return err;
    }
    err = writeRegister(REG_LED3_PA, LED_CURRENT);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure green LED current: %s", esp_err_to_name(err));
        deinit();
        return err;
    }

    // Slot 1 = red, slot 2 = infrared.
    err = writeRegister(REG_MULTI_LED_1, SLOTS_1_2);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure slots 1 and 2: %s", esp_err_to_name(err));
        deinit();
        return err;
    }

    // Slot 3 = green, slot 4 disabled.
    err = writeRegister(REG_MULTI_LED_2, SLOTS_3_4);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure slots 3 and 4: %s", esp_err_to_name(err));
        deinit();
        return err;
    }

    // Start multi-LED mode.
    err = writeRegister(REG_MODE_CONFIG, MODE_MULTI_LED);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start multi-LED mode: %s", esp_err_to_name(err));
        deinit();
        return err;
    }

    ESP_LOGI(TAG, "Detected at 0x%02X", address_);
    ESP_LOGI(TAG, "Initialized successfully");
    return ESP_OK;
}

esp_err_t MAX30105::read(Data& data)
{
    RawData raw{};

    const esp_err_t err = readRawData(raw);
    if (err != ESP_OK)
    {
        return err;
    }

    return translateRawData(&raw, data);
}

esp_err_t MAX30105::readRawData(RawData& data)
{
    uint8_t write_ptr = 0;
    uint8_t read_ptr = 0;

    esp_err_t err = readRegisters(REG_FIFO_WR_PTR, &write_ptr, 1);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read FIFO write pointer: %s", esp_err_to_name(err));
        return err;
    }

    err = readRegisters(REG_FIFO_RD_PTR, &read_ptr, 1);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read FIFO read pointer: %s", esp_err_to_name(err));
        return err;
    }

    write_ptr &= FIFO_POINTER_MASK;
    read_ptr &= FIFO_POINTER_MASK;

    // Equal pointers are treated as empty here. Full/wrapped FIFO detection
    // also requires overflow/full-state handling; pointers alone are ambiguous.
    if (write_ptr == read_ptr)
    {
        return ESP_ERR_NOT_FOUND;
    }

    // Read the oldest unread record, not necessarily the newest measurement.
    // The sensor advances its read pointer as the record is consumed.
    uint8_t fifo[FIFO_SAMPLE_SIZE]{};

    err = readRegisters(REG_FIFO_DATA, fifo, sizeof(fifo));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read FIFO sample: %s", esp_err_to_name(err));
        return err;
    }

    data.red =
        ((static_cast<uint32_t>(fifo[0]) << 16) |
         (static_cast<uint32_t>(fifo[1]) << 8) |
          static_cast<uint32_t>(fifo[2])) & SAMPLE_MASK;

    data.infrared =
        ((static_cast<uint32_t>(fifo[3]) << 16) |
         (static_cast<uint32_t>(fifo[4]) << 8) |
          static_cast<uint32_t>(fifo[5])) & SAMPLE_MASK;

    data.green =
        ((static_cast<uint32_t>(fifo[6]) << 16) |
         (static_cast<uint32_t>(fifo[7]) << 8) |
          static_cast<uint32_t>(fifo[8])) & SAMPLE_MASK;

    return ESP_OK;
}

esp_err_t MAX30105::translateRawData(const RawData* data, Data& out)
{
    if (data == nullptr)
    {
        ESP_LOGE(TAG, "Cannot translate a null raw sample");
        return ESP_ERR_INVALID_ARG;
    }

    // These are raw optical ADC readings, not calibrated physical units.
    out.red = data->red;
    out.infrared = data->infrared;
    out.green = data->green;

    return ESP_OK;
}

esp_err_t MAX30105::resetFIFO()
{
    esp_err_t err = writeRegister(REG_FIFO_WR_PTR, FIFO_RESET_VALUE);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to clear FIFO write pointer: %s", esp_err_to_name(err));
        return err;
    }

    err = writeRegister(REG_FIFO_OVF, FIFO_RESET_VALUE);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to clear FIFO overflow counter: %s", esp_err_to_name(err));
        return err;
    }

    err = writeRegister(REG_FIFO_RD_PTR, FIFO_RESET_VALUE);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to clear FIFO read pointer: %s", esp_err_to_name(err));
    }
    return err;
}
