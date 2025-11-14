#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>

#include "LinuxFace/application.h"
#include "config.hpp"

using linuxface::Application;
using linuxface::Config;

int main(int argc, char* argv[])
{
    try
    {
        linuxface::common::initLogger("b0.0.0", true);
        std::string configFileLocation{"../config.yaml"};
        if (argc > 1)
        {
            linuxface::common::logInfo("Config file: %s", argv[1]);
            configFileLocation = std::string(argv[1]);
        }
        if (!Config::getInstance(configFileLocation.c_str()).loadConfiguration())
        {
            linuxface::common::logError("Failed to load configuration");
            return -1;
        }

        auto app = std::make_shared<Application>();

        // Initialize the application
        if (!app->initialize())
        {
            linuxface::common::logError("Failed to initialize application");
            return -1;
        }

        // Run the main loop
        app->run();

        // Cleanup happens automatically via destructors
        linuxface::common::logInfo("Application finished successfully");
        return 0;
    }
    catch (const std::exception& e)
    {
        linuxface::common::logError("Unhandled exception: %s", e.what());
        return -1;
    }
    catch (...)
    {
        linuxface::common::logError("Unknown exception occurred");
        return -1;
    }
}
