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

#pragma once 

#include "SysTypeManager.h"
#include "SysModFactory.h"
#include "NamedValueProvider.h"
#include "SupervisorStats.h"
#include "ProtocolExchange.h"
#include "DeviceManager.h"
#include "RaftJsonNVS.h"

typedef String (*SysManager_statsCB)();

class RaftSysMod;

class RestAPIEndpointManager;

class SysManager : public NamedValueProvider
{
public:
    // Constructor
    SysManager(const char* pModuleName,
            RaftJsonIF& systemConfig,
            const String sysManagerNVSNamespace,
            SysTypeManager& sysTypeManager,
            const char* pSystemName = nullptr,
            const char* pDefaultFriendlyName = nullptr,
            uint32_t serialLengthBytes = DEFAULT_SERIAL_LEN_BYTES, 
            const char* pSerialMagicStr = nullptr);

    // Pre-setup - called before all other modules setup
    void preSetup();

    // Post-setup - called after other modules setup (and to setup SysMods)
    void postSetup();

    // Loop (called from main loop)
    void loop();

    // Register SysMod with the SysMod factory
    void registerSysMod(const char* pClassName, SysModCreateFn pCreateFn, bool alwaysEnable = false, const char* pDependencyListCSV = nullptr)
    {
        _sysModFactory.registerSysMod(pClassName, pCreateFn, alwaysEnable, pDependencyListCSV);
    }

    // Add a pre-constructed SysMod to the managed list
    void addManagedSysMod(RaftSysMod* pSysMod);

    /// @brief Get SysMod instance by name
    /// @param sysModName
    /// @return Pointer to SysMod instance or nullptr if not found
    RaftSysMod* getSysMod(const char* sysModName) const;

    // Get system name
    String getSystemName() const
    {
        return _systemName;
    }

    // Get system version
    String getSystemVersion() const
    {
        return platform_getAppVersion();
    }

    // Get system serial number
    String getSystemSerialNo() const
    {
        return _mutableConfig.getString("serialNo", "");
    }

    // Get system manufacturer
    String getSystemManufacturer() const
    {
        return _systemConfig.getString("Manufacturer", "");
    }

    // Set base SysType version
    void setBaseSysTypeVersion(const char* pVersionStr)
    {
        _sysTypeManager.setBaseSysTypeVersion(pVersionStr);
    }

    // Get base SysType version
    String getBaseSysTypeVersion() const
    {
        return _sysTypeManager.getBaseSysTypeVersion();
    }

    // Get friendly name
    String getFriendlyName(bool& isSet) const;
    bool getFriendlyNameIsSet() const;
    bool setFriendlyName(const String& friendlyName, bool setHostname, String& respStr);

    // Set system unique string
    void setSystemUniqueString(const char* sysUniqueStr)
    {
        _systemUniqueString = sysUniqueStr;
    }

    // Get system unique string
    String getSystemUniqueString() const
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
    String getStatusJSON(const char* sysModName) const;

    // Get debug from SysMod
    String getDebugJSON(const char* sysModName) const;

    /// @brief Notify of system shutdown
    /// @param isRestart True if this is a restart (false if shutdown)
    /// @param reasonOrNull Reason for shutdown (may be nullptr)
    void notifyOfShutdown(bool isRestart = true, const char* reasonOrNull = nullptr);

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
    RaftRetCode sendCmdJSON(const char* sysModNameOrNullForAll, const char* cmdJSON);

    // Register data source (message generator functions)
    bool registerDataSource(const char* sysModName, const char* pubTopic, SysMod_publishMsgGenFn msgGenCB, SysMod_stateDetectCB stateDetectCB);

    // Get named value 
    virtual double getNamedValue(const char* sysModName, const char* valueName, bool& isValid) const override;

    // Set named value
    virtual bool setNamedValue(const char* sysModName, const char* valueName, double value) override;

    // Get named value string
    virtual String getNamedString(const char* sysModName, const char* valueName, bool& isValid) const override;

    // Set named value string
    virtual bool setNamedString(const char* sysModName, const char* valueName, const char* value) override;

    // Request system restart
    void systemRestart()
    {
        // Notify all SysMods of restart
        notifyOfShutdown(true);

        // Actual restart occurs within loop routine after a short delay
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

    // Device manager
    void setDeviceManager(DeviceManager* pDeviceManager)
    {
        _pDeviceManager = pDeviceManager;
    }
    DeviceManager* getDeviceManager()
    {
        return _pDeviceManager;
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

    // SysMod loop
    void sysModListSetup();
    bool _sysmodListDirty = false;
    bool _supervisorEnable = true;
    bool _loopAllSysMods = true;
    static const uint32_t LOOP_SLEEP_MS_DEFAULT = 1;
    uint32_t _loopSleepMs = LOOP_SLEEP_MS_DEFAULT;

    // SysMods to loop over
    std::vector<RaftSysMod*> _sysModLoopVector;
    uint32_t _loopCurModIdx = 0;

    // NOTE: _sysModuleList and _supervisorStats must be in synch
    //       when a module is added it must be added to both lists

    // List of modules
    std::list<RaftSysMod*> _sysModuleList;

    // Stress test loop delay
    uint32_t _stressTestLoopDelayMs = 0;
    uint32_t _stressTestLoopSkipCount = 0;
    uint32_t _stressTestCurSkipCount = 0;

    // Supervisor statistics
    SupervisorStats _supervisorStats;

    // Threshold of time for SysMod loop considered too slow
    static const uint32_t SLOW_SYS_MOD_THRESHOLD_MS_DEFAULT = 50;
    uint32_t _slowSysModThresholdUs = SLOW_SYS_MOD_THRESHOLD_MS_DEFAULT * 1000;
    bool _reportSlowSysMod = true;

    // Monitor timer and period
    uint32_t _monitorPeriodMs = 0;
    uint32_t _monitorTimerMs = 0;
    bool _monitorTimerStarted = false;
    bool _monitorShownFirstTime = false;
    static const uint32_t MONITOR_PERIOD_FIRST_SHOW_MS = 5000;
    bool _reportEnable = true;
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

    // System name
    String _systemName;

    // Hardware revision reporting prefix
    String _altHardwareRevisionPrefix = "";

    // System config
    RaftJsonIF& _systemConfig;

    // Mutable (NVS) config (for this module)
    RaftJsonNVS _mutableConfig;

    // SysTypeManager
    SysTypeManager& _sysTypeManager;

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

    // Device manager
    DeviceManager* _pDeviceManager = nullptr;

    // API to reset system
    RaftRetCode apiReset(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    // Get system version
    RaftRetCode apiGetVersion(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    // Friendly name get/set
    RaftRetCode apiFriendlyName(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    // Serial no
    RaftRetCode apiSerialNumber(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

    // Base SysType version
    RaftRetCode apiBaseSysTypeVersion(const String &reqStr, String& respStr, const APISourceInfo& sourceInfo);

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

    // Get base SysType version JSON
    String getBaseSysVersJson();

    // Check SysMod dependency satisfied
    bool checkSysModDependenciesSatisfied(const SysModFactory::SysModClassDef& sysModClassDef);

    // System restart
    void systemRestartNow();

    /// @brief Send report message
    /// @param msg Message to send
    void sendReportMessage(const char* msg);

    // Debug
    static constexpr const char* MODULE_PREFIX = "SysMan";
};
