/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// SysManagerIF
//
// Rob Dobson 2025
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once 

#include "NamedValueProvider.h"
#include "RaftSysMod.h"

typedef String (*SysManager_statsCB)();

class RaftSysMod;
class RestAPIEndpointManager;
class ProtocolExchange;
class DeviceManager;

class SysManagerIF : public NamedValueProvider
{
public:

    /// @brief Get SysMod instance by name
    /// @param pSysModName
    /// @return Pointer to SysMod instance or nullptr if not found
    virtual RaftSysMod* getSysMod(const char* pSysModName) const = 0;

    /// @brief Add a pre-constructed SysMod to the managed list
    /// @param pSysMod Pointer to the SysMod instance to add
    virtual void addManagedSysMod(RaftSysMod* pSysMod) = 0;

    /// @brief Set status change callback on a SysMod
    /// @param pSysModName Name of SysMod
    /// @param statusChangeCB Callback function
    virtual void setStatusChangeCB(const char* pSysModName, SysMod_statusChangeCB statusChangeCB) = 0;

    /// @brief Get status from SysMod
    /// @param pSysModName Name of SysMod
    /// @return Status string (JSON)
    virtual String getStatusJSON(const char* pSysModName) const = 0;

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
    virtual RaftRetCode sendCmdJSON(const char* pSysModNameOrNullForAll, const char* cmdJSON) = 0;

    /// @brief Register data source (message generator functions)
    /// @param pSysModName Name of SysMod
    /// @param pubTopic Publish topic name
    /// @param msgGenCB Message generator callback (receives allocated topicIndex)
    /// @param stateDetectCB State detect callback (receives allocated topicIndex)
    /// @return Allocated topic index (0-based), or UINT16_MAX on failure
    virtual uint16_t registerDataSource(const char* pSysModName, const char* pubTopic, 
            SysMod_publishMsgGenFn msgGenCB, 
            SysMod_stateDetectCB stateDetectCB) = 0;

    /// @brief Request system restart
    virtual void systemRestart() = 0;

    /// @brief Get REST API endpoint manager
    /// @return Pointer to RestAPIEndpointManager
    virtual RestAPIEndpointManager* getRestAPIEndpointManager() = 0;

    /// @brief Get communications core interface
    /// @return Pointer to CommsCoreIF or nullptr if not available
    virtual CommsCoreIF* getCommsCore() = 0;

    /// @brief Get protocol exchange interface
    /// @return Pointer to ProtocolExchange or nullptr if not available
    virtual ProtocolExchange* getProtocolExchange() = 0;

    /// @brief Get device manager
    /// @return Pointer to DeviceManager or nullptr if not available
    virtual DeviceManager* getDeviceManager() = 0;

    /// @brief Get statistics
    /// @return Pointer to SupervisorStats or nullptr if not available
    virtual SupervisorStats* getStats() = 0;

    /// @brief Get named value
    /// @param pSysModNameOrNullForSysMan Module name (null for SysManager)
    /// @param param Parameter name
    /// @param isValid (out) true if value is valid
    /// @return value
    virtual double getNamedValue(const char* pSysModNameOrNullForSysMan, const char* param, bool& isValid) const = 0;

        /// @brief Set named value
    /// @param pSysModNameOrNullForSysMan Module name (null for SysManager)
    /// @param param Parameter name
    /// @param value Value to set
    virtual bool setNamedValue(const char* pSysModNameOrNullForSysMan, const char* param, double value) = 0;

    /// @brief Get named value string
    /// @param pSysModNameOrNullForSysMan Module name (null for SysManager)
    /// @param valueName Value name
    /// @param isValid (out) true if value is valid
    /// @return value string
    virtual String getNamedString(const char* pSysModNameOrNullForSysMan, const char* valueName, bool& isValid) const = 0;

    /// @brief Set named value string
    /// @param pSysModNameOrNullForSysMan Module name (null for SysManager)
    /// @param valueName Value name
    /// @param value Value to set
    /// @return true if set
    virtual bool setNamedString(const char* pSysModNameOrNullForSysMan, const char* valueName, const char* value) = 0;
};
