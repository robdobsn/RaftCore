////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Class RaftJsonPrefixed
//
// Rob Dobson 2023
//
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include "RaftJsonIF.h"

class RaftJsonPrefixed : public RaftJsonIF
{
public:
    RaftJsonPrefixed(const RaftJsonIF& raftJsonIF, const char* pPrefix) :
        _raftJsonIF(raftJsonIF),
        _prefix(pPrefix ? pPrefix + String("/") : "")
    {
    }
    RaftJsonPrefixed(const RaftJsonIF& raftJsonIF, const String& prefix) :
        _raftJsonIF(raftJsonIF),
        _prefix(prefix + String("/"))
    {
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get string value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual String getString(const char *dataPath, const char *defaultValue) const override final
    {
        return _raftJsonIF.getString(getPrefixedDataPath(dataPath).c_str(), defaultValue);
    }
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get double value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual double getDouble(const char *dataPath, double defaultValue) const override final
    {
        return _raftJsonIF.getDouble(getPrefixedDataPath(dataPath).c_str(), defaultValue);
    }
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get int value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual int getInt(const char* pDataPath, int defaultValue) const override final
    {
        return _raftJsonIF.getInt(getPrefixedDataPath(pDataPath).c_str(), defaultValue);
    }
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get long value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found    
    virtual long getLong(const char *dataPath, long defaultValue) const override final
    {
        return _raftJsonIF.getLong(getPrefixedDataPath(dataPath).c_str(), defaultValue);
    }
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get boolean value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found    
    virtual bool getBool(const char *dataPath, bool defaultValue) const override final
    {
        return _raftJsonIF.getBool(getPrefixedDataPath(dataPath).c_str(), defaultValue);
    }
    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get array elements using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param strList a vector which is filled with the array elements
    /// @return true if the array was found
    virtual bool getArrayElems(const char *dataPath, std::vector<String>& strList) const override final
    {
        return _raftJsonIF.getArrayElems(getPrefixedDataPath(dataPath).c_str(), strList);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get array integers using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param strList a vector which is filled with the array integers
    /// @return true if the array was found
    /// @note This is a convenience function for the common case of an array of integers and assumes that
    ///       the array elements are all integers - any non-integer elements will be converted to 0
    virtual bool getArrayInts(const char *pDataPath, std::vector<int>& intList) const override final
    {
        return _raftJsonIF.getArrayInts(getPrefixedDataPath(pDataPath).c_str(), intList);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get keys of an object using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param keysVector a vector which is filled with the keys
    /// @return true if the object was found
    virtual bool getKeys(const char *pDataPath, std::vector<String>& keysVector) const override final
    {
        return _raftJsonIF.getKeys(getPrefixedDataPath(pDataPath).c_str(), keysVector);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Check if the member JSON document contains the key specified by the path
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @return true if the key was found
    virtual bool contains(const char *dataPath) const override final
    {
        return _raftJsonIF.contains(getPrefixedDataPath(dataPath).c_str());
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get type of element from a JSON document at the specified path
    /// @param pDataPath the path of the required object in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param arrayLen the length of the array if the element is an array
    /// @return the type of the element
    virtual RaftJsonType getType(const char* pDataPath, int &arrayLen) const override final
    {
        return _raftJsonIF.getType(getPrefixedDataPath(pDataPath).c_str(), arrayLen);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get JSON doc contents
    /// @return const char* : JSON doc contents
    virtual const char* getJsonDoc() const override final
    {
        return _raftJsonIF.getJsonDoc();
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get JSON doc end
    /// @return const char* : JSON doc end
    virtual const char* getJsonDocEnd() const override final
    {
        return _raftJsonIF.getJsonDocEnd();
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get as a string
    /// @return String
    virtual String toString() const override final
    {
        return _raftJsonIF.toString();
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get as a double
    /// @return double
    virtual double toDouble() const override final
    {
        return _raftJsonIF.toDouble();
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get as an int
    /// @return int
    virtual int toInt() const override final
    {
        return _raftJsonIF.toInt();
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get as a long
    /// @return long
    virtual long toLong() const override final
    {
        return _raftJsonIF.toLong();
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get as a boolean
    /// @return bool
    virtual bool toBool() const override final
    {
        return _raftJsonIF.toBool();
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get chained RaftJson object
    /// @return RaftJsonIF* : chained RaftJson object (may be null if there is no chaining)
    virtual const RaftJsonIF* getChainedRaftJson() const override final
    {
        return _raftJsonIF.getChainedRaftJson();
    }
    
private:
    // Get prefixed data path
    String getPrefixedDataPath(const char* pDataPath) const
    {
        if (_prefix.length() == 0)
            return pDataPath;
        return _prefix + pDataPath;
    }

private:
    // RaftJsonIF
    const RaftJsonIF& _raftJsonIF;

    // Prefix
    String _prefix;
};
