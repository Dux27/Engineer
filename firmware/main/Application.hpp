#pragma once

#include "GlobalConfig.hpp"
#include "MPU6050.hpp"
#include "MAX30102.hpp"
#include "TPiS1385.hpp"
#include "GroveGSR.hpp"

#include "freertos/projdefs.h"
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "esp_log.h"
#include "esp_err.h"

#include <memory>


class Application
{
public:
    Application() = default;
    ~Application();

    esp_err_t init();
    void loop();
private:
    i2c_master_bus_handle_t bus_handle_0_ = nullptr;
    i2c_master_bus_handle_t bus_handle_1_ = nullptr;

    std::unique_ptr<MPU6050> acc_;
    MPU6050::Data acc_data_{};
    bool acc_initialized_ {false};

    std::unique_ptr<MAX30102> bvp_;
    MAX30102::Data bvp_data_{};
    bool bvp_initialized_ {false};

    std::unique_ptr<TPiS1385> temp_;
    TPiS1385::Data temp_data_{};
    bool temp_initialized_ {false};

    std::unique_ptr<GroveGSR> eda_;
    GroveGSR::Data eda_data_{};
    bool eda_initialized_ {false};

    void scanI2CBus(i2c_master_bus_handle_t);
};
