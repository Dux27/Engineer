#include "Application.hpp"
#include "MPU6050.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


static const char *TAG = "APP";

bool Application::init()
{
    ESP_LOGI(TAG, "Initializing application");

    if (!imu_.init())
    {
        ESP_LOGE(TAG, "Failed to initialize MPU6050");
        return false;
    }

    ESP_LOGI(TAG, "MPU6050 initialized successfully");
    return true;
}

void Application::loop()
{
    MPU6050::Data data{};

    while(true)
    {
        if (imu_.read(data))
        {
            ESP_LOGI(
                TAG,
                "ACC: X=%.3f Y=%.3f Z=%.3f | TEMP=%.3f",
                data.acc_x,
                data.acc_y,
                data.acc_z,
                data.temperature);
        }
        else
        {
            ESP_LOGW(TAG, "Failed to read MPU6050");
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
