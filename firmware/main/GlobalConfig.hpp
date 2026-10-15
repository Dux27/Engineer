#pragma once

#include "TPiS1385.hpp"
#include "driver/gpio.h"
#include "driver/i2c_master.h"


// I2C configuration
inline constexpr gpio_num_t I2C_0_SDA_GPIO = GPIO_NUM_8;
inline constexpr gpio_num_t I2C_0_SCL_GPIO = GPIO_NUM_9;
inline constexpr gpio_num_t I2C_1_SDA_GPIO = GPIO_NUM_5;
inline constexpr gpio_num_t I2C_1_SCL_GPIO = GPIO_NUM_4;

inline constexpr i2c_port_t I2C_0_PORT = I2C_NUM_0;
inline constexpr i2c_port_t I2C_1_PORT = I2C_NUM_1;

inline constexpr uint32_t I2C_FREQUENCY_HZ = 100000;
inline constexpr int16_t I2C_TIMEOUT_MS = 100;

// MPU6050
inline constexpr uint8_t MPU6050_I2C_ADDRESS = 0x68;

// Excelitas TPiS1385
inline constexpr uint8_t TPIS1385_I2C_ADDRESS = 0x0D;
