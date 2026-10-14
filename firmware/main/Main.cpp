#include "Application.hpp"

extern "C" void app_main()
{
    Application app;
    app.init();
    app.loop();
}
