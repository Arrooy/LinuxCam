#ifndef COMMON_H
#define COMMON_H

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <vector>
#include <memory>
// Macro to clear a buffer
#define CLEAR(x) memset(&(x), 0, sizeof(x))


// I did replace all static for inline to remove warnings. Dont know if its good or no...
namespace linuxface
{
namespace common
{
inline bool file_exists(const std::string& port)
{
    struct stat sb;
    return stat(port.c_str(), &sb) == 0;
}

template <typename T>
inline const T& clamp(const T& v, const T& lo, const T& hi)
{
    return (v < lo) ? lo : (hi < v) ? hi : v;
}


// Helper function to format the size into human-readable format
inline const char* format_size(unsigned long size)
{
    static char buffer[64];

    // If size is smaller than 1 KB, print in bytes
    if (size < 1024)
    {
        if (snprintf(buffer, sizeof(buffer), "%lu bytes", size) == -1)
        {
            return "Error formatting size";
        }
    }
    // If size is smaller than 1 MB, print in KB
    else if (size < 1024 * 1024)
    {
        if (snprintf(buffer, sizeof(buffer), "%.2f KB", size / 1024.0) == -1)
        {
            return "Error formatting size";
        }
    }
    // If size is smaller than 1 GB, print in MB
    else if (size < 1024 * 1024 * 1024)
    {
        if (snprintf(buffer, sizeof(buffer), "%.2f MB", size / (1024.0 * 1024)) == -1)
        {
            return "Error formatting size";
        }
    }
    // If size is 1 GB or larger, print in GB
    else
    {
        if (snprintf(buffer, sizeof(buffer), "%.2f GB", size / (1024.0 * 1024 * 1024)) == -1)
        {
            return "Error formatting size";
        }
    }

    return buffer;
}

enum class LogLevel
{
    INFO,
    WARN,
    ERROR
};

// This is a function so we can share same static variable
inline int& getLogFd()
{
    static int log_fd = -1;
    return log_fd;
}

inline const char* log_level_str(LogLevel level)
{
    switch (level)
    {
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARN:
            return "WARN";
        case LogLevel::ERROR:
            return "ERROR";
        default:
            return "LOG";
    }
}

inline const char* log_color(LogLevel level)
{
    switch (level)
    {
        case LogLevel::INFO:
            return "\033[32m"; // Green
        case LogLevel::WARN:
            return "\033[33m"; // Yellow
        case LogLevel::ERROR:
            return "\033[31m"; // Red
        default:
            return "";
    }
}

inline const char* log_color_reset()
{
    return "\033[0m";
}

inline void init_logger(const char* prefix, bool saveLogToFile = false)
{
    if (getLogFd() != -1)
    {
        close(getLogFd());
    }

    // Get UNIX timestamp (seconds since epoch)
    time_t now = time(nullptr);

    // Format: prefix-<timestamp>.log
    char* log_filename = nullptr;
    if (asprintf(&log_filename, "%ld-%s.log", now, prefix) == -1 || log_filename == nullptr)
    {
        fprintf(stderr, "Failed to generate log file name\n");
        std::exit(EXIT_FAILURE);
    }

    if (saveLogToFile)
    {
        auto& log_fd = getLogFd();
        log_fd = open(log_filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (log_fd == -1)
        {
            fprintf(stderr, "Failed to open log file: %s\n", strerror(errno));
            std::exit(EXIT_FAILURE);
        }
    }

    fprintf(stdout, "Logging to file: %s %s\n", log_filename, saveLogToFile ? "enabled" : "disabled");
    fflush(stdout);

    free(log_filename);
}

inline void log_to_file(const char* msg)
{
    if (getLogFd() != -1)
    {
        const ssize_t msgLen = strlen(msg);
        if (write(getLogFd(), msg, msgLen) != msgLen)
        {
            fprintf(stderr, "Failed to write to log file: %s\n", strerror(errno));
            std::exit(EXIT_FAILURE);
        }
    }
}

inline void log_message(LogLevel level, const char* format, ...)
{
    // Timestamp
    time_t now = time(nullptr);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));

    // Format user message
    char* user_msg = nullptr;
    va_list args;
    va_start(args, format);
    if (vasprintf(&user_msg, format, args) == -1)
    {
        fprintf(stderr, "log_message: vasprintf failed\n");
        std::exit(EXIT_FAILURE);
    }
    va_end(args);

    if (!user_msg)
    {
        fprintf(stderr, "log_message: vasprintf failed\n");
        std::exit(EXIT_FAILURE);
    }

    // Final full log message
    char* full_msg = nullptr;
    if (asprintf(&full_msg, "[%s] [%s] %s\n", time_buf, log_level_str(level), user_msg) == -1)
    {
        free(user_msg);
        fprintf(stderr, "log_message: asprintf failed\n");
        std::exit(EXIT_FAILURE);
    }

    if (!full_msg)
    {
        fprintf(stderr, "log_message: asprintf failed\n");
        free(user_msg);
        std::exit(EXIT_FAILURE);
    }

    // Print to stdout with color
    fprintf(stdout, "%s%s%s", log_color(level), full_msg, log_color_reset());
    fflush(stdout);

    // Log to file without color
    log_to_file(full_msg);

    free(user_msg);
    free(full_msg);
}

inline void log_vformatted(LogLevel level, const char* format, va_list args)
{
    char* msg = nullptr;
    if (vasprintf(&msg, format, args) == -1)
    {
        log_message(level, "log_vformatted: vasprintf failed");
    }
    if (msg)
    {
        log_message(level, "%s", msg);
        free(msg);
    }
    else
    {
        log_message(level, "log_vformatted: vasprintf failed");
    }
}

inline void log_info(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    log_vformatted(LogLevel::INFO, format, args);
    va_end(args);
}

inline void log_error(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    log_vformatted(LogLevel::ERROR, format, args);
    va_end(args);
}

inline void log_warn(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    log_vformatted(LogLevel::WARN, format, args);
    va_end(args);
}

inline void errno_log(const char* s)
{
    log_error("%s error %d, %s", s, errno, std::strerror(errno));
}



inline bool long_write(int fd, const void* buf, size_t size)
{

    // Write the buff data
    size_t written {0u};
    const char* ptr = static_cast<const char*>(buf);
    while (written < size)
    {
        ssize_t result = write(fd, ptr + written, size - written);
        if (result <= 0)
        {
            log_error("common::long_write - Write buf data failed. Written %zd bytes", written);
            return false;
        }
         written += static_cast<size_t>(result);
    }
    return true;
}

template<typename T>
std::vector<std::string> getKeysFromMap(const std::unordered_map<std::string, std::shared_ptr<T>>& map)
{
    std::vector<std::string> keys;
    for (const auto& pair : map)
    {
        keys.push_back(pair.first);
    }

    // Sort keys alphabetically
    std::sort(keys.begin(), keys.end());
    return keys;
}

// Interpolate between two T
template<typename T>
T lerp(T a, T b, T t) {
    return a + t * (b - a);
}

} // namespace common
} // namespace linuxface

#endif // COMMON_H
