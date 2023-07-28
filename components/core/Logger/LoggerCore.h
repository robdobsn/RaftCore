/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// LoggerCore
//
// Rob Dobson 2020-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdio.h>
#include "Logger.h"
#include <vector>
#include "LoggerBase.h"

class LoggerCore
{
public:
    LoggerCore();
    ~LoggerCore();
    void LOGGING_FUNCTION_DECORATOR log(esp_log_level_t level, const char *tag, const char* msg);
    void clearLoggers();
    void addLogger(LoggerBase* pLogger);

private:
    std::vector<LoggerBase*> _loggers;
};

extern LoggerCore loggerCore;

