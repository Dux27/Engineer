#include "Application.hpp"


static const char* TAG = "APP";

Application::~Application()
{
    acc_.reset();
    temp_.reset();

    if (bus_handle_0_ != nullptr)
    {
        i2c_del_master_bus(bus_handle_0_);
        bus_handle_0_ = nullptr;
    }
    if (bus_handle_1_ != nullptr)
    {
        i2c_del_master_bus(bus_handle_1_);
        bus_handle_1_ = nullptr;
    }
}

esp_err_t Application::init()
{
    i2c_master_bus_config_t bus_config_0 = {};
    bus_config_0.i2c_port = I2C_0_PORT;
    bus_config_0.sda_io_num = I2C_0_SDA_GPIO;
    bus_config_0.scl_io_num = I2C_0_SCL_GPIO;
    bus_config_0.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config_0.glitch_ignore_cnt = 7;
    bus_config_0.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(
        &bus_config_0, &bus_handle_0_);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize I2C bus 0: %s",
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C bus 0 handle: %p", bus_handle_0_);

    i2c_master_bus_config_t bus_config_1 = {};
    bus_config_1.i2c_port = I2C_1_PORT;
    bus_config_1.sda_io_num = I2C_1_SDA_GPIO;
    bus_config_1.scl_io_num = I2C_1_SCL_GPIO;
    bus_config_1.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config_1.glitch_ignore_cnt = 7;
    bus_config_1.flags.enable_internal_pullup = true;

    err = i2c_new_master_bus(
        &bus_config_1, &bus_handle_1_);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize I2C bus 1: %s",
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C bus 1 handle: %p", bus_handle_1_);

    acc_ = std::make_unique<MPU6050>(
        bus_handle_1_, MPU6050_I2C_ADDRESS);

    err = acc_->init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize MPU6050: %s",
                 esp_err_to_name(err));
        acc_.reset();
    }
    else
    {
        acc_initialized_ = true;
    }

    temp_ = std::make_unique<TPiS1385>(
        bus_handle_0_, TPIS1385_I2C_ADDRESS);

    err = temp_->init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize TPiS1385: %s",
                 esp_err_to_name(err));
        temp_.reset();
    }
    else
    {
        temp_initialized_ = true;
    }

    return ESP_OK;
}

void Application::loop()
{
    while (true)
    {
        if (acc_initialized_ && acc_ != nullptr)
        {
            const esp_err_t err = acc_->read(acc_data_);

            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to read MPU6050: %s",
                         esp_err_to_name(err));
            }
            else
            {
                ESP_LOGI(TAG,
                         "[ACC] ACC: X=%.3f Y=%.3f Z=%.3f | DEVICE-TEMP=%.2f C",
                         acc_data_.acc_x,
                         acc_data_.acc_y,
                         acc_data_.acc_z,
                         acc_data_.temperature);
            }
        }

        if (temp_initialized_ && temp_ != nullptr)
        {
            const esp_err_t err = temp_->read(temp_data_);

            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to read TPiS1385: %s",
                         esp_err_to_name(err));
            }
            else
            {
                ESP_LOGI(TAG,
                         "[TEMP] SKIN-TEMP=%.2f C | DEVICE-TEMP: %.2f C",
                         temp_data_.skin_temperature,
                         temp_data_.ambient_temperature);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
