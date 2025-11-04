#ifndef COMMON_H
#define COMMON_H

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

// Macro to clear a buffer
#define CLEAR(x) memset(&(x), 0, sizeof(x))

// TODO: rework this helper file.
namespace linuxface::common
{
// Global debug flag - set during config initialization
inline bool& getDebugEnabled()
{
    static bool debugEnabled = false;
    return debugEnabled;
}

inline void setDebugEnabled(bool enabled)
{
    getDebugEnabled() = enabled;
}
inline bool fileExists(const std::string& port)
{
    struct stat sb{};
    return stat(port.c_str(), &sb) == 0;
}

template <typename T>
inline const T& clamp(const T& v, const T& lo, const T& hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (hi < v)
    {
        return hi;
    }
    return v;
}

// Helper function to format the size into human-readable format
inline const char* formatSize(unsigned long size)
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
    else if (size < static_cast<unsigned long>(1024) * 1024)
    {
        if (snprintf(buffer, sizeof(buffer), "%.2f KB", static_cast<double>(size) / 1024.0) == -1)
        {
            return "Error formatting size";
        }
    }
    // If size is smaller than 1 GB, print in MB
    else if (size < static_cast<unsigned long>(1024) * 1024 * 1024)
    {
        if (snprintf(buffer, sizeof(buffer), "%.2f MB", static_cast<double>(size) / (1024.0 * 1024)) == -1)
        {
            return "Error formatting size";
        }
    }
    // If size is 1 GB or larger, print in GB
    else
    {
        if (snprintf(buffer, sizeof(buffer), "%.2f GB", static_cast<double>(size) / (1024.0 * 1024 * 1024)) == -1)
        {
            return "Error formatting size";
        }
    }

    return buffer;
}

enum class LogLevel
{
    DEBUG,
    INFO,
    WARN,
    ERROR
};

// This is a function so we can share same static variable
inline int& getLogFd()
{
    static int logFd = -1;
    return logFd;
}

inline const char* logLevelStr(LogLevel level)
{
    switch (level)
    {
        case LogLevel::DEBUG:
            return "DEBUG";
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

inline const char* logColor(LogLevel level)
{
    switch (level)
    {
        case LogLevel::DEBUG:
            return "\033[36m"; // Cyan
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

inline const char* logColorReset()
{
    return "\033[0m";
}

inline void initLogger(const char* prefix, bool saveLogToFile = false)
{
    if (getLogFd() != -1)
    {
        close(getLogFd());
    }

    // Get UNIX timestamp (seconds since epoch)
    const time_t now = time(nullptr);

    // Format: prefix-<timestamp>.log
    char* logFilename = nullptr;
    if (asprintf(&logFilename, "%ld-%s.log", now, prefix) == -1 || logFilename == nullptr)
    {
        fprintf(stderr, "Failed to generate log file name\n");
        std::exit(EXIT_FAILURE);
    }

    if (saveLogToFile)
    {
        auto& logFd = getLogFd();
        logFd = open(logFilename, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (logFd == -1)
        {
            fprintf(stderr, "Failed to open log file: %s\n", strerror(errno));
            std::exit(EXIT_FAILURE);
        }
    }

    fprintf(stdout, "Logging to file: %s %s\n", logFilename, saveLogToFile ? "enabled" : "disabled");
    fflush(stdout);

    free(logFilename);
}

inline void logToFile(const char* msg)
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

inline void logMessage(LogLevel level, const char* format, ...)
{
    // Timestamp with milliseconds
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    time_t now_sec = tv.tv_sec;
    int msec = static_cast<int>(tv.tv_usec / 1000);
    char timeBuf[64];
    struct tm tm_info;
    // Use localtime_r for thread-safety
    localtime_r(&now_sec, &tm_info);
    if (strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tm_info) == 0)
    {
        // Fallback in unlikely event of formatting failure
        snprintf(timeBuf, sizeof(timeBuf), "%ld", now_sec);
    }
    // Append milliseconds
    char timeBufMs[80];
    snprintf(timeBufMs, sizeof(timeBufMs), "%s.%03d", timeBuf, msec);

    // Format user message
    char* userMsg = nullptr;
    va_list args;
    va_start(args, format);
    if (vasprintf(&userMsg, format, args) == -1)
    {
        fprintf(stderr, "log_message: vasprintf failed\n");
        std::exit(EXIT_FAILURE);
    }
    va_end(args);

    if (userMsg == nullptr)
    {
        fprintf(stderr, "log_message: vasprintf failed\n");
        std::exit(EXIT_FAILURE);
    }

    // Final full log message
    char* fullMsg = nullptr;
    if (asprintf(&fullMsg, "[%s] [%s] %s\n", timeBufMs, logLevelStr(level), userMsg) == -1)
    {
        free(userMsg);
        fprintf(stderr, "log_message: asprintf failed\n");
        std::exit(EXIT_FAILURE);
    }

    if (fullMsg == nullptr)
    {
        fprintf(stderr, "log_message: asprintf failed\n");
        free(userMsg);
        std::exit(EXIT_FAILURE);
    }

    // Print to stdout with color
    fprintf(stdout, "%s%s%s", logColor(level), fullMsg, logColorReset());
    fflush(stdout);

    // Log to file without color
    logToFile(fullMsg);

    free(userMsg);
    free(fullMsg);
}

inline void logVformatted(LogLevel level, const char* format, va_list args)
{
    char* msg = nullptr;
    if (vasprintf(&msg, format, args) == -1)
    {
        logMessage(level, "log_vformatted: vasprintf failed");
    }
    if (msg != nullptr)
    {
        logMessage(level, "%s", msg);
        free(msg);
    }
    else
    {
        logMessage(level, "log_vformatted: vasprintf failed");
    }
}


inline void logDebug(const char* format, ...)
{
    if (!getDebugEnabled())
    {
        return; // Skip logging when debug is disabled
    }
    va_list args;
    va_start(args, format);
    logVformatted(LogLevel::DEBUG, format, args);
    va_end(args);
}

inline void logInfo(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    logVformatted(LogLevel::INFO, format, args);
    va_end(args);
}

inline void logError(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    logVformatted(LogLevel::ERROR, format, args);
    va_end(args);
}

inline void logWarn(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    logVformatted(LogLevel::WARN, format, args);
    va_end(args);
}

inline void errnoLog(const char* s)
{
    logError("%s error %d, %s", s, errno, std::strerror(errno));
}

inline bool longWrite(int fd, const void* buf, size_t size)
{
    // Write the buff data
    size_t written{0u};
    const char* ptr = static_cast<const char*>(buf);
    while (written < size)
    {
        const ssize_t result = write(fd, ptr + written, size - written);
        if (result <= 0)
        {
            logError("common::longWrite - Write buf data failed. Written %zd bytes", written);
            return false;
        }
        written += static_cast<size_t>(result);
    }
    return true;
}

template <typename T>
std::vector<std::string> getKeysFromMap(const std::unordered_map<std::string, std::shared_ptr<T>>& map)
{
    std::vector<std::string> keys;
    keys.reserve(map.size());
    for (const auto& pair : map)
    {
        keys.push_back(pair.first);
    }

    // Sort keys alphabetically
    std::sort(keys.begin(), keys.end());
    return keys;
}

// Interpolate between two T
template <typename T>
T lerp(T a, T b, T t)
{
    return a + t * (b - a);
}

// Check if a graphical display is available (X11 or Wayland)
inline bool isGraphicalDisplayAvailable()
{
    // Check for X11 display
    const char* display = std::getenv("DISPLAY");
    if (display != nullptr && display[0] != '\0')
    {
        return true;
    }

    // Check for Wayland display
    const char* waylandDisplay = std::getenv("WAYLAND_DISPLAY");
    if (waylandDisplay != nullptr && waylandDisplay[0] != '\0')
    {
        return true;
    }

    return false;
}

// Get current memory usage in MB
inline double getMemoryUsageMB()
{
    std::ifstream file("/proc/self/status");
    std::string line;
    
    while (std::getline(file, line))
    {
        if (line.substr(0, 6) == "VmRSS:")
        {
            std::istringstream iss(line);
            std::string label, value, unit;
            iss >> label >> value >> unit;
            
            double kb = std::stod(value);
            return kb / 1024.0; // Convert KB to MB
        }
    }
    return -1.0; // Error case
}

// Log memory usage with a custom message
inline void logMemoryUsage(const char* context)
{
    double memMB = getMemoryUsageMB();
    if (memMB > 0)
    {
        logInfo("Memory Usage [%s]: %.1f MB", context, memMB);
    }
}

} // namespace linuxface::common

#endif // COMMON_H
