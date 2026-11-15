#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "soc/gpio_num.h"
#include "soc/gpio_struct.h"

// === GENERAL ===
inline constexpr uint16_t LOG_FREQUENCY_HZ = 10; // ACC, EDA, TEMP
inline constexpr uint16_t BVP_LOG_FREQUENCY_HZ = 50;

// === I2C CONFIGURATION ===
inline constexpr gpio_num_t I2C_0_SDA_GPIO = GPIO_NUM_5;
inline constexpr gpio_num_t I2C_0_SCL_GPIO = GPIO_NUM_6;
// Temperature bus: D8 (GPIO7) SDA, D7 (GPIO44) SCL; console uses USB.
inline constexpr gpio_num_t I2C_1_SDA_GPIO = GPIO_NUM_7;
inline constexpr gpio_num_t I2C_1_SCL_GPIO = GPIO_NUM_44;

inline constexpr i2c_port_t I2C_0_PORT = I2C_NUM_0;
inline constexpr i2c_port_t I2C_1_PORT = I2C_NUM_1;

inline constexpr uint32_t I2C_FREQUENCY_HZ = 100000;
inline constexpr int16_t I2C_TIMEOUT_MS = 100;

// === SENSORS CONFIGURATION ===
// [ACC] MPU6050
inline constexpr uint8_t MPU6050_I2C_ADDRESS = 0x68;
inline constexpr uint8_t ADXL345_I2C_ADDRESS = 0x53;
inline constexpr uint8_t ADXL345_ALT_I2C_ADDRESS = 0x1D;

// [TEMP] Excelitas TPiS1385
inline constexpr uint8_t TPIS1385_I2C_ADDRESS = 0x0D;

// [BVP] MAX30102
inline constexpr uint8_t MAX30102_I2C_ADDRESS = 0x57;
// FIFO: 400 conversions/s / 2 = 200 Hz. Anti-alias FIR decimates to 100 Hz
// for the existing beat detector; raw diagnostic records remain at 200 Hz.
inline constexpr uint32_t MAX30102_FIFO_RATE_HZ = 200;
inline constexpr uint32_t MAX30102_SAMPLE_RATE_HZ = 100;
// Enable temporarily for waveform recordings; normal use logs quality at 1 Hz.
inline constexpr bool BVP_RAW_DIAGNOSTICS = false;

// [EDA] Seeed Studio Grove GSR
inline constexpr gpio_num_t GROVEGSR_ADC_PIN = GPIO_NUM_2;
