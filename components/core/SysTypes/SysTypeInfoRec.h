/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Record to store SysType information
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftArduino.h"

class SysTypeInfoRec
{
public:
    // Get SysType name
    String getSysTypeName() const {
        if (pSysTypeName)
            return pSysTypeName;
        return "";
    }

    // Get SysType version
    String getSysTypeVersion() const {
        if (pSysTypeVersion)
            return pSysTypeVersion;
        return "";
    }

    // SysType name
    const char* pSysTypeName = nullptr;

    // SysType version
    const char* pSysTypeVersion = nullptr;

    // SysType JSON document
    const char* pSysTypeJSONDoc = nullptr;
};
