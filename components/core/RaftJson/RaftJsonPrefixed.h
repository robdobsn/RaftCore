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

    // Get functions
    virtual String getString(const char *dataPath, const char *defaultValue) const override final
    {
        return _raftJsonIF.getString(getPrefixedDataPath(dataPath).c_str(), defaultValue);
    }
    virtual long getLong(const char *dataPath, long defaultValue) const override final
    {
        return _raftJsonIF.getLong(getPrefixedDataPath(dataPath).c_str(), defaultValue);
    }
    virtual bool getBool(const char *dataPath, bool defaultValue) const override final
    {
        return _raftJsonIF.getBool(getPrefixedDataPath(dataPath).c_str(), defaultValue);
    }
    virtual double getDouble(const char *dataPath, double defaultValue) const override final
    {
        return _raftJsonIF.getDouble(getPrefixedDataPath(dataPath).c_str(), defaultValue);
    }
    
    // Get array elements
    virtual bool getArrayElems(const char *dataPath, std::vector<String>& strList) const override final
    {
        return _raftJsonIF.getArrayElems(getPrefixedDataPath(dataPath).c_str(), strList);
    }

    // Get keys
    virtual bool getKeys(const char *pDataPath, std::vector<String>& keysVector) const override final
    {
        return _raftJsonIF.getKeys(getPrefixedDataPath(pDataPath).c_str(), keysVector);
    }

    // Check if config contains key
    virtual bool contains(const char *dataPath) const override final
    {
        return _raftJsonIF.contains(getPrefixedDataPath(dataPath).c_str());
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
