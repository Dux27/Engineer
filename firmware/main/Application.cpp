#include "Application.hpp"
#include "GlobalConfig.hpp"

static const char* TAG = "Application";

Application::~Application()
{
    imu_.reset();

    if (bus_handle_ != nullptr)
    {
        i2c_del_master_bus(bus_handle_);
        bus_handle_ = nullptr;
    }
}

esp_err_t Application::init()
{
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = I2C_SDA_GPIO;
    bus_config.scl_io_num = I2C_SCL_GPIO;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;

    esp_err_t err = i2c_new_master_bus(&bus_config, &bus_handle_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize I2C bus: %s",
                 esp_err_to_name(err));
        return err;
    }

    imu_ = std::make_unique<MPU6050>(
        bus_handle_,
        MPU6050_I2C_ADDRESS);

    err = imu_->init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize MPU6050: %s",
                 esp_err_to_name(err));
        imu_.reset();
        return err;
    }

    return ESP_OK;
}

void Application::loop()
{
    while(true)
    {
        const esp_err_t err = imu_->read(imu_data_);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to read MPU6050: %s", esp_err_to_name(err));
            return;
        }

        ESP_LOGI(TAG, "ACC: X=%.3f Y=%.3f Z=%.3f | TEMP=%.3f",
                imu_data_.acc_x,
                imu_data_.acc_y,
                imu_data_.acc_z,
                imu_data_.temperature);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
