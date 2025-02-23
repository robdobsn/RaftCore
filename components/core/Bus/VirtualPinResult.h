/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Virtual Pin Result
//
// Rob Dobson 2025
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftRetCode.h"

class VirtualPinResult
{
public:
    VirtualPinResult()
    {
    }
    VirtualPinResult(int vPinNum, bool level, RaftRetCode retCode)
    {
        this->vPinNum = vPinNum;
        this->level = level;
        this->retCode = retCode;
    }
    int vPinNum = -1;
    bool level = false;
    RaftRetCode retCode = RaftRetCode::RAFT_OTHER_FAILURE;
};

typedef void (*VirtualPinSetCallbackType) (void* pCallbackData, RaftRetCode reqResult);
typedef void (*VirtualPinReadCallbackType) (void* pCallbackData, VirtualPinResult reqResult);
