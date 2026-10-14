#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_log.h>

class Application
{
public:
  bool init();
  void loop();

private:
  const char* TAG_ = "APP";
};
