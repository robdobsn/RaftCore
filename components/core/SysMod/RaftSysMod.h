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

// Forward declarations
class SysManager;
class RestAPIEndpointManager;
class CommsCoreIF;
class CommsChannelMsg;
class SupervisorStats;
class FileStreamBlock;

// Status change callback function type
typedef std::function<void(const String& sourceName, bool changeToOnline)> SysMod_statusChangeCB;

// Message generator callback function type
typedef std::function<bool(const char* messageName, CommsChannelMsg& msg)> SysMod_publishMsgGenFn;

// State change detector callback function type
typedef std::function<void(const char* stateName, std::vector<uint8_t>& stateHash)> SysMod_stateDetectCB;

class RaftSysMod
{
public:
    RaftSysMod(const char *pModuleName, 
            RaftJsonIF& sysConfig,
            const char* pConfigPrefix = nullptr, 
            const char* pMutableConfigNamespace = nullptr,
            const char* pMutableConfigPrefix = nullptr);
    virtual ~RaftSysMod();

    // Setup
    virtual void setup()
    {
    }

    // Add REST API endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& endpointManager)
    {
    }

    // Add comms channels
    virtual void addCommsChannels(CommsCoreIF& commsCore)
    {
    }

    // Loop (called frequently)
    virtual void loop()
    {
    }

    // Post-setup - called after setup of all sysMods complete
    virtual void postSetup()
    {
    }

    // Get name
    virtual const char* modName()
    {
        return _sysModName.c_str();
    }
    virtual String& modNameStr()
    {
        return _sysModName;
    }

    // Check if main activity busy
    virtual bool isBusy()
    {
        return false;
    }
    
    // System name
    virtual String getSystemName();

    // System unique string
    virtual String getSystemUniqueString();

    // Friendly name
    virtual String getFriendlyName(bool& isSet);

    // Config access
    virtual long configGetLong(const char *dataPath, long defaultValue);
    virtual double configGetDouble(const char *dataPath, double defaultValue);
    virtual bool configGetBool(const char *dataPath, bool defaultValue);
    virtual String configGetString(const char *dataPath, const char* defaultValue);
    virtual String configGetString(const char *dataPath, const String& defaultValue);
    virtual RaftJsonIF::RaftJsonType configGetType(const char *dataPath, int& arrayLen);
    virtual bool configGetArrayElems(const char *dataPath, std::vector<String>& strList) const;
    virtual int configGetPin(const char* dataPath, const char* defaultValue);
    virtual void configRegisterChangeCallback(RaftJsonChangeCallbackType configChangeCallback);
    virtual RaftJsonIF& configGetConfig()
    {
        return config;
    }
    virtual RaftJsonIF& modConfig()
    {
        return config;
    }

    // Get JSON status string
    virtual String getStatusJSON() const
    {
        return "{\"rslt\":\"ok\"}";
    }

    // Receive JSON command
    virtual RaftRetCode receiveCmdJSON(const char* cmdJSON)
    {
        return RaftRetCode::RAFT_INVALID_OPERATION;
    }

    // Register data source (msg generator callback functions)
    // This is generally only implemented by a SysMod that handles message publishing 
    virtual bool registerDataSource(const char* pubTopic, SysMod_publishMsgGenFn msgGenCB, SysMod_stateDetectCB stateDetectCB)
    {
        return false;
    }

    // Static function to define the manager for system modules
    static void setSysManager(SysManager* pSysManager)
    {
        _pSysManager = pSysManager;
    }

    SysManager* getSysManager() const
    {
        return _pSysManager;
    }
    const SysManager* getSysManagerConst() const
    {
        return _pSysManager;
    }
    SupervisorStats* getSysManagerStats();

    // Logging destination functionality - ensure no Log calls are made while logging!
    virtual void logSilently(const char* pLogStr)
    {
    }

    // Get debug string
    virtual String getDebugJSON() const
    {
        return "{}";
    }

    // Get named value
    virtual double getNamedValue(const char* valueName, bool& isValid)
    {
        isValid = false;
        return 0;
    }

    // Set named value
    virtual bool setNamedValue(const char* valueName, double value)
    {
        return false;
    }

    // Get named string
    virtual String getNamedString(const char* valueName, bool& isValid)
    {
        isValid = false;
        return "";
    }

    // Set named string
    virtual bool setNamedString(const char* valueName, const char* value)
    {
        return false;
    }

    // File/Stream Start
    virtual bool fileStreamStart(const char* fileName, size_t fileLen)
    {
        return false;
    }
    virtual RaftRetCode fileStreamDataBlock(FileStreamBlock& fileStreamBlock)
    {
        return RaftRetCode::RAFT_INVALID_OPERATION;
    }
    virtual bool fileStreamCancelEnd(bool isNormalEnd)
    {
        return true;
    }

    // File/stream system activity - main firmware update
    bool isSystemMainFWUpdate();

    // File/stream system activity - file transfer
    bool isSystemFileTransferring();

    // File/stream system activity - streaming
    bool isSystemStreaming();

public:
    // Non-virtual methods

    // Get REST API endpoints
    RestAPIEndpointManager* getRestAPIEndpointManager();

    // Get CommsCore
    CommsCoreIF* getCommsCore();

    // Add status change callback on another SysMod
    void sysModSetStatusChangeCB(const char* sysModName, SysMod_statusChangeCB statusChangeCB);

    // Get JSON status of another SysMod
    String sysModGetStatusJSON(const char* sysModName) const;

    // Send JSON command to another SysMod
    RaftRetCode sysModSendCmdJSON(const char* sysModName, const char* jsonCmd);

    // SysMod get named value
    double sysModGetNamedValue(const char* sysModName, const char* valueName, bool& isValid);

    // Status change callback
    void setStatusChangeCB(SysMod_statusChangeCB statusChangeCB)
    {
        _statusChangeCBs.push_back(statusChangeCB);
    }

    // Clear status change callbacks
    void clearStatusChangeCBs()
    {
        _statusChangeCBs.clear();
    }

    // Set log level of module
    static void setModuleLogLevel(const char* pModuleName, const String& logLevel)
    {
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
    static SysManager* _pSysManager;

    // Status change callbacks
    std::list<SysMod_statusChangeCB> _statusChangeCBs;
};
