#include <dlib/dnn.h>
#include <fcntl.h>
// #include <onnxruntime_cxx_api.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "JPEGManager.h"
#include "application.h"
#include "camera.h"
#include "common.h"
#include "detectors.h"
#include "face.h"
#include "math_utils.h"
#include "config.hpp"

using namespace funnyface;

int main()
{
    common::init_logger("a0.0.0");
    common::log_info("Num of cuda devices: %d", dlib::cuda::get_num_devices());

    if(!Config::getInstance("../config.yaml").loadConfiguration())
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
