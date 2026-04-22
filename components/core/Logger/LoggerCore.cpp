/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// LoggerCore
//
// Rob Dobson 2021-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "LoggerCore.h"
#include "RaftArduino.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
// Only use the direct USB JTAG path when USB JTAG is the PRIMARY console.
// When USB JTAG is a SECONDARY console, the PRIMARY (typically UART) would
// lose output if we bypass printf, so fall back to printf in that case.
#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#define RAFT_LOGGER_USE_USB_JTAG_DIRECT 1
// Timeout for usb_serial_jtag_write_bytes when the ring buffer is full.
// Trade-off:
//   small value (e.g. 10): drops log bytes if host isn't draining or USB
//                          is unplugged, never blocks the calling task.
//   large value / portMAX_DELAY: blocks the calling task until space is
//                                available (back-pressure); can hang tasks
//                                indefinitely if USB unplugged.
// Override by defining RAFT_LOGGER_USB_JTAG_WRITE_TIMEOUT_MS in the build
// (e.g. via target_compile_definitions or CFLAGS).
#ifndef RAFT_LOGGER_USB_JTAG_WRITE_TIMEOUT_MS
#define RAFT_LOGGER_USB_JTAG_WRITE_TIMEOUT_MS 10
#endif
#endif
#endif

LoggerCore loggerCore;

/// @brief Log to all loggers
/// @param level log level
/// @param tag prefix tag
/// @param format format string (printf style)
/// @param ...
extern "C" void LOGGING_FUNCTION_DECORATOR loggerLog(esp_log_level_t level, const char *tag, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    static const uint32_t MAX_VSPRINTF_BUFFER_SIZE = 1024;
    char buf[MAX_VSPRINTF_BUFFER_SIZE];
    vsnprintf(buf, MAX_VSPRINTF_BUFFER_SIZE, format, args);
    loggerCore.log(level, tag, buf);
    va_end(args);
}

/// @brief Constructor
LoggerCore::LoggerCore()
{
}

/// @brief Destructor
LoggerCore::~LoggerCore()
{
    for (LoggerBase* pLogger : _loggers)
    {
        delete pLogger;
    }
}

/// @brief Loop
void LoggerCore::loop()
{
    for (LoggerBase* pLogger : _loggers)
    {
        pLogger->loop();
    }
}

/// @brief Clear loggers
void LoggerCore::clearLoggers()
{
    _loggers.clear();
}

/// @brief Add a logger
void LoggerCore::addLogger(LoggerBase* pLogger)
{
    _loggers.push_back(pLogger);
}

/// @brief Get loggers
/// @return vector of loggers
std::vector<LoggerBase*> LoggerCore::getLoggers()
{
    return _loggers;
}

/// @brief Get loggers as JSON
/// @param includeBraces include braces in JSON
/// @return JSON string
String LoggerCore::getLoggersJSON(bool includeBraces)
{
    String loggersJSON;
    for (LoggerBase* pLogger : _loggers)
    {
        if (!loggersJSON.isEmpty())
            loggersJSON += ",";
        loggersJSON += pLogger->getLoggerJSON();
    }
    loggersJSON = "loggers:[" + loggersJSON + "]";
    return includeBraces ? "{" + loggersJSON + "}" : loggersJSON;
}

/// @brief Log
/// @param level log level
/// @param tag prefix tag
/// @param msg message
void LOGGING_FUNCTION_DECORATOR LoggerCore::log(esp_log_level_t level, const char *tag, const char *msg)
{
#ifdef RAFT_LOGGER_USE_USB_JTAG_DIRECT
    // Bypass newlib stdio + VFS and write directly to the USB JTAG ring-buffer
    // driver. The VFS/stdio path flushes one USB packet per newline which caps
    // throughput at ~67 KB/s. Direct writes let the driver coalesce data and
    // reach the ~200 KB/s USB FS ceiling.
    // Falls back gracefully if the driver isn't installed yet (returns -1,
    // which we ignore — early-boot logs may be lost but that is acceptable).
    usb_serial_jtag_write_bytes(msg, strlen(msg),
                                pdMS_TO_TICKS(RAFT_LOGGER_USB_JTAG_WRITE_TIMEOUT_MS));
#else
    printf("%s", msg);
#endif

    // Log to all loggers
    for (LoggerBase* pLogger : _loggers)
    {
        // printf("LoggerCore::log type %s msg %s", pLogger->getLoggerType(), msg);
        pLogger->log(level, tag, msg);
    }
}
