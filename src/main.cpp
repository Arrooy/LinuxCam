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
        linuxface::common::init_logger("a0.0.0");
        std::string configFileLocation{"../config.yaml"};
        if (argc > 1)
        {
            linuxface::common::log_info("Config file: %s", argv[1]);
            configFileLocation = std::string(argv[1]);
        }
        if (!Config::getInstance(configFileLocation.c_str()).loadConfiguration())
        {
            linuxface::common::log_error("Failed to load configuration");
            return -1;
        }

        Application app;

        // Initialize the application
        if (!app.initialize())
        {
            linuxface::common::log_error("Failed to initialize application");
            return -1;
        }

        // Run the main loop
        app.run();

        // Cleanup happens automatically via destructors
        linuxface::common::log_info("Application finished successfully");
        return 0;
    }
    catch (const std::exception& e)
    {
        linuxface::common::log_error("Unhandled exception: %s", e.what());
        return -1;
    }
    catch (...)
    {
        linuxface::common::log_error("Unknown exception occurred");
        return -1;
    }
}
