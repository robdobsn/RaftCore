/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Protocol Definition
// Definition for protocol messages
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <ArduinoOrAlt.h>
#include <ProtocolBase.h>

class ConfigBase;

class ProtocolCodecFactoryHelper
{
public:
    String protocolName;
    ProtocolCreateFnType createFn;
    ConfigBase& config;
    const char* pConfigPrefix;
    CommsChannelInboundHandleMsgFnType frameRxCB;
    CommsChannelInboundCanAcceptFnType readyToRxCB;
};
