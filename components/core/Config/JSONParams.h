/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// JSON Params
// Holder for JSON information providing access methods
//
// Rob Dobson 2016-2022
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "ConfigBase.h"

class JSONParams : public ConfigBase
{
public:
    JSONParams(const char* configStr) :
        ConfigBase(configStr)
    {
    }

    JSONParams(const String& configStr) :
        ConfigBase(configStr)
    {
    }

    const char* c_str()
    {
        return ConfigBase::getConfigString().c_str();
    }

    String configStr()
    {
        return ConfigBase::getConfigString();
    }
};
