/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// LoggerCore
//
// Rob Dobson 2020-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
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
    std::vector<LoggerBase*> _loggers;
};

extern LoggerCore loggerCore;

