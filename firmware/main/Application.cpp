#include "Application.hpp"
#include "MPU6050.hpp"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


static const char *TAG = "APP";

esp_err_t Application::init()
{
    ESP_LOGI(TAG, "Initializing application");

    esp_err_t err = imu_.init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize MPU6050");
        return err;
    }

    ESP_LOGI(TAG, "MPU6050 initialized successfully");
    return err;
}

void Application::loop()
{
    while(true)
    {
        esp_err_t err = imu_.read(imu_data_);
        if (err == ESP_OK)
        {
            ESP_LOGI(
                TAG,
                "ACC: X=%.3f Y=%.3f Z=%.3f | TEMP=%.3f",
                imu_data_.acc_x,
                imu_data_.acc_y,
                imu_data_.acc_z,
                imu_data_.temperature);
        }
        else
        {
            ESP_LOGW(TAG, "Failed to read MPU6050");
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
