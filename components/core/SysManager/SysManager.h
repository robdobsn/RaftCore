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

#pragma once 

#include <list>
#include <vector>
#include "ExecTimer.h"
#include "SupervisorStats.h"
#include "SysModBase.h"
#include "RestAPIEndpointManager.h"
#include "RaftArduino.h"
#include "CommsCoreIF.h"
#include "RaftJsonNVS.h"
#include "SysModFactory.h"
#include "ProtocolExchange.h"

typedef String (*SysManager_statsCB)();

class RestAPIEndpointManager;

class SysManager
{
public:
    // Constructor
    SysManager(const char* pModuleName,
            RaftJsonIF& systemConfig,
            const String sysManagerNVSNamespace,
            const char* pSystemHWName = nullptr,
            const char* pDefaultFriendlyName = nullptr,
            uint32_t serialLengthBytes = DEFAULT_SERIAL_LEN_BYTES, 
            const char* pSerialMagicStr = nullptr);

    // Setup
    void setup();

    // Service
    void service();

    // Register SysMod with the SysMod factory
    void registerSysMod(const char* pClassName, SysModCreateFn pCreateFn, bool alwaysEnable = false, const char* pDependencyListCSV = nullptr)
    {
        _sysModFactory.registerSysMod(pClassName, pCreateFn, alwaysEnable, pDependencyListCSV);
    }

    // Add a pre-constructed SysMod to the managed list
    void addManagedSysMod(SysModBase* pSysMod);

    // Get system name
    String getSystemName()
    {
        return _systemName;
    }

    // Get system version
    String getSystemVersion()
    {
        return _systemVersion;
    }

    // Set hardware revision
    void setHwRevision(const char* pHwRevStr)
    {
        _hardwareRevision = pHwRevStr;
    }

    // Get hardware revision
    String getHwRevision()
    {
        return _hardwareRevision;
    }

    // Get friendly name
    String getFriendlyName(bool& isSet);
    bool getFriendlyNameIsSet();
    bool setFriendlyName(const String& friendlyName, bool setHostname, String& respStr);

    // Set system unique string
    void setSystemUniqueString(const char* sysUniqueStr)
    {
        _systemUniqueString = sysUniqueStr;
    }

    // Get system unique string
    String getSystemUniqueString()
    {
        return _systemUniqueString;
    }

    // Set stats callback (for SysManager's own stats)
    void setStatsCB(SysManager_statsCB statsCB)
    {
        _statsCB = statsCB;
    }

    // Add status change callback on a SysMod
    void setStatusChangeCB(const char* sysModName, SysMod_statusChangeCB statusChangeCB);

    // Get status from SysMod
    String getStatusJSON(const char* sysModName);

    // Get debug from SysMod
    String getDebugJSON(const char* sysModName);

    // Send command to SysMod
    RaftRetCode sendCmdJSON(const char* sysModName, const char* cmdJSON);

    // Send message-generator callback to SysMod
    void sendMsgGenCB(const char* sysModName, const char* msgGenID, SysMod_publishMsgGenFn msgGenCB, SysMod_stateDetectCB stateDetectCB);

    // Get named value 
    double getNamedValue(const char* sysModName, const char* valueName, bool& isValid);

    // Request system restart
    void systemRestart()
    {
        // Actual restart occurs within service routine after a short delay
        _systemRestartPending = true;
        _systemRestartMs = millis();
    }

    // REST API Endpoints
    void setRestAPIEndpoints(RestAPIEndpointManager& restAPIEndpoints)
    {
        _pRestAPIEndpointManager = &restAPIEndpoints;
    }

    RestAPIEndpointManager* getRestAPIEndpointManager()
    {
        return _pRestAPIEndpointManager;
    }

    // CommsCore
    void setCommsCore(CommsCoreIF* pCommsCore)
    {
        _pCommsCore = pCommsCore;
    }
    CommsCoreIF* getCommsCore()
    {
        return _pCommsCore;
    }

    // Protocol exchange
    void setProtocolExchange(ProtocolExchange* pProtocolExchange)
    {
        _pProtocolExchange = pProtocolExchange;
    }
    ProtocolExchange* getProtocolExchange()
    {
        return _pProtocolExchange;
    }

    // Get supervisor stats
    SupervisorStats* getStats()
    {
        return &_supervisorStats;
    }

    void informOfFileStreamActivity(bool isMainFWUpdate, bool isFileSystemActivity, bool isStreaming)
    {
        _isSystemMainFWUpdate = isMainFWUpdate;
        _isSystemFileTransferring = isFileSystemActivity;
        _isSystemStreaming = isStreaming;
    }

    // File/stream system activity - main FW update
    bool isSystemMainFWUpdate()
    {
        return _isSystemMainFWUpdate;
    }

    // File/stream system activity - streaming
    bool isSystemFileTransferring()
    {
        return _isSystemFileTransferring;
    }

    // File/stream system activity - streaming
    bool isSystemStreaming()
    {
        return _isSystemStreaming;
    }

    // Get SysConfig
    RaftJsonIF& getSysConfig()
    {
        return _systemConfig;
    }

    // Defaults
    static const uint32_t DEFAULT_SERIAL_LEN_BYTES = 16;

private:

    // Name of this module
    String _moduleName;

    // SysMod factory
    SysModFactory _sysModFactory;

    // Serial length and set magic string
    uint32_t _serialLengthBytes = DEFAULT_SERIAL_LEN_BYTES;
    String _serialMagicStr;

    // Service loop supervisor
    void supervisorSetup();
    bool _supervisorDirty = false;

    // Service loop
    std::vector<SysModBase*> _sysModServiceVector;
    uint32_t _serviceLoopCurModIdx = 0;

    // NOTE: _sysModuleList and _supervisorStats must be in synch
    //       when a module is added it must be added to both lists

    // List of modules
    std::list<SysModBase*> _sysModuleList;

    // Stress test loop delay
    uint32_t _stressTestLoopDelayMs = 0;
    uint32_t _stressTestLoopSkipCount = 0;
    uint32_t _stressTestCurSkipCount = 0;

    // Supervisor statistics
    SupervisorStats _supervisorStats;

    // Threshold of time for SysMod service considered too slow
    static const uint32_t SLOW_SYS_MOD_THRESHOLD_MS_DEFAULT = 50;
    uint32_t _slowSysModThresholdUs = SLOW_SYS_MOD_THRESHOLD_MS_DEFAULT * 1000;

    // Monitor timer and period
    unsigned long _monitorPeriodMs = 0;
    unsigned long _monitorTimerMs = 0;
    bool _monitorTimerStarted = false;
    bool _monitorShownFirstTime = false;
    static const unsigned long MONITOR_PERIOD_FIRST_SHOW_MS = 5000;
    std::vector<String> _monitorReportList;

    // Stats available callback
    SysManager_statsCB _statsCB = nullptr;

    // Stats
    void statsShow();

    // SysManager also handles system restarts
    bool _systemRestartPending = false;
    unsigned long _systemRestartMs = 0;
    static const int SYSTEM_RESTART_DELAY_MS = 1000;

    // Pause WiFi for BLE
    bool _pauseWiFiForBLE = false;

    // System name and version
    String _systemName;
    String _systemVersion;

    // Hardware revision
    String _hardwareRevision;

    // System config
    RaftJsonIF& _systemConfig;

    // Mutable (NVS) config (for this module)
    RaftJsonNVS _mutableConfig;

    // Mutable config
    struct
    {
        String friendlyName;
        bool friendlyNameIsSet = false;
        String serialNo;
    } _mutableConfigCache;

    // Default friendly name
    String _defaultFriendlyName;

    // Unique string for this system
    String _systemUniqueString;

    // File/stream activity
    bool _isSystemMainFWUpdate = false;
    bool _isSystemFileTransferring = false;
    bool _isSystemStreaming = false;
    
    // System reboot after N hours
    // If 0 then no reboot
    uint32_t _rebootAfterNHours = 0;

    // System reboot after N minutes of no network connection
    // If 0 then no reboot
    uint32_t _rebootIfDiscMins = 0;

    // Last time network connected
    uint32_t _rebootLastNetConnMs = 0;

    // Friendly name max len
    static const uint32_t MAX_FRIENDLY_NAME_LENGTH = 60;

    // Endpoints
    RestAPIEndpointManager* _pRestAPIEndpointManager = nullptr;

    // Comms core
    CommsCoreIF* _pCommsCore = nullptr;

    // Protocol exchange
    ProtocolExchange* _pProtocolExchange = nullptr;

    // API to reset system
    RaftRetCode apiReset(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    // Get system version
    RaftRetCode apiGetVersion(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    // Friendly name get/set
    RaftRetCode apiFriendlyName(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    // Serial no
    RaftRetCode apiSerialNumber(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    // Hardware revision
    RaftRetCode apiHwRevision(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    // SysMod info and debug
    RaftRetCode apiGetSysModInfo(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);
    RaftRetCode apiGetSysModDebug(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    // Test function to set loop delay
    RaftRetCode apiTestSetLoopDelay(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    // Setup SysMan diagnostics
    RaftRetCode apiSysManSettings(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    // Clear status change callbacks
    void clearAllStatusChangeCBs();

    // Connection change on BLE
    void statusChangeBLEConnCB(const String& sysModName, bool changeToOnline);

    // Mutable config
    String getMutableConfigJson();

    // Get hardware revision JSON
    String getHardwareRevisionJson();

    // Check SysMod dependency satisfied
    bool checkSysModDependenciesSatisfied(const SysModFactory::SysModClassDef& sysModClassDef);
};
