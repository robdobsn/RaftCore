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

#include "SysManagerIF.h"
#include "SysTypeManager.h"
#include "SysModFactory.h"
#include "SupervisorStats.h"
#include "ProtocolExchange.h"
#include "DeviceManager.h"
#include "RaftJsonNVS.h"

class SysManager : public SysManagerIF
{
public:
    /// @brief Constructor
    /// @param pModuleName Module name
    /// @param systemConfig System configuration (based on a JSON document)
    /// @param sysManagerNVSNamespace NVS namespace for SysManager
    /// @param sysTypeManager SysTypeManager instance
    /// @param pSystemName Optional system name
    /// @param pDefaultFriendlyName Optional default friendly name
    /// @param serialLengthBytes Length of serial number in bytes
    /// @param pSerialMagicStr Optional magic string for serial number generation
    SysManager(const char* pModuleName,
            RaftJsonIF& systemConfig,
            const String sysManagerNVSNamespace,
            SysTypeManager& sysTypeManager,
            const char* pSystemName = nullptr,
            const char* pDefaultFriendlyName = nullptr,
            uint32_t serialLengthBytes = DEFAULT_SERIAL_LEN_BYTES, 
            const char* pSerialMagicStr = nullptr);

    /// @brief Pre-setup - called before all other modules setup
    void preSetup();

    /// @brief Post-setup - called after other modules setup (and to setup SysMods)
    void postSetup();

    /// @brief Loop (called from main loop)
    virtual void loop();

    /// @brief Register SysMod with the SysMod factory
    /// @param pClassName Class name of the SysMod
    /// @param pCreateFn Function pointer to create the SysMod
    /// @param alwaysEnable Whether to always enable this SysMod
    /// @param pDependencyListCSV Comma-separated list of dependencies
    void registerSysMod(const char* pClassName, SysModCreateFn pCreateFn, bool alwaysEnable = false, const char* pDependencyListCSV = nullptr)
    {
        _sysModFactory.registerSysMod(pClassName, pCreateFn, alwaysEnable, pDependencyListCSV);
    }

    /// @brief Add a pre-constructed SysMod to the managed list
    /// @param pSysMod Pointer to the SysMod instance to add
    virtual void addManagedSysMod(RaftSysMod* pSysMod) override final;

    /// @brief Get SysMod instance by name
    /// @param pSysModName Name of the SysMod
    /// @return Pointer to SysMod instance or nullptr if not found
    virtual RaftSysMod* getSysMod(const char* pSysModName) const override final;

    /// @brief Set stats callback (for SysManager's own stats)
    /// @param statsCB Callback function for stats
    void setStatsCB(SysManager_statsCB statsCB)
    {
        _statsCB = statsCB;
    }

    /// @brief Add status change callback on a SysMod
    /// @param sysModName Name of the SysMod
    /// @param statusChangeCB Callback function for status change
    virtual void setStatusChangeCB(const char* sysModName, SysMod_statusChangeCB statusChangeCB) override final;

    /// @brief Get status from SysMod
    /// @param pSysModName Name of the SysMod
    /// @return Status string (JSON)
    virtual String getStatusJSON(const char* pSysModName) const override final;

    /// @brief Get debug information from SysMod
    /// @param pSysModName Name of the SysMod
    /// @return Debug string (JSON)
    String getDebugJSON(const char* pSysModName) const;

    /// @brief Notify of system shutdown
    /// @param isRestart True if this is a restart (false if shutdown)
    /// @param reasonOrNull Reason for shutdown (may be nullptr)
    void notifyOfShutdown(bool isRestart = true, const char* reasonOrNull = nullptr);

    /// @brief Send command to one or all SysMods
    /// @param pSysModNameOrNullForAll Name of SysMod to send command to or nullptr for all SysMods
    /// @param cmdJSON Command JSON string
    /// @return Result code
    /// @note The command JSON string should be in the format:
    ///       {"cmd":"<command>",...other args...}
    ///       where <command> is the command to be sent and other args are any additional arguments
    ///       to be passed to the command handler.
    ///       The command will be sent to the SysMod's command handler.
    ///       The SysMod should handle the command and return a result.
    virtual RaftRetCode sendCmdJSON(const char* pSysModNameOrNullForAll, const char* cmdJSON) override final;

    /// @brief Register data source (message generator functions)
    /// @param pSysModName Name of the SysMod
    /// @param pubTopic Publish topic name
    /// @param msgGenCB Message generator callback (receives allocated topicIndex)
    /// @param stateDetectCB State detect callback (receives allocated topicIndex)
    /// @return Allocated topic index (0-based), or UINT16_MAX on failure
    virtual uint16_t registerDataSource(const char* pSysModName, const char* pubTopic, 
            SysMod_publishMsgGenFn msgGenCB, 
            SysMod_stateDetectCB stateDetectCB) override final;
    
    /// @brief Get named value
    /// @param pSysModName Name of the SysMod
    /// @param valueName Value name
    /// @param isValid (out) true if value is valid
    /// @return value
    virtual double getNamedValue(const char* pSysModName, const char* valueName, bool& isValid) const override final;

    /// @brief Set named value
    /// @param pSysModName Name of the SysMod
    /// @param valueName Value name
    /// @param value Value to set
    /// @return true if set
    virtual bool setNamedValue(const char* pSysModName, const char* valueName, double value) override final;

    /// @brief Get named value string
    /// @param pSysModName Name of the SysMod
    /// @param valueName Value name
    /// @param isValid (out) true if value is valid
    /// @return value string
    virtual String getNamedString(const char* pSysModName, const char* valueName, bool& isValid) const override final;

    /// @brief Set named value string
    /// @param pSysModName Name of the SysMod
    /// @param valueName Value name
    /// @param value Value to set
    /// @return true if set
    virtual bool setNamedString(const char* pSysModName, const char* valueName, const char* value) override final;

    /// @brief Request system restart
    virtual void systemRestart() override final
    {
        // Notify all SysMods of restart
        notifyOfShutdown(true);

        // Actual restart occurs within loop routine after a short delay
        _systemRestartPending = true;
        _systemRestartMs = millis();
    }

    /// @brief Set REST API endpoints
    /// @param restAPIEndpoints Reference to RestAPIEndpointManager
    void setRestAPIEndpoints(RestAPIEndpointManager& restAPIEndpoints)
    {
        _pRestAPIEndpointManager = &restAPIEndpoints;
    }

    /// @brief Get REST API endpoint manager
    /// @return Pointer to RestAPIEndpointManager
    virtual RestAPIEndpointManager* getRestAPIEndpointManager() override final
    {
        return _pRestAPIEndpointManager;
    }

    // @brief Set communications core interface
    /// @param pCommsCore Pointer to CommsCoreIF
    void setCommsCore(CommsCoreIF* pCommsCore)
    {
        _pCommsCore = pCommsCore;
    }

    /// @brief Get communications core interface
    /// @return Pointer to CommsCoreIF
    virtual CommsCoreIF* getCommsCore() override final
    {
        return _pCommsCore;
    }

    /// @brief Set protocol exchange interface
    /// @param pProtocolExchange Pointer to ProtocolExchange
    void setProtocolExchange(ProtocolExchange* pProtocolExchange)
    {
        _pProtocolExchange = pProtocolExchange;
    }

    /// @brief Get protocol exchange interface
    /// @return Pointer to ProtocolExchange
    virtual ProtocolExchange* getProtocolExchange() override final
    {
        return _pProtocolExchange;
    }

    /// @brief Set device manager
    /// @param pDeviceManager Pointer to DeviceManager
    void setDeviceManager(DeviceManager* pDeviceManager)
    {
        _pDeviceManager = pDeviceManager;
    }

    /// @brief Get device manager
    /// @return Pointer to DeviceManager
    virtual DeviceManager* getDeviceManager() override final
    {
        return _pDeviceManager;
    }

    // Get supervisor stats
    virtual SupervisorStats* getStats() override final
    {
        return &_supervisorStats;
    }

    void informOfFileStreamActivity(bool isMainFWUpdate, bool isFileSystemActivity, bool isStreaming)
    {
        _isSystemMainFWUpdate = isMainFWUpdate;
        _isSystemFileTransferring = isFileSystemActivity;
        _isSystemStreaming = isStreaming;
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

    // Auto-set hostname when friendly name is set
    bool _autoSetHostname = true;

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

    /// @brief get friendly name
    /// @param (out) isSet
    /// @return friendly name
    String getFriendlyName(bool& isSet) const;

    /// @brief set friendly name
    /// @param friendlyName
    /// @param forceSetHostname
    /// @return true if set
    bool setFriendlyName(const String& friendlyName, bool forceSetHostname = false);

    // Debug
    static constexpr const char* MODULE_PREFIX = "SysMan";
};
