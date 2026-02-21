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
typedef std::function<bool(uint16_t topicIndex, CommsChannelMsg& msg)> SysMod_publishMsgGenFn;

// State change detector callback function type
typedef std::function<void(uint16_t topicIndex, std::vector<uint8_t>& stateHash)> SysMod_stateDetectCB;

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
    /// @note This is called by the SysManager during system setup. It should be implemented by derived
    /// classes to initialize the module. It should not be called directly by user code.
    virtual void setup()
    {
    }

    /// @brief Add REST API endpoints
    /// @param endpointManager Reference to the REST API endpoint manager
    /// @note This is called by the SysManager during system setup. It should be implemented by derived
    /// classes to add REST API endpoints. It should not be called directly by user code.
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& endpointManager)
    {
    }

    /// @brief Add communication channels
    /// @param commsCore Reference to the communications core interface
    /// @note This is called by the SysManager during system setup. It should be implemented by derived
    /// classes to add communication channels. It should not be called directly by user code.
    virtual void addCommsChannels(CommsCoreIF& commsCore)
    {
    }

    /// @brief Loop (called frequently)
    /// @note This is called by the SysManager in the main loop. It should be implemented by derived
    /// classes to perform periodic processing. It should not be called directly by user code.
    virtual void loop()
    {
    }

    /// @brief Post-setup (called after setup of all sysMods complete)
    /// @note This is called by the SysManager after setup of all sysMods is complete. It should be implemented by derived
    /// classes to perform any post-setup processing (after all other SysMods - that don't implement this function are setup). 
    /// It should not be called directly by user code.
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

    /// @brief Receive JSON command to be processed by the SysMod
    /// @param cmdJSON Command JSON string
    /// @return Result code
    /// @note The command JSON string should be in the format:
    ///       {"cmd":"<command>",...other args...}
    ///       where <command> is the command to be sent and other args are any additional arguments
    ///       to be passed to the command handler.
    ///       This will be called by the SysManager when a command is sent to this SysMod. It should be implemented by derived
    ///       classes to handle the command.
    virtual RaftRetCode receiveCmdJSON(const char* cmdJSON)
    {
        return RaftRetCode::RAFT_INVALID_OPERATION;
    }

    /// @brief Register a data source (msg generator callback functions) for publishing
    /// @param pubTopic Publish topic name
    /// @param msgGenCB Message generator callback (receives allocated topicIndex)
    /// @param stateDetectCB State detect callback (receives allocated topicIndex)
    /// @return Allocated topic index (0-based), or UINT16_MAX on failure
    /// @note This will be called by the SysManager when a data source is registered for this SysMod. It should be implemented by derived
    ///       classes to register the data source and its associated callbacks. It should not be called directly by user code.
    ///       This mechanism allows the SysMod to provide data for publishing on a topic, and to provide state change detection for that topic.
    virtual uint16_t registerDataSource(const char* pubTopic, SysMod_publishMsgGenFn msgGenCB, SysMod_stateDetectCB stateDetectCB)
    {
        return UINT16_MAX;
    }

    /// @brief Set the SysManager for this SysMod
    /// @param pSysManager Pointer to the system manager interface
    /// @note This is called by the SysManager during system setup. It should not be called directly by user code.
    static void setSysManager(SysManagerIF* pSysManager)
    {
        _pSysManager = pSysManager;
    }

    /// @brief Get SysManager interface
    /// @return Pointer to the system manager interface
    /// @note This allows access to the SysManager interface.
    SysManagerIF* getSysManager() const
    {
        return _pSysManager;
    }

    /// @brief Get const SysManager interface
    /// @return Pointer to the const system manager interface
    /// @note This allows access to the const SysManager interface.
    const SysManagerIF* getSysManagerConst() const
    {
        return _pSysManager;
    }

    /// @brief Get SysManager statistics
    /// @return Pointer to SupervisorStats
    /// @note This allows access to the SysManager statistics.
    SupervisorStats* getSysManagerStats();

    /// @brief Log silently (no output) - ensure no Log calls are made while logging!
    /// @param pLogStr Log string
    /// @note this can be used by SysMods that perform part of the logging mechanism to allow debugging 
    ///       without causing recursive logging calls.
    virtual void logSilently(const char* pLogStr)
    {
    }

    /// @brief Get Debug string
    /// @return Debug string as JSON
    /// @note This can be used to provide additional debug information in JSON format. It can be implemented by 
    ///       derived classes to provide relevant debug information. The default implementation returns an empty JSON object.
    virtual String getDebugJSON() const
    {
        return "{}";
    }

    /// @brief Get named value (double)
    /// @param pValueName Name of the value
    /// @param isValid (out) true if value is valid
    /// @return Named value
    /// @note Named values are a way to provide access to specific information from the SysMod. This can be used to query values
    ///       from this SysMod. If the returned isValid flag is false, it indicates that the named value is not available or its
    ///       value is not valid.
    virtual double getNamedValue(const char* pValueName, bool& isValid)
    {
        isValid = false;
        return 0;
    }

    /// @brief Set named value (double)
    /// @param pValueName Name of the value
    /// @param value Value to set
    /// @return true if successful, false otherwise
    /// @note This can be used to set specific values in the SysMod.
    virtual bool setNamedValue(const char* pValueName, double value)
    {
        return false;
    }

    /// @brief Get named string
    /// @param pValueName Name of the value
    /// @param isValid (out) true if value is valid
    /// @return Named string
    /// @note This can be used to get specific string values from the SysMod. If the returned isValid flag is false, 
    ///       it indicates that the named string is not available or its value is not valid.
    virtual String getNamedString(const char* pValueName, bool& isValid)
    {
        isValid = false;
        return "";
    }

    /// @brief Set named string
    /// @param pValueName Name of the value
    /// @param value Value to set
    /// @return true if successful, false otherwise
    /// @note This can be used to set specific string values in the SysMod.
    virtual bool setNamedString(const char* pValueName, const char* value)
    {
        return false;
    }

    /// @brief File/stream system activity - start file stream
    /// @param fileName Name of file
    /// @param fileLen Length of file
    /// @return true if successful, false otherwise
    /// @note This can be implemented by derived classes to handle the start of a file or stream. The fileName and fileLen
    ///       parameters provide information about the file / stream. The return value indicates whether the file / stream 
    ///       was successfully started.
    virtual bool fileStreamStart(const char* fileName, size_t fileLen)
    {
        return false;
    }

    /// @brief File/stream system activity - data block
    /// @param fileStreamBlock Reference to the file stream block
    /// @return RaftRetCode
    /// @note This can be implemented by derived classes to handle a block of data for a file or stream. The fileStreamBlock
    ///       parameter is block of data, including the file name, block content and metadata about the block and file. The 
    ///       return value indicates the result of processing the data block.
    virtual RaftRetCode fileStreamDataBlock(FileStreamBlock& fileStreamBlock)
    {
        return RaftRetCode::RAFT_INVALID_OPERATION;
    }

    /// @brief File/stream system activity - cancel/end file stream
    /// @param isNormalEnd true if normal end, false if cancelled
    /// @return true if successful, false otherwise
    /// @note This can be implemented by derived classes to handle the end or cancellation of a file or stream. The isNormalEnd
    ///       parameter indicates whether the file / stream ended normally (true) or was cancelled (false). The return value indicates
    ///       whether the end / cancellation was successfully processed.
    virtual bool fileStreamCancelEnd(bool isNormalEnd)
    {
        return true;
    }

public:
    // Non-virtual methods

    /// @brief Check if system main firmware update is in progress
    /// @return true if main firmware update is in progress, false otherwise
    /// @note Helper function to check if the system is currently performing a main firmware update.
    bool isSystemMainFWUpdate();

    /// @brief Check if system file transfer is in progress
    /// @return true if file transfer in progress, false otherwise
    /// @note Helper function to check if the system is currently performing a file transfer.
    bool isSystemFileTransferring();

    /// @brief Check if system streaming is in progress
    /// @return true if streaming in progress, false otherwise
    /// @note Helper function to check if the system is currently performing streaming activity.
    bool isSystemStreaming();

    /// @brief Get RestAPIEndpointManager
    /// @return Pointer to RestAPIEndpointManager
    /// @note This allows access to the RestAPIEndpointManager.
    RestAPIEndpointManager* getRestAPIEndpointManager();

    /// @brief Get communications core interface
    /// @return Pointer to CommsCoreIF
    /// @note This allows access to the communications core interface.
    CommsCoreIF* getCommsCore();

    /// @brief Add status change callback on another SysMod
    /// @param sysModName Name of the SysMod
    /// @param statusChangeCB Status change callback
    /// @note This allows setting a status change callback for another SysMod. The callback will be called when the status 
    ///       of the specified SysMod changes (e.g. goes online or offline). This can be used to monitor the status of other
    ///       SysMods and react accordingly.
    void sysModSetStatusChangeCB(const char* sysModName, SysMod_statusChangeCB statusChangeCB);

    /// @brief Get JSON status of another SysMod
    /// @param sysModName Name of the SysMod
    /// @return JSON status string
    /// @note This allows getting the JSON status string of another SysMod. This can be used to query the status of other SysMods.
    String sysModGetStatusJSON(const char* sysModName) const;

    /// @brief Send JSON command to another SysMod
    /// @param sysModName Name of the SysMod
    /// @param jsonCmd JSON command string
    /// @return RaftRetCode
    /// @note This allows sending a JSON command to another SysMod. The command will be processed by the receiveCmdJSON function 
    ///       of the target SysMod.
    RaftRetCode sysModSendCmdJSON(const char* sysModName, const char* jsonCmd);

    /// @brief Get named value from another SysMod
    /// @param sysModName Name of the SysMod
    /// @param valueName Name of the value
    /// @param isValid (out) true if value is valid
    /// @return Named value
    /// @note This allows getting a named value from another SysMod. This can be used to query specific values from other SysMods. 
    ///       If the returned isValid flag is false, it indicates that the named value is not available or its value is invalid.
    double sysModGetNamedValue(const char* sysModName, const char* valueName, bool& isValid) const;

    /// @brief Get named string from another SysMod
    /// @param pSysModName Name of the SysMod
    /// @param valueName String name
    /// @param isValid (out) true if value is valid
    /// @return string
    /// @note This allows getting a named string from another SysMod. This can be used to query the status or configuration of other SysMods.
    ///       If the returned isValid flag is false, it indicates that the named string is not available or its value is invalid.
    virtual String sysModGetNamedString(const char* pSysModName, const char* valueName, bool& isValid) const;

    /// @brief Set status change callback for this SysMod
    /// @param statusChangeCB Status change callback
    /// @note This allows setting a status change callback for this SysMod. The callback will be called when the status of this SysMod changes
    ///       (e.g. goes online or offline). This can be used to trigger actions when the status of this SysMod changes.
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
    /// @note This is a helper function to set the log level for a module. The logLevel string can start with:
    ///       "N" for None, "E" for Error, "W" for Warning, "I" for Info, "D" for Debug, "V" for Verbose.
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
