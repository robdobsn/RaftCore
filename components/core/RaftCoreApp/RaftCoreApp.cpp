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
#include "SysTypesInfo.h"
#include "RaftJsonNVS.h"

static const char *MODULE_PREFIX = "RaftCoreApp";

// C, ESP32 and RTOS
// #include "nvs_flash.h"
// #include "esp_event.h"
// #include "esp_task_wdt.h"
// #include "esp_heap_caps.h"
// #include "freertos/FreeRTOS.h"

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
    _sysManager("SysManager", _systemConfig, "system"),
    _commsChannelManager("CommsMan", _systemConfig),
    _protocolExchange("ProtExchg", _systemConfig)
{
    // Init NVS
    RaftJsonNVS::initNVS(true);

    // Set base types in SysTypeManager
    _sysTypeManager.setBaseSysTypes(&sysTypesInfo);

    // Add the system restart callback to the SysTypeManager
    _sysTypeManager.setSystemRestartCallback([this] { _sysManager.systemRestart(); });

    // SysTypeManager endpoints
    _sysTypeManager.addRestAPIEndpoints(_restAPIEndpointManager);

    // Setup SysManager
    _sysManager.setRestAPIEndpoints(_restAPIEndpointManager);
    _sysManager.setCommsCore(&_commsChannelManager);
    _sysManager.setProtocolExchange(&_protocolExchange);

    // Log out system info
    ESP_LOGI(MODULE_PREFIX, PROJECT_BASENAME " " SYSTEM_VERSION " (built " __DATE__ " " __TIME__ ") Heap %d", 
                        heap_caps_get_free_size(MALLOC_CAP_8BIT));

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor
RaftCoreApp::~RaftCoreApp()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Chain default system config
void RaftCoreApp::chainDefaultSystemConfig()
{
    // Chain the default config to the SysType so that it is used as a fallback
    // if no SysTypes are specified
    _sysTypeConfig.setChainedRaftJson(&_defaultSystemConfig);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup
/// @note Called after other modules have been setup
void RaftCoreApp::setup()
{
    // Initialise the system module manager here so that API endpoints are registered
    // before file system ones
    _sysManager.setup();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Service
/// @note Called from main loop
void RaftCoreApp::service()
{
    // Service all the system modules
    _sysManager.service();
}
