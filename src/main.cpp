#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>

#include "LinuxFace/application.h"
#include "config.hpp"

using namespace linuxface;

int main(int argc, char* argv[])
{
    common::init_logger("a0.0.0");
    std::string configFileLocation{"../config.yaml"};
    if (argc > 1)
    {
        common::log_info("Config file: %s", argv[1]);
        configFileLocation = std::string(argv[1]);
    }
    if (!Config::getInstance(configFileLocation.c_str()).loadConfiguration())
    {
        common::log_error("Failed to load configuration");
        return -1;
    }

    Application app;

    // Initialize the application
    if (!app.initialize())
    {
        common::log_error("Failed to initialize application");
        return -1;
    }

    // Run the main loop
    app.run();

    // Cleanup happens automatically via destructors
    common::log_info("Application finished successfully");
    return 0;
}
