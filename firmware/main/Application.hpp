#pragma once

#include <MPU6050.hpp>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_log.h>


class Application
{
public:
    esp_err_t init();
    void loop();

private:
    MPU6050 imu_;
    MPU6050::Data imu_data_{};
};
