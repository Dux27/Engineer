#pragma once

#include "GlobalConfig.hpp"
#include "MPU6050.hpp"

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"

#include <memory>


class Application
{
public:
    Application() = default;
    ~Application();

    esp_err_t init();
    void loop();
private:
    i2c_master_bus_handle_t bus_handle_ = nullptr;

    std::unique_ptr<MPU6050> imu_;
    MPU6050::Data imu_data_{};
};
