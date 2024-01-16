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

    // Get SysType key
    String getSysTypeKey() const {
        if (pSysTypeKey)
            return pSysTypeKey;
        return "";
    }

    // SysType name
    const char* pSysTypeName = nullptr;

    // Hardware revision
    const char* pSysTypeKey = nullptr;

    // SysType JSON document
    const char* pSysTypeJSONDoc = nullptr;
};
