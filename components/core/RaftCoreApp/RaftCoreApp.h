/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftCoreApp - standardised app using Raft
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "SysManager.h"
#include "RestAPIEndpointManager.h"
#include "CommsChannelManager.h"
#include "ProtocolExchange.h"
#include "RaftBusSystem.h"
#include "DeviceManager.h"
#include "RaftJsonNVS.h"

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
    /// @brief Loop
    /// @note Called from main loop
    void loop();

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set base SysType version
    /// @param pVersionStr - version string
    void setBaseSysTypeVersion(const char* pVersionStr)
    {
        _sysTypeManager.setBaseSysTypeVersion(pVersionStr);
    }
    
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Unchain default system config - call this if you don't want to use the default config
    void unchainDefaultSystemConfig()
    {
        _sysTypeConfig.setChainedRaftJson(nullptr);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get SysManager
    SysManager& getSysManager()
    {
        return _sysManager;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Register SysMod with the SysMod factory
    /// @param pClassName - name of the class
    /// @param pCreateFn - function to create the SysMod
    /// @param alwaysEnable - if true then the SysMod is always enabled
    /// @param pDependencyListCSV - comma separated list of dependencies
    void registerSysMod(const char* pClassName, SysModCreateFn pCreateFn, bool alwaysEnable = false, const char* pDependencyListCSV = nullptr)
    {
        _sysManager.registerSysMod(pClassName, pCreateFn, alwaysEnable, pDependencyListCSV);
    }

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

    // Device manager
    DeviceManager _deviceManager;

    // SysManager is setup
    bool _sysManagerSetupDone = false;

    // Start debugging thread
    void startDebuggingThread();
    RaftThreadHandle _debuggingThreadHandle = RAFT_THREAD_HANDLE_INVALID;

    // Debug
    static constexpr const char* MODULE_PREFIX = "RaftApp";
};