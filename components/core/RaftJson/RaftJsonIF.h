/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftJsonIF - JSON parser interface
//
// Rob Dobson 2017-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include <functional>
#include "RaftArduino.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Callback type for JSON change - used by RaftJsonIF implementations that support 
///        changes to the JSON document
typedef std::function<void()> RaftJsonChangeCallbackType;

class RaftJsonIF
{
public:

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get string value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual String getString(const char* pDataPath, const char* defaultValue) const = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get double value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual double getDouble(const char* pDataPath, double defaultValue) const = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get long value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found    
    virtual long getLong(const char* pDataPath, long defaultValue) const = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get boolean value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found    
    virtual bool getBool(const char* pDataPath, bool defaultValue) const = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get array elements using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param strList a vector which is filled with the array elements
    /// @return true if the array was found
    virtual bool getArrayElems(const char *pDataPath, std::vector<String>& strList) const = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get keys of an object using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param keysVector a vector which is filled with the keys
    /// @return true if the object was found
    virtual bool getKeys(const char *pDataPath, std::vector<String>& keysVector) const = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Check if the member JSON document contains the key specified by the path
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @return true if the key was found
    virtual bool contains(const char* pDataPath) const = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Register a callback for JSON change - used by RaftJsonIF implementations that support
    ///        changes to the JSON document
    /// @param jsonChangeCallback the callback to be called when the JSON document changes
    virtual void registerChangeCallback(RaftJsonChangeCallbackType configChangeCallback)
    {
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set new contents for the JSON document
    /// @param configJSONStr the new JSON document
    /// @return true if the JSON document was successfully set
    /// @note This is used by RaftJsonIF implementations that support changes to the JSON document
    ///       Implementations that store to NVS or similar may persist the new JSON document
    virtual bool setNewContent(const char* pJsonDoc)
    {
        return false;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Locate an element in a JSON document using a path
    /// @param pPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @return the position of the element or nullptr if not found
    virtual const char* locateElementByPath(const char* pPath)
    {
        return nullptr;
    }

};
