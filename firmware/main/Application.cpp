#include "Application.hpp"

bool Application::init()
{
  ESP_LOGI(TAG_, "Application initialized.");
  return true;
}

void Application::loop()
{
  while(true)
  {
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG_, "Application loop...");
  }
}
