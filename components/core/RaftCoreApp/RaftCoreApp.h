/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftApp - standardised app using Raft
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "SysManager.h"
#include "SysTypeManager.h"
#include "CommsChannelManager.h"
#include "ProtocolExchange.h"

class RaftCoreApp
{
public:
    // Constructor & destructor
    RaftCoreApp();
    virtual ~RaftCoreApp();

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief setup
    void setup();

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Service
    /// @note Called from main loop
    void service();

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Chain default system config
    void chainDefaultSystemConfig();

private:
    // System configuration
    RaftJsonNVS _systemConfig;

    // SysType configuration
    RaftJson _sysTypeConfig;

    // SysType manager
    SysTypeManager _sysTypeManager;

    // Default system config
    RaftJson _defaultSystemConfig;

    // System Module Manager;
    SysManager _sysManager;
    
    // REST API endpoint manager
    RestAPIEndpointManager _restAPIEndpointManager;

    // Comms channel manager
    CommsChannelManager _commsChannelManager;

    // ProtocolExchange
    ProtocolExchange _protocolExchange;
};