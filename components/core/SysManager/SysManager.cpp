/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Manager for SysMods (System Modules)
// All modules that are core to the system should be derived from RaftSysMod
// These modules are then looped over by this manager's loop function
// They can be enabled/disabled and reconfigured in a consistent way
// Also modules can be referred to by name to allow more complex interaction
//
// Rob Dobson 2019
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "esp_system.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Logger.h"
#include "SysManager.h"
#include "RaftSysMod.h"
#include "RaftJsonNVS.h"
#include "RestAPIEndpointManager.h"
#include "RaftUtils.h"
#include "ESPUtils.h"
#include "NetworkSystem.h"
#include "DebugGlobals.h"

// Log prefix
static const char *MODULE_PREFIX = "SysMan";

// #define ONLY_ONE_MODULE_PER_LOOP 1

// Warn
#define WARN_ON_SYSMOD_SLOW_LOOP

// Debug supervisor step (for hangup detection within a loop call)
// Uses global logger variables - see logger.h
#define DEBUG_GLOB_SYSMAN 0
#define INCLUDE_PROTOCOL_FILE_UPLOAD_IN_STATS

// Debug
// #define DEBUG_SYSMOD_MEMORY_USAGE
// #define DEBUG_LIST_SYSMODS
// #define DEBUG_SYSMOD_WITH_GLOBAL_VALUE
// #define DEBUG_SEND_CMD_JSON_PERF
// #define DEBUG_REGISTER_MSG_GEN_CB
// #define DEBUG_API_ENDPOINTS
// #define DEBUG_SYSMOD_FACTORY

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SysManager::SysManager(const char* pModuleName,
                RaftJsonIF& systemConfig,
            const String sysManagerNVSNamespace,
            SysTypeManager& sysTypeManager,
            const char* pSystemName,
                const char* pDefaultFriendlyName,
                uint32_t serialLengthBytes, 
            const char* pSerialMagicStr) :
                            _systemConfig(systemConfig),
                    _mutableConfig(sysManagerNVSNamespace.c_str()),
                    _sysTypeManager(sysTypeManager)
{
    // Save params (some of these may be overriden later from config)
    _moduleName = pModuleName;
    _systemName = pSystemName ? pSystemName : 
#ifdef PROJECT_BASENAME
            PROJECT_BASENAME
#else
            "Unknown"
#endif
            ;
    _defaultFriendlyName = pDefaultFriendlyName ? pDefaultFriendlyName : _systemName;

    // Set serial length and magic string
    _serialLengthBytes = serialLengthBytes;
    if (pSerialMagicStr)
        _serialMagicStr = pSerialMagicStr;

    // Register this manager to all objects derived from RaftSysMod
    RaftSysMod::setSysManager(this);

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pre-Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SysManager::preSetup()
{
    // Extract system name from root level of config
    String sysTypeName = _systemConfig.getString("SysTypeName", _systemName.c_str());

    // Override system name if it is specified in the config
    _systemName = _systemConfig.getString("SystemName", sysTypeName.c_str());
    _systemVersion = _systemConfig.getString("SystemVersion", "0.0.0");

    // System config for this module
    RaftJsonPrefixed sysManConfig(_systemConfig, _moduleName.c_str());

    // System friendly name (may be overridden by config)
    _defaultFriendlyName = sysManConfig.getString("DefaultName", _defaultFriendlyName.c_str());

    // Prime the mutable config info
    _mutableConfigCache.friendlyName = _mutableConfig.getString("friendlyName", "");
    _mutableConfigCache.friendlyNameIsSet = _mutableConfig.getBool("nameSet", 0);
    _mutableConfigCache.serialNo = _mutableConfig.getString("serialNo", "");

    // Slow SysMod threshold
    _slowSysModThresholdUs = sysManConfig.getLong("slowSysModMs", SLOW_SYS_MOD_THRESHOLD_MS_DEFAULT) * 1000;

    // Monitoring period and monitoring timer
    _monitorPeriodMs = sysManConfig.getLong("monitorPeriodMs", 10000);
    _monitorTimerMs = millis();
    sysManConfig.getArrayElems("reportList", _monitorReportList);

    // System restart flag
    _systemRestartMs = millis();

    // System unique string - use BT MAC address
    _systemUniqueString = getSystemMACAddressStr(ESP_MAC_BT, "");

    // Reboot after N hours
    _rebootAfterNHours = sysManConfig.getLong("rebootAfterNHours", 0);

    // Reboot if disconnected for N minutes
    _rebootIfDiscMins = sysManConfig.getLong("rebootIfDiscMins", 0);

    // Pause WiFi for BLE
    _pauseWiFiForBLE = sysManConfig.getBool("pauseWiFiforBLE", 0);

    // Get friendly name
    bool friendlyNameIsSet = false;
    String friendlyName = getFriendlyName(friendlyNameIsSet);

    // Debug
    LOG_I(MODULE_PREFIX, "systemName %s friendlyName %s (default %s) serialNo %s nvsNamespace %s",
                _systemName.c_str(),
                (friendlyName + (friendlyNameIsSet ? " (user-set)" : "")).c_str(),
                _defaultFriendlyName.c_str(),
                _mutableConfigCache.serialNo.isEmpty() ? "<<NONE>>" : _mutableConfigCache.serialNo.c_str(),
                _mutableConfig.getNVSNamespace().c_str());
    LOG_I(MODULE_PREFIX, "slowSysModThresholdUs %d monitorPeriodMs %d rebootAfterNHours %d rebootIfDiscMins %d",
                _slowSysModThresholdUs,
                _monitorPeriodMs,
                _rebootAfterNHours, 
                _rebootIfDiscMins);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SysManager::postSetup()
{
    // Clear status change callbacks for sysmods (they are added again in postSetup)
    clearAllStatusChangeCBs();

    // Add our own REST endpoints
    if (_pRestAPIEndpointManager)
    {
        _pRestAPIEndpointManager->addEndpoint("reset", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET, 
                std::bind(&SysManager::apiReset, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), 
                "Restart program");
        _pRestAPIEndpointManager->addEndpoint("v", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET, 
                std::bind(&SysManager::apiGetVersion, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), 
                "Get version info");
        _pRestAPIEndpointManager->addEndpoint("sysmodinfo", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET, 
                std::bind(&SysManager::apiGetSysModInfo, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), 
                "Get sysmod info");
        _pRestAPIEndpointManager->addEndpoint("sysmoddebug", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET, 
                std::bind(&SysManager::apiGetSysModDebug, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), 
                "Get sysmod debug");
        _pRestAPIEndpointManager->addEndpoint("friendlyname", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                std::bind(&SysManager::apiFriendlyName, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                "Friendly name for system");
        _pRestAPIEndpointManager->addEndpoint("serialno", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                std::bind(&SysManager::apiSerialNumber, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                "Serial number");
        _pRestAPIEndpointManager->addEndpoint("hwrevno", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                std::bind(&SysManager::apiBaseSysTypeVersion, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                "HW revision");
        _pRestAPIEndpointManager->addEndpoint("testsetloopdelay", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                std::bind(&SysManager::apiTestSetLoopDelay, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                "Set a loop delay to test resilience, e.g. ?delayMs=10&skipCount=1, applies 10ms delay to alternate loops");
        _pRestAPIEndpointManager->addEndpoint("sysman", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                std::bind(&SysManager::apiSysManSettings, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                "Set SysMan settings, e.g. sysman?interval=2&rxBuf=10240");
    }

    // Short delay here to allow logging output to complete as some hardware configurations
    // require changes to serial uarts and this disturbs the logging flow
    delay(20);

    // Work through the SysMods in the factory and create those that are enabled
    // We may loop multiple times here to ensure that a SysMod is not created unless all it's dependencies
    // have been created
    bool anySysModsCreated = true;
    while (anySysModsCreated)
    {
        anySysModsCreated = false;
        for (SysModFactory::SysModClassDef& sysModClassDef : _sysModFactory.sysModClassDefs)
        {
            // Check if already created
            bool alreadyCreated = false;
            for (RaftSysMod* pSysMod : _sysModuleList)
            {
                if (pSysMod && pSysMod->modNameStr().equals(sysModClassDef.name))
                {
#ifdef DEBUG_SYSMOD_FACTORY
                    LOG_I(MODULE_PREFIX, "postSetup %s alreadyCreated");
#endif
                    alreadyCreated = true;
                    break;
                }
            }
            if (alreadyCreated)
                continue;

            // Get enabled flag from SysConfig
            bool isEnabled = _systemConfig.getBool((sysModClassDef.name + "/enable").c_str(), sysModClassDef.alwaysEnable);

#ifdef DEBUG_SYSMOD_FACTORY
            {
                String depListStrCSV;
                for (const String& dep : sysModClassDef.dependencyList)
                {
                    if (!depListStrCSV.isEmpty())
                        depListStrCSV += ",";
                    depListStrCSV += dep;
                }
                LOG_I(MODULE_PREFIX, "postSetup SysMod %s isEnabled %s (alwaysEnable %s) deps <<<%s>>> depsSatisfied %s", 
                            sysModClassDef.name.c_str(), 
                            isEnabled ? "YES" : "NO",
                            sysModClassDef.alwaysEnable ? "YES" : "NO",
                            depListStrCSV.c_str(),
                            checkSysModDependenciesSatisfied(sysModClassDef) ? "YES" : "NO");
            }
#endif

            // Check if enabled
            if (!isEnabled)
                continue;

            // Check if dependencies are met
            if (!checkSysModDependenciesSatisfied(sysModClassDef))
                continue;

            // Create the SysMod (it registers itself with the SysManager)
            sysModClassDef.pCreateFn(sysModClassDef.name.c_str(), _systemConfig);

            // Set flag to indicate we created a SysMod
            anySysModsCreated = true;
        }
    }

    // Now call setup on system modules
    for (RaftSysMod* pSysMod : _sysModuleList)
    {
#ifdef DEBUG_SYSMOD_MEMORY_USAGE
        uint32_t heapBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
        if (pSysMod)
            pSysMod->setup();

#ifdef DEBUG_SYSMOD_MEMORY_USAGE
        uint32_t heapAfter = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        ESP_LOGI(MODULE_PREFIX, "%s setup heap before %d after %d diff %d", 
                pSysMod->modName(), (int)heapBefore, (int)heapAfter, (int)(heapBefore - heapAfter));
#endif
    }

    // Give each SysMod the opportunity to add endpoints and comms channels and to keep a
    // pointer to the CommsCoreIF that can be used to send messages
    for (RaftSysMod* pSysMod : _sysModuleList)
    {
        if (pSysMod)
        {
            if (_pRestAPIEndpointManager)
                pSysMod->addRestAPIEndpoints(*_pRestAPIEndpointManager);
            if (_pCommsCore)
                pSysMod->addCommsChannels(*_pCommsCore);
        }            
    }

    // Post-setup - called after setup of all sysMods complete
    for (RaftSysMod* pSysMod : _sysModuleList)
    {
        if (pSysMod)
            pSysMod->postSetup();
    }

    // Check if WiFi to be paused when BLE connected
    LOG_I(MODULE_PREFIX, "pauseWiFiForBLEConn %s", _pauseWiFiForBLE ? "YES" : "NO");
    if (_pauseWiFiForBLE)
    {
        // Hook status change on BLE
        setStatusChangeCB("BLEMan", 
                std::bind(&SysManager::statusChangeBLEConnCB, this, std::placeholders::_1, std::placeholders::_2));
    }

#ifdef DEBUG_LIST_SYSMODS
    uint32_t sysModIdx = 0;
    for (RaftSysMod* pSysMod : _sysModuleList)
    {
        LOG_I(MODULE_PREFIX, "SysMod %d: %s", sysModIdx++, 
                pSysMod ? pSysMod->modName() : "UNKNOWN");
            
    }
#endif

    // Set last time connected for rebbot if disconnected check
    _rebootLastNetConnMs = millis();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loop (called from main thread's endless loop)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SysManager::loop()
{
    // Check if supervisory info is dirty
    if (_supervisorDirty)
    {
        // Ideally this operation should be atomic but we assume currently system modules
        // are only added before the main program loop is entered
        supervisorSetup();
        _supervisorDirty = false;
    }
       
    // Loop monitor periodically records timing
    if (_monitorTimerStarted)
    {
        // Check if monitor period is up
        if (Raft::isTimeout(millis(), _monitorTimerMs, 
                            _monitorShownFirstTime ? _monitorPeriodMs :MONITOR_PERIOD_FIRST_SHOW_MS))
        {
            // Wait until next period
            _monitorTimerMs = millis();
            _monitorShownFirstTime = true;
            
            // Calculate supervisory stats
            _supervisorStats.calculate();

            // Show stats
            statsShow();

            // Clear stats for start of next monitor period
            _supervisorStats.clear();
        }
    }
    else
    {
        // Start monitoring
        _supervisorStats.clear();
        _monitorTimerMs = millis();
        _monitorTimerStarted = true;
    }

    // Check the index into list of sys-mods to loop over is valid
    uint32_t numSysMods = _sysModLoopVector.size();
    if (numSysMods == 0)
        return;
    if (_loopCurModIdx >= numSysMods)
        _loopCurModIdx = 0;

    // Monitor how long it takes to go around loop
    _supervisorStats.outerLoopStarted();

#ifndef ONLY_ONE_MODULE_PER_LOOP
    for (_loopCurModIdx = 0; _loopCurModIdx < numSysMods; _loopCurModIdx++)
    {
#endif

    if (_sysModLoopVector[_loopCurModIdx])
    {
#ifdef DEBUG_SYSMOD_WITH_GLOBAL_VALUE
        DEBUG_GLOB_VAR_NAME(DEBUG_GLOB_SYSMAN) = _loopCurModIdx;
#endif
#ifdef WARN_ON_SYSMOD_SLOW_LOOP
        uint64_t sysModExecStartUs = micros();
#endif

        // Call the SysMod's loop method to allow code inside the module to run
        _supervisorStats.execStarted(_loopCurModIdx);
        _sysModLoopVector[_loopCurModIdx]->loop();
        _supervisorStats.execEnded(_loopCurModIdx);

#ifdef WARN_ON_SYSMOD_SLOW_LOOP
        uint64_t sysModLoopUs = micros() - sysModExecStartUs;
        if (sysModLoopUs > _slowSysModThresholdUs)
        {
            LOG_W(MODULE_PREFIX, "loop sysMod %s SLOW took %lldms", _sysModLoopVector[_loopCurModIdx]->modName(), sysModLoopUs/1000);
        }
#endif
    }

#ifndef ONLY_ONE_MODULE_PER_LOOP
    }
#endif

    // Debug
    // LOG_D(MODULE_PREFIX, "loop module %s", sysModInfo._pSysMod->modName());

    // Next SysMod
    _loopCurModIdx++;

    // Check system restart pending
    if (_systemRestartPending)
    {
        if (Raft::isTimeout(millis(), _systemRestartMs, SYSTEM_RESTART_DELAY_MS))
        {
            _systemRestartPending = false;
            esp_restart();
        }
    }

    // End loop
    _supervisorStats.outerLoopEnded();

    // Stress testing
    if (_stressTestLoopDelayMs > 0)
    {
        if (_stressTestCurSkipCount >= _stressTestLoopSkipCount)
        {
            delay(_stressTestLoopDelayMs);
            _stressTestCurSkipCount = 0;
        }
        else
        {
            _stressTestCurSkipCount++;
        }
    }

    // Check for reboot after N hours
    if ((_rebootAfterNHours != 0) && Raft::isTimeout(millis(), 0, _rebootAfterNHours * (uint64_t)3600000))
    {
        LOG_I(MODULE_PREFIX, "Rebooting after %d hours", _rebootAfterNHours);
        delay(500);
        esp_restart();
    }

    // Check for reboot after N mins if disconnected
    if (networkSystem.isIPConnected())
    {
        _rebootLastNetConnMs = millis();
    }
    else
    {
        if ((_rebootIfDiscMins != 0) && Raft::isTimeout(millis(), _rebootLastNetConnMs, _rebootIfDiscMins * (uint64_t)60000))
        {
            LOG_I(MODULE_PREFIX, "Rebooting after %d mins disconnected", _rebootIfDiscMins);
            delay(500);
            esp_restart();
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Manage SysMod List
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SysManager::addManagedSysMod(RaftSysMod* pSysMod)
{
    // Avoid adding null pointers
    if (!pSysMod)
        return;

    // Add to module list
    _sysModuleList.push_back(pSysMod);

    // Supervisor info now dirty
    _supervisorDirty = true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Supervisor Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SysManager::supervisorSetup()
{
    // Reset iterator to start of list
    _loopCurModIdx = 0;

    // Clear stats
    _supervisorStats.clear();

    // Clear and reserve sysmods from loop vector
    _sysModLoopVector.clear();
    _sysModLoopVector.reserve(_sysModuleList.size());

    // Add modules to list and initialise stats
    for (RaftSysMod* pSysMod : _sysModuleList)
    {
        if (pSysMod)
        {
            _sysModLoopVector.push_back(pSysMod);
            _supervisorStats.add(pSysMod->modName());
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add status change callback on a SysMod
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SysManager::setStatusChangeCB(const char* sysModName, SysMod_statusChangeCB statusChangeCB)
{
    // See if the sysmod is in the list
    for (RaftSysMod* pSysMod : _sysModuleList)
    {
        if (pSysMod->modNameStr().equals(sysModName))
        {
            // Debug
            LOG_I(MODULE_PREFIX, "setStatusChangeCB sysMod %s cbValid %d sysModFound OK", sysModName, statusChangeCB != nullptr);
            return pSysMod->setStatusChangeCB(statusChangeCB);
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clear all status change callbacks on all sysmods
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SysManager::clearAllStatusChangeCBs()
{
    // Debug
    // LOG_I(MODULE_PREFIX, "clearAllStatusChangeCBs");
    // Go through the sysmod list
    for (RaftSysMod* pSysMod : _sysModuleList)
    {
        return pSysMod->clearStatusChangeCBs();
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get status from SysMod
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
String SysManager::getStatusJSON(const char* sysModName)
{
    // See if the sysmod is in the list
    for (RaftSysMod* pSysMod : _sysModuleList)
    {
        if (pSysMod->modNameStr().equals(sysModName))
        {
            return pSysMod->getStatusJSON();
        }
    }
    return "{}";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get debugStr from SysMod
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
String SysManager::getDebugJSON(const char* sysModName)
{
    // Check if it is the SysManager's own stats that are wanted
    if (strcasecmp(sysModName, "SysMan") == 0)
        return _supervisorStats.getSummaryString();

    // Check for global debug values
    if (strcasecmp(sysModName, "Globs") == 0)
    {
        return DebugGlobals::getDebugJson(false);
    }

    // Check for stats callback
    if (strcasecmp(sysModName, "StatsCB") == 0)
    {
        if (_statsCB)
            return _statsCB();
        return "";
    }

    // See if the sysmod is in the list
    for (RaftSysMod* pSysMod : _sysModuleList)
    {
        if (pSysMod->modNameStr().equals(sysModName))
        {
            return pSysMod->getDebugJSON();
        }
    }
    return "{}";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send command to SysMod
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode SysManager::sendCmdJSON(const char* sysModName, const char* cmdJSON)
{
    // See if the sysmod is in the list
#ifdef DEBUG_SEND_CMD_JSON_PERF
    uint64_t startUs = micros();
#endif
    for (RaftSysMod* pSysMod : _sysModuleList)
    {
        if (pSysMod->modNameStr().equals(sysModName))
        {
#ifdef DEBUG_SEND_CMD_JSON_PERF
            uint64_t foundSysModUs = micros();
#endif

            RaftRetCode rslt = pSysMod->receiveCmdJSON(cmdJSON);

#ifdef DEBUG_SEND_CMD_JSON_PERF
            LOG_I(MODULE_PREFIX, "sendCmdJSON %s rslt %s found in %dus exec time %dus", 
                    sysModName, Raft::getRetCodeStr(rslt), int(foundSysModUs - startUs), int(micros() - foundSysModUs));
#endif
            return rslt;
        }
    }
#ifdef DEBUG_SEND_CMD_JSON_PERF
    LOG_I(MODULE_PREFIX, "getHWElemByName %s NOT found in %dus", sysModName, int(micros() - startUs));
#endif
    return RAFT_INVALID_OPERATION;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get named value from SysMod
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

double SysManager::getNamedValue(const char* sysModName, const char* valueName, bool& isValid)
{
    // See if the sysmod is in the list
    for (RaftSysMod* pSysMod : _sysModuleList)
    {
        if (pSysMod->modNameStr().equals(sysModName))
        {
            return pSysMod->getNamedValue(valueName, isValid);
        }
    }
    isValid = false;
    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send message-generator callback to SysMod
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SysManager::sendMsgGenCB(const char* sysModName, const char* msgGenID, SysMod_publishMsgGenFn msgGenCB, SysMod_stateDetectCB stateDetectCB)
{
    // See if the sysmod is in the list
    for (RaftSysMod* pSysMod : _sysModuleList)
    {
        if (pSysMod->modNameStr().equals(sysModName))
        {
#ifdef DEBUG_REGISTER_MSG_GEN_CB
            LOG_I(MODULE_PREFIX, "sendMsgGenCB registered %s with the %s sysmod", msgGenID, sysModName);
#endif
            pSysMod->receiveMsgGenCB(msgGenID, msgGenCB, stateDetectCB);
            return;
        }
    }
#ifdef DEBUG_REGISTER_MSG_GEN_CB
    LOG_W(MODULE_PREFIX, "sendMsgGenCB NOT FOUND %s %s", sysModName, msgGenID);
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// API endpoints
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode SysManager::apiReset(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Register that a restart is required but don't restart immediately
    // as the acknowledgement would not get through
    systemRestart();

    // Result
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
}

RaftRetCode SysManager::apiGetVersion(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Get serial number
    String serialNo = _mutableConfig.getString("serialNo", "");
    char versionJson[225];
    // Hardware revision Json
    String hwRevJson = getBaseSysVersJson();
    snprintf(versionJson, sizeof(versionJson),
             R"({"req":"%s","rslt":"ok","SystemName":"%s","SystemVersion":"%s","SerialNo":"%s",)"
            R"("MAC":"%s",%s})",
             reqStr.c_str(), 
             _systemName.c_str(), 
             _systemVersion.c_str(), 
             serialNo.c_str(),
             _systemUniqueString.c_str(),
            hwRevJson.c_str());
    respStr = versionJson;

#ifdef DEBUG_API_ENDPOINTS
    LOG_I(MODULE_PREFIX, "apiGetVersion %s", respStr.c_str());
#endif

    return RaftRetCode::RAFT_OK;
}

RaftRetCode SysManager::apiGetSysModInfo(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Get name of SysMod
    String sysModName = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);
    respStr = getStatusJSON(sysModName.c_str());
    return RaftRetCode::RAFT_OK;
}

RaftRetCode SysManager::apiGetSysModDebug(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Get name of SysMod
    String sysModName = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);
    String debugStr = "\"debug\":" + getDebugJSON(sysModName.c_str());
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, debugStr.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Friendly name
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode SysManager::apiFriendlyName(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Check if we're setting
    if (RestAPIEndpointManager::getNumArgs(reqStr.c_str()) > 1)
    {
        // Set name
        String friendlyName = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);
        String errorStr;
        bool rslt = setFriendlyName(friendlyName.c_str(), true, errorStr);
        if (!rslt)
        {
            Raft::setJsonErrorResult(reqStr.c_str(), respStr, errorStr.c_str());
            return RaftRetCode::RAFT_INVALID_DATA;
        }
    }

    // Get current
    bool friendlyNameIsSet = false;
    String friendlyName = getFriendlyName(friendlyNameIsSet);
    LOG_I(MODULE_PREFIX, "apiFriendlyName -> %s, friendlyNameIsSet %s", 
                friendlyName.c_str(), friendlyNameIsSet ? "Y" : "N");

    // Create response JSON
    char JsonOut[MAX_FRIENDLY_NAME_LENGTH + 70];
    snprintf(JsonOut, sizeof(JsonOut), R"("friendlyName":"%s","friendlyNameIsSet":%d)", 
                friendlyName.c_str(), friendlyNameIsSet);
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, JsonOut);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serial number
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode SysManager::apiSerialNumber(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Check if we're setting
    if (RestAPIEndpointManager::getNumArgs(reqStr.c_str()) > 1)
    {
        // Get serial number to set
        String serialNoHexStr = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);
        uint8_t serialNumBuf[_serialLengthBytes];
        if (Raft::getBytesFromHexStr(serialNoHexStr.c_str(), serialNumBuf, _serialLengthBytes) != _serialLengthBytes)
        {
            Raft::setJsonErrorResult(reqStr.c_str(), respStr, "SNNot16Byt");
            return RaftRetCode::RAFT_INVALID_DATA;
        }

        // Validate magic string
        String magicString;
        if (RestAPIEndpointManager::getNumArgs(reqStr.c_str()) > 2)
        {
            magicString = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 2);
            if (!magicString.equals(_serialMagicStr) && !_serialMagicStr.isEmpty())
            {
                Raft::setJsonErrorResult(reqStr.c_str(), respStr, "SNNeedsMagic");
                return RaftRetCode::RAFT_INVALID_DATA;
            }
        }

        // Get formatted serial no
        Raft::getHexStrFromBytes(serialNumBuf, _serialLengthBytes, _mutableConfigCache.serialNo);

        // Store the serial no
        _mutableConfig.setJsonDoc(getMutableConfigJson().c_str());
    }

    // Get serial number from mutable config
    String serialNo = _mutableConfig.getString("serialNo", "");

    // Create response JSON
    char JsonOut[MAX_FRIENDLY_NAME_LENGTH + 100];
    snprintf(JsonOut, sizeof(JsonOut), R"("SerialNo":"%s")", serialNo.c_str());
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, JsonOut);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// HW revision
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode SysManager::apiBaseSysTypeVersion(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Create response JSON
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, getBaseSysVersJson().c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test function to set loop delay
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode SysManager::apiTestSetLoopDelay(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Extract params
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    RestAPIEndpointManager::getParamsAndNameValues(reqStr.c_str(), params, nameValues);
    RaftJson nameValueParamsJson = RaftJson::getJSONFromNVPairs(nameValues, true);

    // Extract values
    _stressTestLoopDelayMs = nameValueParamsJson.getLong("delayMs", 0);
    _stressTestLoopSkipCount = nameValueParamsJson.getLong("skipCount", 0);
    _stressTestCurSkipCount = 0;

    // Debug
    LOG_I(MODULE_PREFIX, "apiTestSetLoopDelay delay %dms skip %d loops", _stressTestLoopDelayMs, _stressTestLoopSkipCount);

    // Create response JSON
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup SysMan diagnostics
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode SysManager::apiSysManSettings(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Extract params
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    RestAPIEndpointManager::getParamsAndNameValues(reqStr.c_str(), params, nameValues);
    RaftJson nameValueParamsJson = RaftJson::getJSONFromNVPairs(nameValues, true);

    // Extract log output interval
    _monitorPeriodMs = nameValueParamsJson.getDouble("interval", _monitorPeriodMs/1000) * 1000;
    if (_monitorPeriodMs < 1000)
        _monitorPeriodMs = 1000;

    // Extract list of modules to show information on
    std::vector<String> reportList;
    if (nameValueParamsJson.getArrayElems("report", reportList))
    {
        _monitorReportList = reportList;
        // for (const String& s : reportList)
        //     LOG_I(MODULE_PREFIX, "apiSysManSettings reportitem %s", s.c_str());
    }

    // Check for baud-rate change
    int baudRate = nameValueParamsJson.getLong("baudRate", -1);
    String debugStr;
    if (baudRate >= 0)
    {
        // Configure the serial console
        String cmdJson = "{\"cmd\":\"set\",\"baudRate\":" + String(baudRate) + "}";
        sendCmdJSON("SerialConsole", cmdJson.c_str());
        debugStr = " baudRate " + String(baudRate);
    }

    // Check for buffer-size changes
    int rxBufSize = nameValueParamsJson.getLong("rxBuf", -1);
    int txBufSize = nameValueParamsJson.getLong("txBuf", -1);
    if ((rxBufSize >= 0) || (txBufSize >= 0))
    {
        // Configure the serial console
        String cmdJson = "{\"cmd\":\"set\"";
        if (rxBufSize >= 0)
        {
            cmdJson += ",\"rxBuf\":" + String(rxBufSize);
            debugStr = " rxBufSize " + String(rxBufSize);
        }
        if (txBufSize >= 0)
        {
            cmdJson += ",\"txBuf\":" + String(txBufSize);
            debugStr = " txBufSize " + String(txBufSize);
        }
        cmdJson += "}";
        sendCmdJSON("SerialConsole", cmdJson.c_str());
    }

    // Debug
    LOG_I(MODULE_PREFIX, "apiSysManSettings report interval %.1f secs reportList %s%s", 
                _monitorPeriodMs/1000.0, nameValueParamsJson.getString("report","").c_str(),
                debugStr.c_str());

    // Create response JSON
    String reqStrWithoutQuotes = reqStr;
    reqStrWithoutQuotes.replace("\"","");
    return Raft::setJsonBoolResult(reqStrWithoutQuotes.c_str(), respStr, true);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get mutable config JSON string
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String SysManager::getMutableConfigJson()
{
    char jsonConfig[MAX_FRIENDLY_NAME_LENGTH + _serialLengthBytes*2 + 70];
    snprintf(jsonConfig, sizeof(jsonConfig), 
            R"({"friendlyName":"%s","nameSet":%d,"serialNo":"%s"})", 
                _mutableConfigCache.friendlyName.c_str(), 
                _mutableConfigCache.friendlyNameIsSet, 
                _mutableConfigCache.serialNo.c_str());
    return jsonConfig;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Stats show
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SysManager::statsShow()
{
    // Generate stats
    char statsStr[200];
    String friendlyNameStr;
    if (_mutableConfigCache.friendlyNameIsSet)
        friendlyNameStr = "\"f\":\"" + _mutableConfigCache.friendlyName + "\",";
    snprintf(statsStr, sizeof(statsStr), R"({%s"n":"%s","v":"%s","r":"%s","hpInt":%d,"hpMin":%d,"hpAll":%d)", 
                friendlyNameStr.c_str(),
                _systemName.c_str(),
                _systemVersion.c_str(),
                _hardwareRevision.c_str(),
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                heap_caps_get_free_size(MALLOC_CAP_8BIT));

    // Stats string
    String statsOut = statsStr;

    // Add stats
    for (String& srcStr : _monitorReportList)
    {
        String modStr = getDebugJSON(srcStr.c_str());
        if (modStr.length() > 2)
            statsOut += ",\"" + srcStr + "\":" + modStr;
    }

    // Terminate JSON
    statsOut += "}";

    // Report stats
    LOG_I(MODULE_PREFIX, "%s", statsOut.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Friendly name helpers
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String SysManager::getFriendlyName(bool& isSet)
{
    // Check if set
    isSet = getFriendlyNameIsSet();

    // Handle default naming
    String friendlyNameNVS = _mutableConfig.getString("friendlyName", "");
    String friendlyName = friendlyNameNVS;
    if (!isSet || friendlyName.isEmpty())
    {
        friendlyName = _defaultFriendlyName;
        if (_systemUniqueString.length() >= 6)
            friendlyName += "_" + _systemUniqueString.substring(_systemUniqueString.length()-6);
    }
    LOG_I(MODULE_PREFIX, "getFriendlyName %s (isSet %s nvsStr %s default %s uniqueStr %s)", 
                friendlyName.c_str(), 
                isSet ? "Y" : "N",
                friendlyNameNVS.c_str(),
                _defaultFriendlyName.c_str(), 
                _systemUniqueString.c_str());
    return friendlyName;
}

bool SysManager::getFriendlyNameIsSet()
{
    return _mutableConfig.getLong("nameSet", 0);
}

bool SysManager::setFriendlyName(const String& friendlyName, bool setHostname, String& errorStr)
{
    // Update cached name
    _mutableConfigCache.friendlyName = friendlyName;
    _mutableConfigCache.friendlyName.trim();
    _mutableConfigCache.friendlyName = _mutableConfigCache.friendlyName.substring(0, MAX_FRIENDLY_NAME_LENGTH);
    _mutableConfigCache.friendlyNameIsSet = !friendlyName.isEmpty();
    if (_mutableConfigCache.friendlyNameIsSet)
    {
        LOG_I(MODULE_PREFIX, "setFriendlyName %s", _mutableConfigCache.friendlyName.c_str());
    }
    else
    {
        LOG_I(MODULE_PREFIX, "setFriendlyName blank");
    }

    // Setup network system hostname
    if (_mutableConfigCache.friendlyNameIsSet && setHostname)
        networkSystem.setHostname(_mutableConfigCache.friendlyName.c_str());

    // Store the new name (even if it is blank)
    _mutableConfig.setJsonDoc(getMutableConfigJson().c_str());
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Status change CB on BLE
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SysManager::statusChangeBLEConnCB(const String& sysModName, bool changeToOnline)
{
    // Check if WiFi should be paused
    LOG_I(MODULE_PREFIX, "BLE connection change isConn %s", changeToOnline ? "YES" : "NO");
    if (_pauseWiFiForBLE)
    {
        networkSystem.pauseWiFi(changeToOnline);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get hardware revision JSON
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String SysManager::getBaseSysVersJson()
{
    // Get hardware revision string (if all digits then set in JSON as a number for backward compatibility)
    String baseSysTypeVersStr = _sysTypeManager.getBaseSysTypeVersion();
    String ricHWRevStr = _sysTypeManager.getBaseSysTypeVersion();
    bool allDigits = true;
    for (int i = 0; i < ricHWRevStr.length(); i++)
    {
        if (!isdigit(ricHWRevStr.charAt(i)))
        {
            allDigits = false;
            break;
        }
    }
    if (!allDigits)
        ricHWRevStr = "\"" + ricHWRevStr + "\"";
    // Form JSON
    return "\"SysTypeVers\":\"" + baseSysTypeVersStr + "\",\"RicHwRevNo\":" + ricHWRevStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check SysMod dependencies satisfied
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool SysManager::checkSysModDependenciesSatisfied(const SysModFactory::SysModClassDef& sysModClassDef)
{
    // Check if all dependencies are satisfied
    for (auto& dependency : sysModClassDef.dependencyList)
    {
        // See if the sysmod is in the list of SysMods
        bool found = false;
        for (RaftSysMod* pSysMod : _sysModuleList)
        {
            if (pSysMod->modNameStr().equals(dependency))
            {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}