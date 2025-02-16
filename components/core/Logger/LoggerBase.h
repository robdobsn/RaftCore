/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Base class for loggers
//
// Rob Dobson 2021-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdarg.h>
#include "Logger.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "RaftJsonIF.h"

class LoggerBase
{
public:
    /// @brief Constructor
    /// @param config Configuration for the logger
    LoggerBase(const RaftJsonIF& config)
    {
        _loggerType = config.getString("type", "");
        _level = convStrToLogLevel(config.getString("level", "").c_str());
        _isPaused = config.getBool("pause", false);
    }

    /// @brief Destructor
    virtual ~LoggerBase()
    {
    }

    /// @brief Logging function
    /// @param level Log level
    /// @param tag Tag
    /// @param msg Message
    virtual void LOGGING_FUNCTION_DECORATOR log(esp_log_level_t level, const char *tag, const char* msg) = 0;

    /// @brief Set logging level
    /// @param level Log level
    virtual void setLevel(esp_log_level_t level)
    {
        _level = level;
    }

    /// @brief Loop (called from main loop)
    virtual void loop()
    {
    }

    /// @brief Get the logger type
    /// @return the logger type
    const char* getLoggerType() const
    {
        return _loggerType.c_str();
    }

    /// @brief Check if logging is paused
    /// @return true if paused
    virtual bool isPaused() const
    {
        return _isPaused;
    }

    /// @brief Set logging pause state
    /// @param isPaused true to pause logging
    virtual void setPaused(bool isPaused)
    {
        _isPaused = isPaused;
    }

    /// @brief Get JSON describing the logger
    /// @return JSON string
    String getLoggerJSON() const
    {
        String loggerJSON = "{";
        loggerJSON += "\"type\":\"";
        loggerJSON += _loggerType;
        loggerJSON += "\",\"level\":\"";
        loggerJSON += getLevelStr(_level);
        loggerJSON += "\",\"paused\":";
        loggerJSON += _isPaused ? 1 : 0;
        loggerJSON += "}";
        return loggerJSON;
    }

    /// @brief Get logging level as a string
    /// @param level Log level
    /// @return const char* log level string
    static const char* getLevelStr(esp_log_level_t level)
    {
        switch(level)
        {
        case ESP_LOG_NONE:
            return "NONE";
        case ESP_LOG_ERROR:
            return "ERROR";
        case ESP_LOG_WARN:
            return "WARN";
        case ESP_LOG_INFO:
            return "INFO";
        case ESP_LOG_DEBUG:
            return "DEBUG";
        case ESP_LOG_VERBOSE:
            return "VERBOSE";
        default:
            return "UNKNOWN";
        }
    }

protected:
    String _loggerType;
    esp_log_level_t _level = ESP_LOG_INFO;
    bool _isPaused = false;
    esp_log_level_t convStrToLogLevel(const char* pStr)
    {
        if ((pStr == nullptr) || (pStr[0] == '\0'))
            return ESP_LOG_INFO;
        switch (toupper(pStr[0]))
        {
        case 'V':
            return ESP_LOG_VERBOSE;
        case 'D':
            return ESP_LOG_DEBUG;
        case 'I':
            return ESP_LOG_INFO;
        case 'W':
            return ESP_LOG_WARN;
        case 'E':
            return ESP_LOG_ERROR;
        default:
            return ESP_LOG_NONE;
        }
    }
};
