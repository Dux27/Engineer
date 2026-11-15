#include "ADXL345.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{
    constexpr char TAG[] = "ADXL345";
    constexpr uint8_t REG_DEVID = 0x00;
    constexpr uint8_t REG_BW_RATE = 0x2C;
    constexpr uint8_t REG_POWER_CTL = 0x2D;
    constexpr uint8_t REG_INT_ENABLE = 0x2E;
    constexpr uint8_t REG_DATA_FORMAT = 0x31;
    constexpr uint8_t REG_DATAX0 = 0x32;
    constexpr uint8_t REG_FIFO_CTL = 0x38;

    // Full-resolution mode: nominal sensitivity 3.9 mg/LSB (AN-1077).
    float decodeAcceleration(uint8_t low, uint8_t high)
    {
        const int value = static_cast<int>(low) | (static_cast<int>(high) << 8);
        const int signed_value = value >= 32768 ? value - 65536 : value;
        return static_cast<float>(signed_value) * 0.0039f * 9.80665f;
    }
} // namespace

esp_err_t ADXL345::init()
{
    if (device_handle_ != nullptr)
        return ESP_OK;
    esp_err_t err = addDevice();
    if (err != ESP_OK)
        return err;

    uint8_t id = 0;
    err = readRegisters(REG_DEVID, &id, 1);
    if (err == ESP_OK && id != 0xE5)
        err = ESP_ERR_INVALID_RESPONSE;
    // Standby while configuring. Full resolution, +/-16 g, right-justified,
    // normal-power 100 Hz ODR. Polling needs neither FIFO nor interrupt wires.
    if (err == ESP_OK)
        err = writeRegister(REG_POWER_CTL, 0x00);
    if (err == ESP_OK)
        err = writeRegister(REG_INT_ENABLE, 0x00);
    if (err == ESP_OK)
        err = writeRegister(REG_FIFO_CTL, 0x00);
    if (err == ESP_OK)
        err = writeRegister(REG_DATA_FORMAT, 0x0B);
    if (err == ESP_OK)
        err = writeRegister(REG_BW_RATE, 0x0A);
    if (err == ESP_OK)
        err = writeRegister(REG_POWER_CTL, 0x08);

    uint8_t format = 0, power = 0, rate = 0;
    if (err == ESP_OK)
        err = readRegisters(REG_DATA_FORMAT, &format, 1);
    if (err == ESP_OK)
        err = readRegisters(REG_POWER_CTL, &power, 1);
    if (err == ESP_OK)
        err = readRegisters(REG_BW_RATE, &rate, 1);
    if (err == ESP_OK && (format != 0x0B || power != 0x08 || rate != 0x0A))
        err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Initialization at 0x%02X failed: %s (DEVID=0x%02X)", address_,
                 esp_err_to_name(err), id);
        deinit();
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "Initialized successfully at 0x%02X: DEVID=0xE5, full resolution +/-16 g, 100 Hz",
             address_);
    return ESP_OK;
}

esp_err_t ADXL345::read(Data &data)
{
    uint8_t bytes[6]{};
    const esp_err_t err = readRegisters(REG_DATAX0, bytes, sizeof(bytes));
    if (err != ESP_OK)
        return err;
    // One multibyte transaction preserves XYZ coherency. ADXL345 is little-endian.
    data.acc_x = decodeAcceleration(bytes[0], bytes[1]);
    data.acc_y = decodeAcceleration(bytes[2], bytes[3]);
    data.acc_z = decodeAcceleration(bytes[4], bytes[5]);
    return ESP_OK;
}
