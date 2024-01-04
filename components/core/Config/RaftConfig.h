

#pragma once

#include <vector>
#include "RaftArduino.h"

class RaftConfig : public RaftJsonIF
{
public:
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // @brief Get string value using the member variable JSON document
    // @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    // @param defaultValue the default value to return if the variable is not found
    // @return the value of the variable or the default value if not found
    virtual String getString(const char* pDataPath, const char* defaultValue) const override
    {
        return getString(_pSourceStr, pDataPath, defaultValue);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get double value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual double getDouble(const char* pDataPath, double defaultValue) const override
    {
        return getDouble(_pSourceStr, pDataPath, defaultValue);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get long value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual long getLong(const char* pDataPath, long defaultValue) const override
    {
        return getLong(_pSourceStr, pDataPath, defaultValue);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get boolean value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual bool getBool(const char* pDataPath, bool defaultValue) const override
    {
        return getBool(_pSourceStr, pDataPath, defaultValue);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get array elements using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param strList a vector which is filled with the array elements
    /// @return true if the array was found
    virtual bool getArrayElems(const char *pDataPath, std::vector<String>& strList) const override
    {
        return getArrayElems(_pSourceStr, pDataPath, strList);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get keys of an object using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param keysVector a vector which is filled with the keys
    /// @return true if the object was found
    virtual bool getKeys(const char *pDataPath, std::vector<String>& keysVector) const override
    {
        return getKeys(_pSourceStr, pDataPath, keysVector);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Check if the member JSON document contains the key specified by the path
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @return true if the key was found
    virtual bool contains(const char* pDataPath) const override
    {
        int arrayLen = 0;
        RaftJsonType elemType = getType(_pSourceStr, pDataPath, arrayLen);
        return elemType != RAFT_JSON_UNDEFINED;
    }

private:
    // Maintain a list of configs that are used hierarchically
    class ConfigData
    {
    public:
        ConfigData(const char* pConfigStr, const char* pConfigPrefix) :
            _pConfigStr(pConfigStr),
            _pConfigPrefix(pConfigPrefix)
        {
        }
        const char* _pConfigStr;
        const char* _pConfigPrefix;
    };
    std::vector<ConfigData> _configDataList;
    
};
