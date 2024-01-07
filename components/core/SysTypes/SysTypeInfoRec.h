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

    // SysType name
    const char* pSysTypeName = nullptr;

    // Hardware revision
    int hwRev = 0;

    // SysType JSON document
    const char* pSysTypeJSONDoc = nullptr;
};
