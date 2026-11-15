#include "Application.hpp"

extern "C" void app_main()
{
    Application app;
    if (app.init() != ESP_OK)
        return;
    app.loop();
}
