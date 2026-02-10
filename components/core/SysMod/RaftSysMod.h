/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Base class for Raft SysMods (System Modules)
// For more info see SysManager
// Rob Dobson 2013-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <list>
#include <functional>
#include "Logger.h"
#include "RaftArduino.h"
#include "RaftRetCode.h"
#include "RaftJsonPrefixed.h"
#include "RaftDevice.h"

// Forward declarations
class RestAPIEndpointManager;
class CommsCoreIF;
class CommsChannelMsg;
class SupervisorStats;
class FileStreamBlock;
class SysManagerIF;

// Status change callback function type
typedef std::function<void(const String& sourceName, bool changeToOnline)> SysMod_statusChangeCB;

// Message generator callback function type
typedef std::function<bool(const char* topicName, CommsChannelMsg& msg)> SysMod_publishMsgGenFn;

// State change detector callback function type
typedef std::function<void(const char* stateName, std::vector<uint8_t>& stateHash)> SysMod_stateDetectCB;

class RaftSysMod
{
public:
    /// @brief Constructor
    /// @param pModuleName Module name
    /// @param sysConfig System configuration interface
    /// @param pConfigPrefix Configuration prefix
    /// @param pMutableConfigNamespace Mutable configuration namespace
    /// @param pMutableConfigPrefix Mutable configuration prefix
    RaftSysMod(const char *pModuleName, 
            RaftJsonIF& sysConfig,
            const char* pConfigPrefix = nullptr, 
            const char* pMutableConfigNamespace = nullptr,
            const char* pMutableConfigPrefix = nullptr);

    /// @brief Destructor
    virtual ~RaftSysMod();

    /// @brief Setup (called once at startup)
    virtual void setup()
    {
    }

    /// @brief Add REST API endpoints
    /// @param endpointManager Reference to the REST API endpoint manager
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& endpointManager)
    {
    }

    /// @brief Add communication channels
    /// @param commsCore Reference to the communications core interface
    virtual void addCommsChannels(CommsCoreIF& commsCore)
    {
    }

    /// @brief Loop (called frequently)
    virtual void loop()
    {
    }

    /// @brief Post-setup (called after setup of all sysMods complete)
    virtual void postSetup()
    {
    }

    /// @brief Get name of sysMod
    /// @return Name of sysMod as const char*
    virtual const char* modName()
    {
        return _sysModName.c_str();
    }

    /// @brief Get name of sysMod as String
    /// @return Name of sysMod as String
    virtual String& modNameStr()
    {
        return _sysModName;
    }

    /// @brief Check if main activity busy
    /// @return true if busy, false otherwise
    virtual bool isBusy()
    {
        return false;
    }
    
    /// @brief Get system name
    /// @return System name
    virtual String getSystemName();

    /// @brief Get system unique string
    /// @return System unique string
    virtual String getSystemUniqueString();

    /// @brief Get friendly name
    /// @param isSet (out) true if friendly name is set
    /// @return Friendly name
    virtual String getFriendlyName(bool& isSet);

    /// @brief Configuration access methods
    virtual int configGetInt(const char *dataPath, int defaultValue);
    virtual long configGetLong(const char *dataPath, long defaultValue);
    virtual double configGetDouble(const char *dataPath, double defaultValue);
    virtual bool configGetBool(const char *dataPath, bool defaultValue);
    virtual String configGetString(const char *dataPath, const char* defaultValue);
    virtual String configGetString(const char *dataPath, const String& defaultValue);
    virtual RaftJsonIF::RaftJsonType configGetType(const char *dataPath, int& arrayLen);
    virtual bool configGetArrayElems(const char *dataPath, std::vector<String>& strList) const;
    virtual void configRegisterChangeCallback(RaftJsonChangeCallbackType configChangeCallback);

    /// @brief Get config interface
    /// @return Reference to the configuration interface
    virtual RaftJsonIF& configGetConfig()
    {
        return config;
    }

    /// @brief Get modifiable config interface
    /// @return Reference to the modifiable configuration interface
    virtual RaftJsonIF& modConfig()
    {
        return config;
    }

    /// @brief Get JSON status string
    /// @return JSON status string
    virtual String getStatusJSON() const
    {
        return "{\"rslt\":\"ok\"}";
    }

    /// @brief Receive JSON command
    /// @param cmdJSON Command JSON string
    /// @return Result code
    virtual RaftRetCode receiveCmdJSON(const char* cmdJSON)
    {
        return RaftRetCode::RAFT_INVALID_OPERATION;
    }

    /// @brief Register a data source (msg generator callback functions) for publishing
    /// @param pubTopic Publish topic
    /// @param msgGenCB Message generator callback
    /// @param stateDetectCB State detect callback
    /// @return true if successful, false otherwise
    virtual bool registerDataSource(const char* pubTopic, SysMod_publishMsgGenFn msgGenCB, SysMod_stateDetectCB stateDetectCB)
    {
        return false;
    }

    /// @brief Set the SysManager for this SysMod
    /// @param pSysManager Pointer to the system manager interface
    static void setSysManager(SysManagerIF* pSysManager)
    {
        _pSysManager = pSysManager;
    }

    /// @brief Get SysManager interface
    /// @return Pointer to the system manager interface
    SysManagerIF* getSysManager() const
    {
        return _pSysManager;
    }

    /// @brief Get const SysManager interface
    /// @return Pointer to the const system manager interface
    const SysManagerIF* getSysManagerConst() const
    {
        return _pSysManager;
    }

    /// @brief Get SysManager statistics
    /// @return Pointer to SupervisorStats
    SupervisorStats* getSysManagerStats();

    /// @brief Log silently (no output) - ensure no Log calls are made while logging!
    /// @param pLogStr Log string
    virtual void logSilently(const char* pLogStr)
    {
    }

    /// @brief Get Debug string
    /// @return Debug string as JSON
    virtual String getDebugJSON() const
    {
        return "{}";
    }

    /// @brief Get named value (double)
    /// @param pValueName Name of the value
    /// @param isValid (out) true if value is valid
    /// @return Named value
    virtual double getNamedValue(const char* pValueName, bool& isValid)
    {
        isValid = false;
        return 0;
    }

    /// @brief Set named value (double)
    /// @param pValueName Name of the value
    /// @param value Value to set
    /// @return true if successful, false otherwise
    virtual bool setNamedValue(const char* pValueName, double value)
    {
        return false;
    }

    /// @brief Get named string
    /// @param pValueName Name of the value
    /// @param isValid (out) true if value is valid
    /// @return Named string
    virtual String getNamedString(const char* pValueName, bool& isValid)
    {
        isValid = false;
        return "";
    }

    /// @brief Set named string
    /// @param pValueName Name of the value
    /// @param value Value to set
    /// @return true if successful, false otherwise
    virtual bool setNamedString(const char* pValueName, const char* value)
    {
        return false;
    }

    /// @brief File/stream system activity - start file stream
    /// @param fileName Name of file
    /// @param fileLen Length of file
    /// @return true if successful, false otherwise
    virtual bool fileStreamStart(const char* fileName, size_t fileLen)
    {
        return false;
    }

    /// @brief File/stream system activity - data block
    /// @param fileStreamBlock Reference to the file stream block
    /// @return RaftRetCode
    virtual RaftRetCode fileStreamDataBlock(FileStreamBlock& fileStreamBlock)
    {
        return RaftRetCode::RAFT_INVALID_OPERATION;
    }

    /// @brief File/stream system activity - cancel/end file stream
    /// @param isNormalEnd true if normal end, false if cancelled
    /// @return true if successful, false otherwise
    virtual bool fileStreamCancelEnd(bool isNormalEnd)
    {
        return true;
    }

    /// @brief Check if system main firmware update is in progress
    /// @return true if main firmware update is in progress, false otherwise
    bool isSystemMainFWUpdate();

    /// @brief Check if system file transfer is in progress
    /// @return true if file transfer in progress, false otherwise
    bool isSystemFileTransferring();

    /// @brief Check if system streaming is in progress
    /// @return true if streaming in progress, false otherwise
    bool isSystemStreaming();

public:
    // Non-virtual methods

    /// @brief Get RestAPIEndpointManager
    /// @return Pointer to RestAPIEndpointManager
    RestAPIEndpointManager* getRestAPIEndpointManager();

    /// @brief Get communications core interface
    /// @return Pointer to CommsCoreIF
    CommsCoreIF* getCommsCore();

    /// @brief Add status change callback on another SysMod
    /// @param sysModName Name of the SysMod
    /// @param statusChangeCB Status change callback
    void sysModSetStatusChangeCB(const char* sysModName, SysMod_statusChangeCB statusChangeCB);

    /// @brief Get JSON status of another SysMod
    /// @param sysModName Name of the SysMod
    /// @return JSON status string
    String sysModGetStatusJSON(const char* sysModName) const;

    /// @brief Send JSON command to another SysMod
    /// @param sysModName Name of the SysMod
    /// @param jsonCmd JSON command string
    /// @return RaftRetCode
    RaftRetCode sysModSendCmdJSON(const char* sysModName, const char* jsonCmd);

    /// @brief Get named value from another SysMod
    /// @param sysModName Name of the SysMod
    /// @param valueName Name of the value
    /// @param isValid (out) true if value is valid
    /// @return Named value
    double sysModGetNamedValue(const char* sysModName, const char* valueName, bool& isValid) const;

    /// @brief Set status change callback for this SysMod
    /// @param statusChangeCB Status change callback
    void setStatusChangeCB(SysMod_statusChangeCB statusChangeCB)
    {
        _statusChangeCBs.push_back(statusChangeCB);
    }

    /// @brief Clear all status change callbacks for this SysMod
    void clearStatusChangeCBs()
    {
        _statusChangeCBs.clear();
    }

    /// @brief Set module log level
    /// @param pModuleName Name of the module
    /// @param logLevel Log level string
    static void setModuleLogLevel(const char* pModuleName, const String& logLevel)
    {
#ifdef ESP_PLATFORM
        if (logLevel.startsWith("N"))
            esp_log_level_set(pModuleName, ESP_LOG_NONE);
        else if (logLevel.startsWith("E"))
            esp_log_level_set(pModuleName, ESP_LOG_ERROR);
        else if (logLevel.startsWith("W"))
            esp_log_level_set(pModuleName, ESP_LOG_WARN);
        else if (logLevel.startsWith("I"))
            esp_log_level_set(pModuleName, ESP_LOG_INFO);
        else if (logLevel.startsWith("D"))
            esp_log_level_set(pModuleName, ESP_LOG_DEBUG);
        else if (logLevel.startsWith("V"))
            esp_log_level_set(pModuleName, ESP_LOG_VERBOSE);
#endif
    }

protected:
    // Module config
    RaftJsonPrefixed config;

    // Execute status change callbacks
    void executeStatusChangeCBs(bool changeToOn);

    // Mutable config management
    virtual void configSaveData(const String& pConfigStr);

private:
    // Name of this module
    String _sysModName;
    String _sysModLogPrefix;

    // Config prefix
    String _configPrefix;
    
    // Manager (parent)
    static SysManagerIF* _pSysManager;

    // Status change callbacks
    std::list<SysMod_statusChangeCB> _statusChangeCBs;
};
