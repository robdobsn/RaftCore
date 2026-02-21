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

#include <inttypes.h>
#include "Logger.h"
#include "SysManager.h"
#include "RaftSysMod.h"
#include "RaftJsonNVS.h"
#include "RestAPIEndpointManager.h"
#include "RaftUtils.h"
#include "PlatformUtils.h"
#include "DebugGlobals.h"
#include "RICRESTMsg.h"

#ifdef ESP_PLATFORM
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "NetworkSystem.h"
#endif // ESP_PLATFORM

// Settings
// #define ONLY_ONE_MODULE_PER_LOOP 1
#define INCLUDE_PROTOCOL_FILE_UPLOAD_IN_STATS

// Debug
// #define DEBUG_SYSMOD_MEMORY_USAGE
// #define DEBUG_LIST_SYSMODS
// #define DEBUG_SEND_CMD_JSON_PERF
// #define DEBUG_REGISTER_MSG_GEN_CB
// #define DEBUG_API_ENDPOINTS
// #define DEBUG_SYSMOD_FACTORY
// #define DEBUG_FRIENDLY_NAME_SET
// #define DEBUG_STATUS_CHANGE_CALLBACK
// #define DEBUG_GET_FRIENDLY_NAME
// #define DEBUG_SEND_REPORT_MESSAGE

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
/// @param pModuleName - name of the module
/// @param systemConfig - system configuration
/// @param sysManagerNVSNamespace - namespace for the NVS storage for this module
/// @param sysTypeManager - system type manager
/// @param pSystemName - name of the system
/// @param pDefaultFriendlyName - default friendly name
/// @param serialLengthBytes - length of the serial number
/// @param pSerialMagicStr - magic string for the serial number
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
/// @brief Pre-setup - called before all other modules setup
void SysManager::preSetup()
{
    // Override system name if it is specified in the config
    _systemName = _systemConfig.getString("SystemName", _systemName.c_str());

    // System config for this module
    RaftJsonPrefixed sysManConfig(_systemConfig, _moduleName.c_str());

    // System friendly name (may be overridden by config)
    _defaultFriendlyName = sysManConfig.getString("DefaultName", _defaultFriendlyName.c_str());

    // Prime the mutable config info
    _mutableConfigCache.friendlyName = _mutableConfig.getString("friendlyName", "");
    _mutableConfigCache.friendlyNameIsSet = _mutableConfig.getBool("nameSet", 0);
    _mutableConfigCache.serialNo = _mutableConfig.getString("serialNo", "");

    // SysMod looping
    _loopAllSysMods = sysManConfig.getBool("loopAllSysMods", true);
    _loopSleepMs = sysManConfig.getLong("loopSleepMs", LOOP_SLEEP_MS_DEFAULT);
    _supervisorEnable = sysManConfig.getBool("supervisorEnable", true);
    _slowSysModThresholdUs = sysManConfig.getLong("slowSysModMs", SLOW_SYS_MOD_THRESHOLD_MS_DEFAULT) * 1000;
    _reportSlowSysMod = _supervisorEnable ? sysManConfig.getBool("reportSlowSysMod", true) : false;

    // Monitoring period and monitoring timer
    _monitorPeriodMs = sysManConfig.getLong("monitorPeriodMs", 10000);
    _monitorTimerMs = millis();
    _reportEnable = sysManConfig.getBool("reportEnable", true);
    sysManConfig.getArrayElems("reportList", _monitorReportList);

    // System restart flag
    _systemRestartMs = millis();

    // System unique string - use BT MAC address
#ifdef ESP_PLATFORM
    _systemUniqueString = getSystemMACAddressStr(ESP_MAC_BT, "");
#else
    _systemUniqueString = "TEST";
#endif

    // Reboot after N hours
    _rebootAfterNHours = sysManConfig.getLong("rebootAfterNHours", 0);

    // Reboot if disconnected for N minutes
    _rebootIfDiscMins = sysManConfig.getLong("rebootIfDiscMins", 0);

    // Pause WiFi for BLE
    _pauseWiFiForBLE = sysManConfig.getBool("pauseWiFiforBLE", 0);

    // Get friendly name
    bool friendlyNameIsSet = false;
    String friendlyName = getFriendlyName(friendlyNameIsSet);

    // Alternate Hardware revision reporting prefix
    _altHardwareRevisionPrefix = sysManConfig.getString("altHwPrefix", _altHardwareRevisionPrefix.c_str());

    // Debug
    LOG_I(MODULE_PREFIX, "systemName %s systemVersion %s friendlyName %s (default %s) serialNo %s nvsNamespace %s",
                _systemName.c_str(),
                platform_getAppVersion().c_str(),
                (friendlyName + (friendlyNameIsSet ? " (user-set)" : "")).c_str(),
                _defaultFriendlyName.c_str(),
                _mutableConfigCache.serialNo.isEmpty() ? "<<NONE>>" : _mutableConfigCache.serialNo.c_str(),
#ifdef ESP_PLATFORM        
                _mutableConfig.getNVSNamespace().c_str()
#else
                "N/A"
#endif
            );
    LOG_I(MODULE_PREFIX, "loopSleepMs %d slowSysModThresholdUs %d monitorPeriodMs %d rebootAfterNHours %d rebootIfDiscMins %d supervisorEnable %s systemUniqueString %s altHwPrefix %s",
                _loopSleepMs,
                _slowSysModThresholdUs,
                _monitorPeriodMs,
                _rebootAfterNHours, 
                _rebootIfDiscMins,
                _supervisorEnable ? "Y" : "N",
                _systemUniqueString.c_str(),
                _altHardwareRevisionPrefix.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Post-setup - called after all modules have been created
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
                "Set loop delay, e.g. ?delayMs=10&skipCount=1, 10ms delay alternately");
        _pRestAPIEndpointManager->addEndpoint("sysman", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                std::bind(&SysManager::apiSysManSettings, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                "Set SysMan, e.g. sysman?interval=2&rxBuf=10240");
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
            RaftSysMod* pSysMod = getSysMod(sysModClassDef.name.c_str());
            if (pSysMod)
            {
#ifdef DEBUG_SYSMOD_FACTORY
                LOG_I(MODULE_PREFIX, "postSetup %s alreadyCreated", sysModClassDef.name.c_str());
#endif
                continue;
            }

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
/// @brief Loop (called from main thread's endless loop)
void SysManager::loop()
{
    // Check if sysmod list is dirty
    if (_sysmodListDirty)
    {
        // Ideally this operation should be atomic but we assume currently system modules
        // are only added before the main program loop is entered
        sysModListSetup();
        _sysmodListDirty = false;
    }

    // Check if supervisor is enabled
    if (_supervisorEnable)
    {        
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

        // Monitor how long it takes to go around loop
        _supervisorStats.outerLoopStarted();
    }

    // Check the index into list of sys-mods to loop over is valid
    uint32_t numSysMods = _sysModLoopVector.size();
    if (numSysMods == 0)
        return;
    if (_loopCurModIdx >= numSysMods)
        _loopCurModIdx = 0;

    // Loop over sysmods
    for (uint32_t loopIdx = 0; loopIdx < numSysMods; loopIdx++)
    {
        if (_sysModLoopVector[_loopCurModIdx])
        {
#ifdef DEBUG_USING_GLOBAL_VALUES
            __loggerGlobalDebugValueSysMan = _loopCurModIdx;
#endif
            // Check if the SysMod slow check is enabled
            if (_reportSlowSysMod)
            {
                // Start time
                uint64_t sysModExecStartUs = micros();

                // Call the SysMod's loop method to allow code inside the module to run
                _supervisorStats.execStarted(_loopCurModIdx);
                _sysModLoopVector[_loopCurModIdx]->loop();
                _supervisorStats.execEnded(_loopCurModIdx);

                uint64_t sysModLoopUs = micros() - sysModExecStartUs;
                if (sysModLoopUs > _slowSysModThresholdUs)
                {
                    LOG_W(MODULE_PREFIX, "loop sysMod %s SLOW took %" PRIu64 "ms", 
                                _sysModLoopVector[_loopCurModIdx]->modName(), sysModLoopUs/1000);
                }
            }
            else
            {
                // Call the SysMod's loop method to allow code inside the module to run
                if (_supervisorEnable)
                    _supervisorStats.execStarted(_loopCurModIdx);
                _sysModLoopVector[_loopCurModIdx]->loop();
                if (_supervisorEnable)
                    _supervisorStats.execEnded(_loopCurModIdx);
            }
#ifdef DEBUG_USING_GLOBAL_VALUES
            __loggerGlobalDebugValueSysMan = -2;
#endif
        }

        // Next SysMod
        _loopCurModIdx++;

        // Check if looping over all sysmods (otherwise just do one)
        if (!_loopAllSysMods)
            break;
    }

    // Debug
    // LOG_D(MODULE_PREFIX, "loop module %s", sysModInfo._pSysMod->modName());

    // Check system restart pending
    if (_systemRestartPending)
    {
        if (Raft::isTimeout(millis(), _systemRestartMs, SYSTEM_RESTART_DELAY_MS))
        {
            _systemRestartPending = false;
            systemRestartNow();
        }
    }

    // End loop
    if (_supervisorEnable)
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
        systemRestartNow();
    }

    // Check for reboot after N mins if disconnected
    if (_rebootIfDiscMins != 0)
    {
#ifdef ESP_PLATFORM
        if (networkSystem.isIPConnected())
        {
            _rebootLastNetConnMs = millis();
        }
        else
#endif
        {
            if (Raft::isTimeout(millis(), _rebootLastNetConnMs, _rebootIfDiscMins * (uint64_t)60000))
            {
                LOG_I(MODULE_PREFIX, "Rebooting after %d mins disconnected", _rebootIfDiscMins);
                delay(500);
                systemRestartNow();
            }
        }
    }

    // Sleep the task
    if (_loopSleepMs > 0)
        delay(_loopSleepMs);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Add a managed SysMod
/// @param pSysMod - pointer to the SysMod
void SysManager::addManagedSysMod(RaftSysMod* pSysMod)
{
    // Avoid adding null pointers
    if (!pSysMod)
        return;

    // Add to module list
    _sysModuleList.push_back(pSysMod);

    // SysMod info now dirty
    _sysmodListDirty = true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get SysMod instance by name
/// @param pSysModName
/// @return Pointer to SysMod instance or nullptr if not found
RaftSysMod* SysManager::getSysMod(const char* pSysModName) const
{
    if (!pSysModName)
        return nullptr;
    // See if the sysmod is in the list
    for (RaftSysMod* pSysMod : _sysModuleList)
    {
        if (pSysMod->modNameStr().equals(pSysModName))
        {
            return pSysMod;
        }
    }
    return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup SysMod list
void SysManager::sysModListSetup()
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
/// @brief Add status change callback on a SysMod
/// @param sysModName - name of the SysMod
/// @param statusChangeCB - callback function
void SysManager::setStatusChangeCB(const char* sysModName, SysMod_statusChangeCB statusChangeCB)
{
    // Get SysMod
    RaftSysMod* pSysMod = getSysMod(sysModName);
    if (pSysMod)
    {
        // Debug
#ifdef DEBUG_STATUS_CHANGE_CALLBACK
        LOG_I(MODULE_PREFIX, "setStatusChangeCB sysMod %s cbValid %d sysModFound OK", sysModName, statusChangeCB != nullptr);
#endif
        return pSysMod->setStatusChangeCB(statusChangeCB);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Clear all status change callbacks on all sysmods
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
/// @brief Get status from SysMod
/// @param sysModName - name of the SysMod
/// @return JSON string
String SysManager::getStatusJSON(const char* sysModName) const
{
    // Get SysMod
    RaftSysMod* pSysMod = getSysMod(sysModName);
    if (pSysMod)
            return pSysMod->getStatusJSON();
    return "{}";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get debugStr from SysMod
/// @param sysModName - name of the SysMod
/// @return JSON string
String SysManager::getDebugJSON(const char* sysModName) const
{
    // Check if it is the SysManager's own stats that are wanted
    if (strcasecmp(sysModName, "SysMan") == 0)
        return _supervisorStats.getSummaryString();

#ifdef DEBUG_USING_GLOBAL_VALUES
    // Check for global debug values
    if (strcasecmp(sysModName, "Globs") == 0)
    {
        return Raft::getDebugGlobalsJson(false);
    }
#endif

    // Check for stats callback
    if (strcasecmp(sysModName, "StatsCB") == 0)
    {
        if (_statsCB)
            return _statsCB();
        return "";
    }

    // Get SysMod
    RaftSysMod* pSysMod = getSysMod(sysModName);
    if (pSysMod)
        return pSysMod->getDebugJSON();
    return "{}";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Notify of system shutdown
/// @param isRestart True if this is a restart (false if shutdown)
/// @param reasonOrNull Reason for shutdown (may be nullptr)
void SysManager::notifyOfShutdown(bool isRestart, const char* reasonOrNull)
{
    // Notify event
    String shutdownCmdAndReason = "{\"msgType\":\"sysevent\",\"msgName\":\"shutdown\",\"isRestart\":";
    shutdownCmdAndReason += isRestart ? "1" : "0";
    if (reasonOrNull)
    {
        shutdownCmdAndReason += ",\"reason\":\"";
        shutdownCmdAndReason += reasonOrNull;
        shutdownCmdAndReason += "\"";
    }
    shutdownCmdAndReason += "}";

    // Send report
    sendReportMessage(shutdownCmdAndReason.c_str());

    // Log
    LOG_I(MODULE_PREFIX, "notifyOfShutdown isRestart %s reason %s", 
            isRestart ? "YES" : "NO",
            reasonOrNull ? reasonOrNull : "N/A");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Send command to one or all SysMods
/// @param sysModName Name of SysMod to send command to or nullptr for all SysMods
/// @param cmdJSON Command JSON string
/// @return Result code
/// @note The command JSON string should be in the format:
///       {"cmd":"<command>",...other args...}
///       where <command> is the command to be sent and other args are any additional arguments
///       to be passed to the command handler.
///       The command will be sent to the SysMod's command handler.
///       The SysMod should handle the command and return a result.
RaftRetCode SysManager::sendCmdJSON(const char* sysModName, const char* cmdJSON)
{
    // Check if SysMod name is null or empty
    if (sysModName == nullptr || sysModName[0] == '\0')
    {
        RaftRetCode rslt = RAFT_OK;
        // Loop over all SysMods
        for (RaftSysMod* pSysMod : _sysModuleList)
        {
            if (pSysMod)
            {
                RaftRetCode tmpRslt = pSysMod->receiveCmdJSON(cmdJSON);
                if (rslt == RAFT_OK)
                    rslt = tmpRslt;
            }
        }
        return rslt;
    }

    // Performance test
#ifdef DEBUG_SEND_CMD_JSON_PERF
    uint64_t startUs = micros();
#endif
    
    // Check if this is a SysManager command
    if (strcasecmp(sysModName, "SysMan") == 0)
    {
        // Send report
        sendReportMessage(cmdJSON);
        return RAFT_OK;
    }
    
    // Get SysMod
    RaftSysMod* pSysMod = getSysMod(sysModName);
    if (pSysMod)
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
#ifdef DEBUG_SEND_CMD_JSON_PERF
    LOG_I(MODULE_PREFIX, "getHWElemByName %s NOT found in %dus", sysModName, int(micros() - startUs));
#endif
    return RAFT_INVALID_OPERATION;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get named value from SysMod
/// @param pSysModName (nullptr for SysManager)
/// @param valueName 
/// @param isValid 
/// @return 
double SysManager::getNamedValue(const char* pSysModName, const char* valueName, bool& isValid) const
{
    // Check for SysManager named values
    if (!pSysModName)
    {
        if (strcasecmp(valueName, "FriendlyNameIsSet") == 0)
        {
            isValid = true;
            return _mutableConfig.getLong("nameSet", 0) ? 1.0 : 0.0;
        }
        else if (strcasecmp(valueName, "IsSystemMainFWUpdate") == 0)
        {
            isValid = true;
            return _isSystemMainFWUpdate ? 1.0 : 0.0;
        }
        else if (strcasecmp(valueName, "IsSystemFileTransferring") == 0)
        {
            isValid = true;
            return _isSystemFileTransferring ? 1.0 : 0.0;
        }
        else if (strcasecmp(valueName, "IsSystemStreaming") == 0)
        {
            isValid = true;
            return _isSystemStreaming ? 1.0 : 0.0;
        }
        isValid = false;
        return 0;
    }
    // Get SysMod
    RaftSysMod* pSysMod = getSysMod(pSysModName);
    if (pSysMod)
            return pSysMod->getNamedValue(valueName, isValid);
    isValid = false;
    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set named value in SysMod
/// @param pSysModName (nullptr for SysManager)
/// @param valueName
/// @param value
/// @return true if set
bool SysManager::setNamedValue(const char* pSysModName, const char* valueName, double value)
{
    // Check for SysManager
    if (!pSysModName)
    {
        if (strcasecmp(valueName, "AutoSetHostname") == 0)
        {
            _autoSetHostname = value != 0.0;
        }
    }

    // Get SysMod
    RaftSysMod* pSysMod = getSysMod(pSysModName);
    if (pSysMod)
        return pSysMod->setNamedValue(valueName, value);
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get named value string from SysMod
/// @param pSysModName (nullptr for SysManager)
/// @param valueName
/// @param isValid
/// @return string
String SysManager::getNamedString(const char* pSysModName, const char* valueName, bool& isValid) const
{
    // Check for SysManager named values
    if (!pSysModName)
    {
        if (strcasecmp(valueName, "FriendlyName") == 0)
        {
            return getFriendlyName(isValid);
        }
        else if (strcasecmp(valueName, "SerialNumber") == 0)
        {
            isValid = true;
            return _mutableConfig.getString("serialNo", "");
        }
        else if (strcasecmp(valueName, "SystemVersion") == 0)
        {
            isValid = true;
            return platform_getAppVersion();
        }
        else if (strcasecmp(valueName, "SystemName") == 0)
        {
            isValid = true;
            return _systemName;
        }
        else if (strcasecmp(valueName, "Manufacturer") == 0)
        {
            isValid = true;
            return _systemConfig.getString("Manufacturer", "");
        }
        else if (strcasecmp(valueName, "BaseSysTypeVersion") == 0)
        {
            isValid = true;
            return _sysTypeManager.getBaseSysTypeVersion();
        }
        else if (strcasecmp(valueName, "SystemUniqueString") == 0)
        {
            isValid = true;
            return _systemUniqueString;
        }
        isValid = false;
        return "";
    }

    // Get SysMod
    RaftSysMod* pSysMod = getSysMod(pSysModName);
    if (pSysMod)
        return pSysMod->getNamedString(valueName, isValid);
    isValid = false;
    return "";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set named value string in SysMod
/// @param pSysModName (nullptr for SysManager)
/// @param valueName
/// @param value
/// @return true if set
bool SysManager::setNamedString(const char* pSysModName, const char* valueName, const char* value)
{
    // Check for SysManager named values
    if (!pSysModName)
    {
        if (strcasecmp(valueName, "FriendlyName") == 0)
        {
            setFriendlyName(value);
        }
        else if (strcasecmp(valueName, "BaseSysTypeVersion") == 0)
        {
            _sysTypeManager.setBaseSysTypeVersion(value);
            return true;
        }
        else if (strcasecmp(valueName, "SystemUniqueString") == 0)
        {
            _systemUniqueString = value;
            return true;
        }
        return false;
    }

    // Get SysMod
    RaftSysMod* pSysMod = getSysMod(pSysModName);
    if (pSysMod)
        return pSysMod->setNamedString(valueName, value);
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Register data source (message generator) with SysMod
/// @param sysModName
/// @param pubTopic
/// @param msgGenCB
/// @param stateDetectCB
/// @return Allocated topic index, or UINT16_MAX on failure
uint16_t SysManager::registerDataSource(const char* sysModName, const char* pubTopic, SysMod_publishMsgGenFn msgGenCB, SysMod_stateDetectCB stateDetectCB)
{
    // Get SysMod
    RaftSysMod* pSysMod = getSysMod(sysModName);
    if (pSysMod)
    {
        uint16_t topicIndex = pSysMod->registerDataSource(pubTopic, msgGenCB, stateDetectCB);
#ifdef DEBUG_REGISTER_MSG_GEN_CB
        LOG_I(MODULE_PREFIX, "registerDataSource %s topic %s with the %s sysmod topicIdx %d", 
              topicIndex != UINT16_MAX ? "OK" : "FAILED", pubTopic, sysModName, (int)topicIndex);
#endif
        return topicIndex;
    }
#ifdef DEBUG_REGISTER_MSG_GEN_CB
    LOG_W(MODULE_PREFIX, "registerDataSource NOT FOUND %s topic %s", sysModName, pubTopic);
#endif
    return UINT16_MAX;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Send report message
void SysManager::sendReportMessage(const char* msg)
{
    // Check comms core
    if (!getCommsCore() || !msg)
        return;

    // Endpoint message we're going to send
    CommsChannelMsg endpointMsg(MSG_CHANNEL_ID_ALL, MSG_PROTOCOL_RICREST, 0, MSG_TYPE_REPORT);

    // Generate message
    RICRESTMsg::encode(msg, endpointMsg, RICRESTMsg::RICREST_ELEM_CODE_CMDRESPJSON);

#ifdef DEBUG_SEND_REPORT_MESSAGE
    LOG_I(MODULE_PREFIX, "sendReportMessage payload %s", msg);
#endif

    // Send message
#ifdef DEBUG_SEND_REPORT_MESSAGE
    CommsCoreRetCode retc =
#endif 
    getCommsCore()->outboundHandleMsg(endpointMsg);

#ifdef DEBUG_SEND_REPORT_MESSAGE
    if (retc != CommsCoreRetCode::COMMS_CORE_RET_OK)
    {
        LOG_W(MODULE_PREFIX, "sendReportMessage failed %d", retc);
    }
    else
    {
        LOG_I(MODULE_PREFIX, "sendReportMessage OK");
    }
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief API for resetting the system
/// @param reqStr
/// @param respStr
/// @param sourceInfo
/// @return response code
RaftRetCode SysManager::apiReset(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Register that a restart is required but don't restart immediately
    // as the acknowledgement would not get through
    systemRestart();

    // Result
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief API for getting system version information
/// @param reqStr
/// @param respStr
/// @param sourceInfo
/// @return response code
RaftRetCode SysManager::apiGetVersion(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Get serial number
    String serialNo = _mutableConfig.getString("serialNo", "");
    char versionJson[225];
    // Friendly name
    bool friendlyNameIsSet = false;
    String friendlyName = getFriendlyName(friendlyNameIsSet);
    // Hardware revision Json
    String hwRevJson = getBaseSysVersJson();
    snprintf(versionJson, sizeof(versionJson),
             R"({"req":"%s","rslt":"ok","SystemName":"%s","SystemVersion":"%s","Friendly":"%s","SerialNo":"%s",)"
            R"("MAC":"%s",%s})",
             reqStr.c_str(), 
             _systemName.c_str(), 
             platform_getAppVersion().c_str(),
             friendlyNameIsSet ? friendlyName.c_str() : "",
             serialNo.c_str(),
             _systemUniqueString.c_str(),
            hwRevJson.c_str());
    respStr = versionJson;

#ifdef DEBUG_API_ENDPOINTS
    LOG_I(MODULE_PREFIX, "apiGetVersion %s", respStr.c_str());
#endif

    return RaftRetCode::RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief API for getting system manager information
/// @param reqStr
/// @param respStr
/// @param sourceInfo
/// @return response code
RaftRetCode SysManager::apiGetSysModInfo(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Get name of SysMod
    String sysModName = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);
    respStr = getStatusJSON(sysModName.c_str());
    return RaftRetCode::RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief API for getting system manager debug information
/// @param reqStr
/// @param respStr
/// @param sourceInfo
/// @return response code
RaftRetCode SysManager::apiGetSysModDebug(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Get name of SysMod
    String sysModName = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);
    String debugStr = "\"debug\":" + getDebugJSON(sysModName.c_str());
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, debugStr.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief API for getting friendly name
/// @param reqStr
/// @param respStr
/// @param sourceInfo
/// @return response code
RaftRetCode SysManager::apiFriendlyName(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Check if we're setting
    if (RestAPIEndpointManager::getNumArgs(reqStr.c_str()) > 1)
    {
        // Set name
        String friendlyName = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);
        bool rslt = setFriendlyName(friendlyName.c_str(), true);
        if (!rslt)
        {
            Raft::setJsonErrorResult(reqStr.c_str(), respStr, "");
            return RaftRetCode::RAFT_INVALID_DATA;
        }
    }

    // Get current
    bool friendlyNameIsSet = false;
    String friendlyName = getFriendlyName(friendlyNameIsSet);
    LOG_I(MODULE_PREFIX, "apiFriendlyName -> %s, friendlyNameIsSet %s", 
                friendlyName.c_str(), friendlyNameIsSet ? "Y" : "N");

    // Create response JSON
    String rsltStr = Raft::formatString(100, R"("friendlyName":"%s","friendlyNameIsSet":%s)", 
                friendlyName.c_str(), friendlyNameIsSet ? "true" : "false");
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, rsltStr.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief API for getting and setting serial number
/// @param reqStr
/// @param respStr
/// @param sourceInfo
/// @return response code
RaftRetCode SysManager::apiSerialNumber(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Check if we're setting
    if (RestAPIEndpointManager::getNumArgs(reqStr.c_str()) > 1)
    {
        // Get serial number to set
        String serialNoHexStr = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);
        uint8_t serialNumBuf[_serialLengthBytes];
        uint32_t serialNoLen = Raft::getBytesFromHexStr(serialNoHexStr.c_str(), serialNumBuf, _serialLengthBytes);
        if (serialNoLen != _serialLengthBytes)
        {
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "SNNot16Byt");
        }

        // Validate magic string if required
        if (_serialMagicStr.length() > 0)
        {
            String magicString;
            if (RestAPIEndpointManager::getNumArgs(reqStr.c_str()) > 2)
            {
                magicString = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 2);
                if (!magicString.equals(_serialMagicStr))
                {
                    return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "SNMagicInvalid");
                }
            }
            else
            {
                return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "SNNeedsMagic");
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
    String jsonResult = R"("SerialNo":")" + serialNo + "\"";
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, jsonResult.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief API for getting base system type and version
/// @param reqStr
/// @param respStr
/// @param sourceInfo
/// @return response code
RaftRetCode SysManager::apiBaseSysTypeVersion(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Create response JSON
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, getBaseSysVersJson().c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief API test set loop delay
/// @param reqStr
/// @param respStr
/// @param sourceInfo
/// @return response code
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
/// @brief API for setting system manager settings
/// @param reqStr
/// @param respStr
/// @param sourceInfo
/// @return response code
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
/// @brief get mutable config JSON
/// @return JSON string
String SysManager::getMutableConfigJson()
{
    return Raft::formatString(100,
                 R"({"friendlyName":"%s","nameSet":%d,"serialNo":"%s"})",
                _mutableConfigCache.friendlyName.c_str(), 
                _mutableConfigCache.friendlyNameIsSet, 
                _mutableConfigCache.serialNo.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief show stats
void SysManager::statsShow()
{
    // Check enabled
    if (!_reportEnable)
        return;

    // Generate stats
    char statsStr[200];
    String friendlyNameStr;
    if (_mutableConfigCache.friendlyNameIsSet)
        friendlyNameStr = "\"f\":\"" + _mutableConfigCache.friendlyName + "\",";
    snprintf(statsStr, sizeof(statsStr), R"({%s"n":"%s","v":"%s","r":"%s","hpInt":%d,"hpMin":%d,"hpAll":%d)", 
                friendlyNameStr.c_str(),
                _systemName.c_str(),
                platform_getAppVersion().c_str(),
                _sysTypeManager.getBaseSysTypeVersion().c_str(),
#ifdef ESP_PLATFORM
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                heap_caps_get_free_size(MALLOC_CAP_8BIT)
#else
                0, 0, 0
#endif
            );

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
/// @brief get friendly name
/// @param (out) isSet
/// @return friendly name
String SysManager::getFriendlyName(bool& isSet) const
{
    // Check if set
    isSet =  _mutableConfig.getLong("nameSet", 0);

    // Handle default naming
    String friendlyNameNVS = _mutableConfig.getString("friendlyName", "");
    String friendlyName = friendlyNameNVS;
    if (!isSet || friendlyName.isEmpty())
    {
        friendlyName = _defaultFriendlyName;
        if (_systemUniqueString.length() >= 6)
            friendlyName += "_" + _systemUniqueString.substring(_systemUniqueString.length()-6);
    }
#ifdef DEBUG_GET_FRIENDLY_NAME
    LOG_I(MODULE_PREFIX, "getFriendlyName %s (isSet %s nvsStr %s default %s uniqueStr %s)", 
                friendlyName.c_str(), 
                isSet ? "Y" : "N",
                friendlyNameNVS.c_str(),
                _defaultFriendlyName.c_str(), 
                _systemUniqueString.c_str());
#endif
    return friendlyName;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief set friendly name
/// @param friendlyName
/// @param forceSetHostname
/// @return true if set
bool SysManager::setFriendlyName(const String& friendlyName, bool forceSetHostname)
{
    // Update cached name
    _mutableConfigCache.friendlyName = friendlyName;
    _mutableConfigCache.friendlyName.trim();
    _mutableConfigCache.friendlyName = _mutableConfigCache.friendlyName.substring(0, MAX_FRIENDLY_NAME_LENGTH);
    _mutableConfigCache.friendlyNameIsSet = !friendlyName.isEmpty();

    // Debug
#ifdef DEBUG_FRIENDLY_NAME_SET
    if (_mutableConfigCache.friendlyNameIsSet)
    {
        LOG_I(MODULE_PREFIX, "setFriendlyName %s", _mutableConfigCache.friendlyName.c_str());
    }
    else
    {
        LOG_I(MODULE_PREFIX, "setFriendlyName blank");
    }
#endif

    // Setup network system hostname
#ifdef ESP_PLATFORM
    if (_mutableConfigCache.friendlyNameIsSet && (_autoSetHostname || forceSetHostname))
        networkSystem.setHostname(_mutableConfigCache.friendlyName.c_str());
#endif

    // Store the new name (even if it is blank)
    _mutableConfig.setJsonDoc(getMutableConfigJson().c_str());
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief status change BLE connection callback
/// @param sysModName
/// @param changeToOnline
void SysManager::statusChangeBLEConnCB(const String& sysModName, bool changeToOnline)
{
    // Check if WiFi should be paused
    LOG_I(MODULE_PREFIX, "BLE connection change isConn %s", changeToOnline ? "YES" : "NO");
    if (_pauseWiFiForBLE)
    {
#ifdef ESP_PLATFORM
        networkSystem.pauseWiFi(changeToOnline);
#endif
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief get base system type and version JSON
/// @return JSON string
String SysManager::getBaseSysVersJson()
{
    // Get base SysType version string (if all digits then set in JSON as a number for backward compatibility)
    String baseSysTypeVersStr = _sysTypeManager.getBaseSysTypeVersion();
    String hWRevStr = _sysTypeManager.getBaseSysTypeVersion();
    bool allDigits = true;
    for (int i = 0; i < hWRevStr.length(); i++)
    {
        if (!isdigit(hWRevStr.charAt(i)))
        {
            allDigits = false;
            break;
        }
    }
    if ((hWRevStr.length() == 0) || !allDigits)
        hWRevStr = "\"" + hWRevStr + "\"";

    // Alt hardware rev string
    String altHwRevStr = "";
    if (_altHardwareRevisionPrefix.length() > 0)
    {
        altHwRevStr = ",\"" + _altHardwareRevisionPrefix + "\":" + hWRevStr;
    }
    // Form JSON
    return "\"SysTypeVers\":\"" + baseSysTypeVersStr + "\",\"HwRev\":" + hWRevStr + altHwRevStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief check if system module dependencies are satisfied
/// @param sysModClassDef
/// @return true if all dependencies satisfied
bool SysManager::checkSysModDependenciesSatisfied(const SysModFactory::SysModClassDef& sysModClassDef)
{
    // Check if all dependencies are satisfied
    for (auto& dependency : sysModClassDef.dependencyList)
    {
        // See if the sysmod is in the list of SysMods
        RaftSysMod* pSysMod = getSysMod(dependency.c_str());
        if (!pSysMod)
            return false;
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief system restart
void SysManager::systemRestartNow()
{
    #ifdef ESP_PLATFORM
    esp_restart();
#else
    LOG_I(MODULE_PREFIX, "------------------------- System restart ------------------------------");
#endif
}
