/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Protocol Definition
// Definition for protocol messages
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include "RaftArduino.h"
#include "ProtocolBase.h"

class RafJsonIF;

class ProtocolCodecFactoryHelper
{
public:
    String protocolName;
    ProtocolCreateFnType createFn;
    RaftJsonIF& config;
    const char* pConfigPrefix;
    CommsChannelInboundHandleMsgFnType frameRxCB;
    CommsChannelInboundCanAcceptFnType readyToRxCB;
};
