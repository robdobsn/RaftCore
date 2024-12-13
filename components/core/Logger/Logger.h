/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Log
//
// Rob Dobson 2016-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifdef ESP_PLATFORM

#include "esp_log.h"
#include "esp_attr.h"

#ifndef LOGGING_FUNCTION_DECORATOR
#define LOGGING_FUNCTION_DECORATOR
// #define LOGGING_FUNCTION_DECORATOR IRAM_ATTR
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern LOGGING_FUNCTION_DECORATOR void loggerLog(esp_log_level_t level, const char *tag, const char *format, ...);

#ifdef __cplusplus
}
#endif

#define LOGCORE_FORMAT(letter, format) #letter " (%d) %s: " format "\n"

#define LOG_E( tag, format, ... ) loggerLog(ESP_LOG_ERROR, tag, LOGCORE_FORMAT(E, format), esp_log_timestamp(), tag, ##__VA_ARGS__);
#define LOG_W( tag, format, ... ) loggerLog(ESP_LOG_WARN, tag, LOGCORE_FORMAT(W, format), esp_log_timestamp(), tag, ##__VA_ARGS__);
#define LOG_I( tag, format, ... ) loggerLog(ESP_LOG_INFO, tag, LOGCORE_FORMAT(I, format), esp_log_timestamp(), tag, ##__VA_ARGS__);
#define LOG_D( tag, format, ... ) loggerLog(ESP_LOG_DEBUG, tag, LOGCORE_FORMAT(D, format), esp_log_timestamp(), tag, ##__VA_ARGS__);
#define LOG_V( tag, format, ... ) loggerLog(ESP_LOG_VERBOSE, tag, LOGCORE_FORMAT(V, format), esp_log_timestamp(), tag, ##__VA_ARGS__);

#else

#include <stdio.h>
#include <time.h>

// Function to get the current time in milliseconds
static inline long get_current_time_ms()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts); // Use CLOCK_REALTIME for system time
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

#define LOG_E(tag, format, ...) fprintf(stdout, "E (%ld) %s: " format "\n", get_current_time_ms(), tag, ##__VA_ARGS__)
#define LOG_W(tag, format, ...) fprintf(stdout, "W (%ld) %s: " format "\n", get_current_time_ms(), tag, ##__VA_ARGS__)
#define LOG_I(tag, format, ...) fprintf(stdout, "I (%ld) %s: " format "\n", get_current_time_ms(), tag, ##__VA_ARGS__)
#define LOG_D(tag, format, ...) fprintf(stdout, "D (%ld) %s: " format "\n", get_current_time_ms(), tag, ##__VA_ARGS__)
#define LOG_V(tag, format, ...) fprintf(stdout, "V (%ld) %s: " format "\n", get_current_time_ms(), tag, ##__VA_ARGS__)

#endif
