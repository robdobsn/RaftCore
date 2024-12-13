/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Named value provider
//
// Rob Dobson 2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <ArduinoWString.h>
class NamedValueProvider
{
public:
    /// @brief Get named value
    /// @param pModule Module name
    /// @param param Parameter name
    /// @param isValid (out) true if value is valid
    /// @return value
    virtual double getNamedValue(const char* pModule, const char* param, bool& isValid) const
    {
        isValid = false;
        return 0;
    }

    /// @brief Set named value
    /// @param pModule Module name
    /// @param param Parameter name
    /// @param value Value to set
    virtual bool setNamedValue(const char* pModule, const char* param, double value)
    {
        return false;
    }

    /// @brief Get named value string
    /// @param pModule Module name 
    /// @param valueName Value name
    /// @param isValid (out) true if value is valid
    /// @return value string
    virtual String getNamedString(const char* pModule, const char* valueName, bool& isValid) const
    {
        isValid = false;
        return "";
    }

    /// @brief Set named value string
    /// @param pModule Module name
    /// @param valueName Value name
    /// @param value Value to set
    /// @return true if set
    virtual bool setNamedString(const char* pModule, const char* valueName, const char* value)
    {
        return false;
    }

};
