#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"


// I2C configuration
inline constexpr gpio_num_t I2C_SDA_GPIO = GPIO_NUM_8;
inline constexpr gpio_num_t I2C_SCL_GPIO = GPIO_NUM_9;

inline constexpr i2c_port_t I2C_PORT = I2C_NUM_0;
inline constexpr uint32_t I2C_FREQUENCY_HZ = 400000;

// MPU6050
inline constexpr uint8_t MPU6050_I2C_ADDRESS = 0x68;
inline constexpr gpio_num_t MPU6050_INT_GPIO = GPIO_NUM_7;
