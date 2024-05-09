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
#if __has_include("RaftArduino.h")
#include "RaftArduino.h"
#else
#include "WString.h"
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Callback type for JSON change - used by RaftJsonIF implementations that support 
///        changes to the JSON document
typedef std::function<void()> RaftJsonChangeCallbackType;

class RaftJsonIF
{
public:
    class BaseIterator;
    class ArrayIterator;
    class ObjectIterator;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get string value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param pDefaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual String getString(const char* pDataPath, const char* pDefaultValue) const = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get double value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual double getDouble(const char* pDataPath, double defaultValue) const = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get int value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found    
    virtual int getInt(const char* pDataPath, int defaultValue) const = 0;

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
    /// JSON element type codes
    // Element type
    typedef enum
    {
        RAFT_JSON_UNDEFINED = 0,
        RAFT_JSON_OBJECT = 1,
        RAFT_JSON_ARRAY = 2,
        RAFT_JSON_STRING = 3,
        RAFT_JSON_BOOLEAN = 4,
        RAFT_JSON_NUMBER = 5,
        RAFT_JSON_NULL = 6
    } RaftJsonType;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get type of element from a JSON document at the specified path
    /// @param pDataPath the path of the required object in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param arrayLen the length of the array if the element is an array
    /// @return the type of the element
    virtual RaftJsonType getType(const char* pDataPath, int &arrayLen) const = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get JSON doc contents
    /// @return const char* : JSON doc contents
    virtual const char* getJsonDoc() const = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get JSON doc end
    /// @return const char* : JSON doc end
    virtual const char* getJsonDocEnd() const = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get as a string
    /// @return String
    virtual String toString() const = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get as a double
    /// @return double
    virtual double toDouble() const = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get as an int
    /// @return int
    virtual int toInt() const = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get as a long
    /// @return long
    virtual long toLong() const = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get as a boolean
    /// @return bool
    virtual bool toBool() const = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get JSON doc (alternative to getJsonDoc)
    /// @return const char* : JSON doc contents
    const char* c_str() const { return getJsonDoc(); }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get chained RaftJson object
    /// @return RaftJsonIF* : chained RaftJson object (may be null if there is no chaining)
    virtual const RaftJsonIF* getChainedRaftJson() const
    {
        return nullptr;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set chained RaftJson object
    /// @param pChainedRaftJson chained RaftJson object (may be null if chaining is to be disabled)
    virtual void setChainedRaftJson(const RaftJsonIF* pChainedRaftJson)
    {
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Register a callback for JSON change - used by RaftJsonIF implementations that support
    ///        changes to the JSON document
    /// @param jsonChangeCallback the callback to be called when the JSON document changes
    virtual void registerChangeCallback(RaftJsonChangeCallbackType configChangeCallback)
    {
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set new contents for the JSON document
    /// @param pJsonDoc the new JSON document
    /// @return true if the JSON document was successfully set
    /// @note This is used by RaftJsonIF implementations that support changes to the JSON document
    ///       Implementations that store to NVS or similar may persist the new JSON document
    virtual bool setJsonDoc(const char* pJsonDoc)
    {
        return false;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Locate an element in a JSON document using a path
    /// @param pPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @return the position of the element or nullptr if not found
    virtual const char* locateElementByPath(const char* pPath) const
    {
        return nullptr;
    }
};
