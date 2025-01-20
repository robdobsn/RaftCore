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
#include "SpiramAwareAllocator.h"

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
    std::vector<char, SpiramAwareAllocator<char>> buf(MAX_VSPRINTF_BUFFER_SIZE);
    vsnprintf(buf.data(), buf.size(), format, args);
    loggerCore.log(level, tag, buf.data());
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
    printf(msg);

    // Log to all loggers
    for (LoggerBase* pLogger : _loggers)
    {
        // printf("LoggerCore::log type %s msg %s", pLogger->getLoggerType(), msg);
        pLogger->log(level, tag, msg);
    }
}
