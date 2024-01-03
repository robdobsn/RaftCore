/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftJsonIF - JSON parser interface
//
// Rob Dobson 2017-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RaftArduino.h>

class RaftJsonIF
{
public:

    // Get values from JSON key/value pairs
    virtual String getString(const char* pDataPath, const char* defaultValue) const = 0;
    virtual double getDouble(const char* pDataPath, double defaultValue) const = 0;
    virtual long getLong(const char* pDataPath, long defaultValue) const = 0;
    virtual bool getBool(const char* pDataPath, bool defaultValue) const = 0;

    // Get array elements
    virtual bool getArrayElems(const char *pDataPath, std::vector<String>& strList) const = 0;

    // Get keys
    virtual bool getKeys(const char *pDataPath, std::vector<String>& keysVector) const = 0;

    // Check if config contains key
    virtual bool contains(const char* pDataPath) const = 0;

};
