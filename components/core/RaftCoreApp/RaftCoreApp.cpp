/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftApp - standardised app using Raft
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// #include <stdio.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "RaftCoreApp.h"
#include "RaftJsonNVS.h"
#include "SysTypeInfoRec.h"

#if __has_include("SysTypeInfoRecs.h")
#include "SysTypeInfoRecs.h"
#else
#pragma message("SysTypeInfoRecs.h not found - using empty json")
static constexpr const SysTypeInfoRec sysTypeInfoRecs[] = {};
#endif

static const char *MODULE_PREFIX = "RaftCoreApp";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Default system config
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// #define MACRO_STRINGIFY(x) #x
// #define MACRO_TOSTRING(x) MACRO_STRINGIFY(x)

#ifndef PROJECT_BASENAME
#define PROJECT_BASENAME "Unknown"
#endif
#ifndef SYSTEM_VERSION
#define SYSTEM_VERSION "Unknown"
#endif

// NOTE:
// In VSCode C++ raw strings can be removed - to reveal JSON with regex search and replace:
//      regex:    R"\((.*)\)"
//      replace:  $1
// And then reinserted with:
//      regex:    (\s*)(.*)
//      replace:  $1R"($2)"

static const char *defaultConfigJSON =
    R"({)"
        R"("SystemName":")" PROJECT_BASENAME R"(",)"
        R"("SystemVersion":")" SYSTEM_VERSION R"(",)"
        R"("IDFVersion":")" IDF_VER R"(",)"
        R"("DefaultName":")" PROJECT_BASENAME R"(",)"
        R"("SysManager":{)"
            R"("monitorPeriodMs":10000,)"
            R"("reportList":["SysMan","StatsCB"],)"
            R"("RICSerial":{)"
                R"("FrameBound":"0xE7",)"
                R"("CtrlEscape":"0xD7")"
            R"(})"
        R"(})"
    R"(})"
;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
RaftCoreApp::RaftCoreApp() :
    _systemConfig("sys"),
    _sysTypeConfig("sysType"),
    _sysTypeManager(_systemConfig, _sysTypeConfig),
    _defaultSystemConfig(defaultConfigJSON, false),
    _sysManager("SysManager", _systemConfig, "system", _sysTypeManager),
    _commsChannelManager("CommsMan", _systemConfig),
    _protocolExchange("ProtExchg", _systemConfig)
{
    // Chain the default config to the SysType so that it is used as a fallback
    // if no SysTypes are specified
    _sysTypeConfig.setChainedRaftJson(&_defaultSystemConfig);

    // Set base types in SysTypeManager
    _sysTypeManager.setBaseSysTypes(sysTypeInfoRecs, sizeof(sysTypeInfoRecs)/sizeof(SysTypeInfoRec));

    // Add the system restart callback to the SysTypeManager
    _sysTypeManager.setSystemRestartCallback([this] { _sysManager.systemRestart(); });

    // SysTypeManager endpoints
    _sysTypeManager.addRestAPIEndpoints(_restAPIEndpointManager);

    // Setup SysManager
    _sysManager.setRestAPIEndpoints(_restAPIEndpointManager);
    _sysManager.setCommsCore(&_commsChannelManager);
    _sysManager.setProtocolExchange(&_protocolExchange);
    _sysManager.preSetup();

    // Get the app version (maybe overridden by SysType)
    String appVersion = _systemConfig.getString("SystemVersion", SYSTEM_VERSION);

    // Log out system info
    ESP_LOGI(MODULE_PREFIX, PROJECT_BASENAME " %s (built " __DATE__ " " __TIME__ ") Heap (int) %d (all) %d", 
                        appVersion.c_str(),
                        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                        heap_caps_get_free_size(MALLOC_CAP_8BIT));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor
RaftCoreApp::~RaftCoreApp()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Service
/// @note Called from main loop
void RaftCoreApp::service()
{
    if (!_sysManagerSetupDone)
    {
        // Initialise the system manager here so that SysMods are created
        _sysManagerSetupDone = true;
        _sysManager.postSetup();
    }
    // Service all the system modules
    _sysManager.service();
}
