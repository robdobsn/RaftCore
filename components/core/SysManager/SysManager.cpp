/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Manager for SysMods (System Modules)
// All modules that are core to the system should be derived from SysModBase
// These modules are then serviced by this manager's service function
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
#include "SysModBase.h"
#include "RaftJsonNVS.h"
#include "RestAPIEndpointManager.h"
#include "RaftUtils.h"
#include "ESPUtils.h"
#include "NetworkSystem.h"
#include "DebugGlobals.h"

// Log prefix
static const char *MODULE_PREFIX = "SysMan";

// #define ONLY_ONE_MODULE_PER_SERVICE_LOOP 1

// Warn
#define WARN_ON_SYSMOD_SLOW_SERVICE

// Debug supervisor step (for hangup detection within a service call)
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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SysManager::SysManager(const char* pModuleName,
                RaftJsonIF& systemConfig,
                const char* pDefaultFriendlyName,
                const char* pSystemHWName,
                uint32_t serialLengthBytes, 
            const String& serialMagicStr,
            const char* pSysManagerNVSNamespace) :
                            _systemConfig(systemConfig),
                            _mutableConfig(pSysManagerNVSNamespace)
{
    // Module name
    _moduleName = pModuleName;

    // Set serial length and magic string
    _serialLengthBytes = serialLengthBytes;
    _serialMagicStr = serialMagicStr;

    // Register this manager to all objects derived from SysModBase
    SysModBase::setSysManager(this);

    // Extract system name from root level of config
    _systemName = _systemConfig.getString("SystemName", pSystemHWName);
    _systemVersion = _systemConfig.getString("SystemVersion", "0.0.0");

    // System config for this module
    RaftJsonPrefixed sysManConfig(_systemConfig, pModuleName);

    // System friendly name
    _defaultFriendlyName = sysManConfig.getString("DefaultName", pDefaultFriendlyName);

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
    LOG_I(MODULE_PREFIX, "friendlyName %s rebootAfterNHours %d rebootIfDiscMins %d slowSysModUs %d serialNo %s (defaultFriendlyName %s)",
                (friendlyName + (friendlyNameIsSet ? " (user-set)" : "")).c_str(),
                _rebootAfterNHours, _rebootIfDiscMins,
                _slowSysModThresholdUs,
                _mutableConfigCache.serialNo.isEmpty() ? "<<NONE>>" : _mutableConfigCache.serialNo.c_str(),
                _defaultFriendlyName.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SysManager::setup()
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
                std::bind(&SysManager::apiHwRevisionNumber, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                "HW revision number");
        _pRestAPIEndpointManager->addEndpoint("testsetloopdelay", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                std::bind(&SysManager::apiTestSetLoopDelay, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                "Set a loop delay to test resilience, e.g. ?delayMs=10&skipCount=1, applies 10ms delay to alternate loops");
        _pRestAPIEndpointManager->addEndpoint("sysman", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                std::bind(&SysManager::apiSysManSettings, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                "Set SysMan settings, e.g. sysman?interval=2&rxBuf=10240");
    }

    // Short delay here to allow logging output to complete as some hardware configurations
    // require changes to serial uarts and this disturbs the logging flow
    delay(100);

    // Now call setup on system modules
    for (SysModBase* pSysMod : _sysModuleList)
    {
#ifdef DEBUG_SYSMOD_MEMORY_USAGE
        uint32_t heapBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
        if (pSysMod)
            pSysMod->setup();
#ifdef DEBUG_SYSMOD_MEMORY_USAGE
        uint32_t heapAfter = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        ESP_LOGI(MODULE_PREFIX, "%s setup heap before %d after %d diff %d", 
                pSysMod->modName(), heapBefore, heapAfter, heapBefore - heapAfter);
#endif
    }

    // Give each SysMod the opportunity to add endpoints and comms channels and to keep a
    // pointer to the CommsCoreIF that can be used to send messages
    for (SysModBase* pSysMod : _sysModuleList)
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
    for (SysModBase* pSysMod : _sysModuleList)
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
    for (SysModBase* pSysMod : _sysModuleList)
    {
        LOG_I(MODULE_PREFIX, "SysMod %d: %s", sysModIdx++, 
                pSysMod ? pSysMod->modName() : "UNKNOWN");
            
    }
#endif

    // Set last time connected for rebbot if disconnected check
    _rebootLastNetConnMs = millis();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SysManager::service()
{
    // Check if supervisory info is dirty
    if (_supervisorDirty)
    {
        // Ideally this operation should be atomic but we assume currently system modules
        // are only added before the main program loop is entered
        supervisorSetup();
        _supervisorDirty = false;
    }
       
    // Service monitor periodically records timing
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

    // Check the index into list of sys-mods to service is valid
    uint32_t numSysMods = _sysModServiceVector.size();
    if (numSysMods == 0)
        return;
    if (_serviceLoopCurModIdx >= numSysMods)
        _serviceLoopCurModIdx = 0;

    // Monitor how long it takes to go around loop
    _supervisorStats.outerLoopStarted();

#ifndef ONLY_ONE_MODULE_PER_SERVICE_LOOP
    for (_serviceLoopCurModIdx = 0; _serviceLoopCurModIdx < numSysMods; _serviceLoopCurModIdx++)
    {
#endif

    if (_sysModServiceVector[_serviceLoopCurModIdx])
    {
#ifdef DEBUG_SYSMOD_WITH_GLOBAL_VALUE
        DEBUG_GLOB_VAR_NAME(DEBUG_GLOB_SYSMAN) = _serviceLoopCurModIdx;
#endif
#ifdef WARN_ON_SYSMOD_SLOW_SERVICE
        uint64_t sysModExecStartUs = micros();
#endif

        // Service SysMod
        _supervisorStats.execStarted(_serviceLoopCurModIdx);
        _sysModServiceVector[_serviceLoopCurModIdx]->service();
        _supervisorStats.execEnded(_serviceLoopCurModIdx);

#ifdef WARN_ON_SYSMOD_SLOW_SERVICE
        uint64_t sysModServiceUs = micros() - sysModExecStartUs;
        if (sysModServiceUs > _slowSysModThresholdUs)
        {
            LOG_W(MODULE_PREFIX, "service sysMod %s SLOW took %lldms", _sysModServiceVector[_serviceLoopCurModIdx]->modName(), sysModServiceUs/1000);
        }
#endif
    }

#ifndef ONLY_ONE_MODULE_PER_SERVICE_LOOP
    }
#endif

    // Debug
    // LOG_D(MODULE_PREFIX, "Service module %s", sysModInfo._pSysMod->modName());

    // Next SysMod
    _serviceLoopCurModIdx++;

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

void SysManager::add(SysModBase* pSysMod)
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
    _serviceLoopCurModIdx = 0;

    // Clear stats
    _supervisorStats.clear();

    // Clear and reserve sysmods from service vector
    _sysModServiceVector.clear();
    _sysModServiceVector.reserve(_sysModuleList.size());

    // Add modules to list and initialise stats
    for (SysModBase* pSysMod : _sysModuleList)
    {
        if (pSysMod)
        {
            _sysModServiceVector.push_back(pSysMod);
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
    for (SysModBase* pSysMod : _sysModuleList)
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
    for (SysModBase* pSysMod : _sysModuleList)
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
    for (SysModBase* pSysMod : _sysModuleList)
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
    for (SysModBase* pSysMod : _sysModuleList)
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
    for (SysModBase* pSysMod : _sysModuleList)
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
    for (SysModBase* pSysMod : _sysModuleList)
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
    for (SysModBase* pSysMod : _sysModuleList)
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
    snprintf(versionJson, sizeof(versionJson),
             R"({"req":"%s","rslt":"ok","SystemName":"%s","SystemVersion":"%s","SerialNo":"%s",)"
             R"("MAC":"%s","RicHwRevNo":%d,"HwRev":%d})",
             reqStr.c_str(), 
             _systemName.c_str(), 
             _systemVersion.c_str(), 
             serialNo.c_str(),
             _systemUniqueString.c_str(),
             _hwRevision,
             _hwRevision);
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
            if (!magicString.equals(_serialMagicStr))
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
// HW revision number
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode SysManager::apiHwRevisionNumber(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Create response JSON
    char jsonOut[50];
    snprintf(jsonOut, sizeof(jsonOut), R"("RicHwRevNo":%d,"HwRevNo":%d)", _hwRevision, _hwRevision);
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, jsonOut);
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
    snprintf(statsStr, sizeof(statsStr), R"({%s"n":"%s","v":"%s","r":%d,"hpInt":%d,"hpMin":%d,"hpAll":%d)", 
                friendlyNameStr.c_str(),
                _systemName.c_str(),
                _systemVersion.c_str(),
                _hwRevision,
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
    String friendlyName = _mutableConfig.getString("friendlyName", "");
    if (!isSet || friendlyName.isEmpty())
    {
        friendlyName = _defaultFriendlyName;
        LOG_I(MODULE_PREFIX, "getFriendlyName default %s uniqueStr %s", _defaultFriendlyName.c_str(), _systemUniqueString.c_str());
        if (_systemUniqueString.length() >= 6)
            friendlyName += "_" + _systemUniqueString.substring(_systemUniqueString.length()-6);
    }
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
