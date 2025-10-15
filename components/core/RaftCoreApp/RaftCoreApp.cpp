/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftApp - standardised app using Raft
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// #include <stdio.h>
#if !defined(__linux__)
#include "sdkconfig.h"
#include "esp_log.h"
#endif
#include "RaftCoreApp.h"
#include "RaftJsonNVS.h"
#include "SysTypeInfoRec.h"
#include "RaftThreading.h"
#include "Logger.h"

#if __has_include("SysTypeInfoRecs.h")
#include "SysTypeInfoRecs.h"
#else
#pragma message("SysTypeInfoRecs.h not found - using empty json")
static constexpr const SysTypeInfoRec sysTypeInfoRecs[] = {};
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Default system config
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// #define MACRO_STRINGIFY(x) #x
// #define MACRO_TOSTRING(x) MACRO_STRINGIFY(x)

#ifndef PROJECT_BASENAME
#define PROJECT_BASENAME "Unknown"
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
    _protocolExchange("ProtExchg", _systemConfig),
    _deviceManager("DevMan", _systemConfig)
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

    // Protocol exchange file stream activity fn
    _protocolExchange.setFileStreamActivityHook( [this](bool isMainFWUpdate, bool isFileSystemActivity, bool isStreaming) {
            _sysManager.informOfFileStreamActivity(isMainFWUpdate, isFileSystemActivity, isStreaming);
        }
    );

    // Setup SysManager
    _sysManager.setRestAPIEndpoints(_restAPIEndpointManager);
    _sysManager.setCommsCore(&_commsChannelManager);
    _sysManager.setProtocolExchange(&_protocolExchange);
    _sysManager.setDeviceManager(&_deviceManager);
    _sysManager.preSetup();

    // Get the system version (maybe overridden by SysType)
    String systemVersion = _systemConfig.getString("SystemVersion", platform_getAppVersion().c_str());

    // Start debugging thread if required
    startDebuggingThread();

    // Log out system info
#if defined(__linux__)
    LOG_I(MODULE_PREFIX, "%s %s (built %s %s)", PROJECT_BASENAME, systemVersion.c_str(), __DATE__, __TIME__);
#else
    ESP_LOGI(MODULE_PREFIX, PROJECT_BASENAME " %s (built " __DATE__ " " __TIME__ ") Heap (int) %d (all) %d",
                        systemVersion.c_str(),
                        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                        heap_caps_get_free_size(MALLOC_CAP_8BIT));
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor
RaftCoreApp::~RaftCoreApp()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup
void RaftCoreApp::setup()
{
    // Nothing to do here - setup is done in constructor
    // This is just for API compatibility
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Loop
/// @note Called in main loop
void RaftCoreApp::loop()
{
    if (!_sysManagerSetupDone)
    {
        // Initialise the system manager here so that SysMods are created
        _sysManagerSetupDone = true;
        _sysManager.postSetup();
    }
    // Loop over all the system modules
    _sysManager.loop();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Start debugging thread
void RaftCoreApp::startDebuggingThread()
{
#ifdef DEBUG_USING_GLOBAL_VALUES
    // Start the debugging thread
    LOG_I(MODULE_PREFIX, "Starting debugging thread");
    if (!RaftThread_start(_debuggingThreadHandle,
                [](void *pArg) {
                    LOG_I(MODULE_PREFIX, "Inside debugging thread");
                    while (true)
                    {
                        LOG_I(MODULE_PREFIX, "Debugging thread %s", Raft::getDebugGlobalsJson(false).c_str());
                        // Sleep
                        RaftThread_sleep(1000);
                    }
                },
                nullptr))
    {
        LOG_E(MODULE_PREFIX, "Failed to start debugging thread");
    }
#endif
}

