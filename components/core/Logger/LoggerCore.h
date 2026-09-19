/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// LoggerCore
//
// Rob Dobson 2020-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include <atomic>
#include "Logger.h"
#include "LoggerBase.h"

class LoggerCore
{
public:
    LoggerCore();
    ~LoggerCore();
    void LOGGING_FUNCTION_DECORATOR log(esp_log_level_t level, const char *tag, const char* msg);
    void loop();
    void clearLoggers();
    void addLogger(LoggerBase* pLogger);
    std::vector<LoggerBase*> getLoggers();
    String getLoggersJSON(bool includeBraces);

private:
    // Loggers are held in a fixed array with an atomic count since log() can be called from any task
    // (and at any time, including while loggers are being added on the main task)
    // A logger is added by writing the slot and then incrementing the count
    // Loggers are never deleted while the system is running as another task may be using them in log()
    static const uint32_t MAX_LOGGERS = 8;
    LoggerBase* _loggers[MAX_LOGGERS] = {};
    std::atomic<uint32_t> _numLoggers{0};
};

extern LoggerCore loggerCore;

