#pragma once

#include <MPU6050.hpp>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_log.h>


class Application
{
public:
    bool init();
    void loop();

private:
    MPU6050 imu_;
};
