////////////////////////////////////////////////////////////////////////////////
//
// RaftDevice.h
//
// Rob Dobson 2024
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftArduino.h"
#include "RaftJson.h"
#include "RaftRetCode.h"
#include "RaftDeviceJSONLevel.h"

class RestAPIEndpointManager;
class CommsCoreIF;

class RaftDevice
{
public:
    // @brief Construct a new Raft Device object
    // @param pDevConfigJson JSON configuration for the device
    RaftDevice(const char* pClassName, const char* pDevConfigJson);
    
    // @brief Destroy the Raft Device object
    virtual ~RaftDevice();

    // @brief Get the name of the device instance
    // @return Device name as a string
    virtual String getDeviceName() const
    {
        return deviceConfig.getString("name", "UNKNOWN");
    }

    // @brief Get the class name of the device
    // @return Device class name as a string
    virtual String getDeviceClassName() const
    {
        return deviceClassName;
    }

    // @brief Get the publish device type
    // @return Publish device type as a string
    virtual String getPublishDeviceType() const
    {
        return publishDeviceType;
    }

    // @brief Setup the device
    virtual void setup();

    // @brief Main loop for the device (called frequently)
    virtual void loop();

    // @brief Add REST API endpoints
    // @param endpointManager Manager for REST API endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& endpointManager);

    // @brief Add communication channels
    // @param commsCore Core interface for communications
    virtual void addCommsChannels(CommsCoreIF& commsCore);

    // @brief Post-setup - called after setup of all devices is complete
    virtual void postSetup();

    // @brief Get time of last device status update
    // @param includeElemOnlineStatusChanges Include element online status changes in the status update time
    // @param includePollDataUpdates Include poll data updates in the status update time
    // @return Time of last device status update in milliseconds
    virtual uint32_t getLastStatusUpdateMs(bool includeElemOnlineStatusChanges, bool includePollDataUpdates) const;

    // @brief Get the device status as JSON
    // @return JSON string
    virtual String getStatusJSON() const;

    /// @brief Send a binary command to the device
    /// @param formatCode Format code for the command
    /// @param pData Pointer to the data
    /// @param dataLen Length of the data
    /// @return RaftRetCode
    virtual RaftRetCode sendCmdBinary(uint32_t formatCode, const uint8_t* pData, uint32_t dataLen);

    /// @brief Send a JSON command to the device
    /// @param jsonCmd JSON command
    /// @return RaftRetCode
    virtual RaftRetCode sendCmdJSON(const char* jsonCmd);

    /// @brief Get binary data from the device
    /// @param formatCode format code for the command
    /// @param buf (out) buffer to receive the binary data
    /// @param bufMaxLen maximum length of data to return
    /// @return RaftRetCode
    virtual RaftRetCode getDataBinary(uint32_t formatCode, std::vector<uint8_t>& buf, uint32_t bufMaxLen) const;

    /// @brief Get JSON data from the device
    /// @param level Level of data to return
    /// @return JSON string
    virtual String getDataJSON(RaftDeviceJSONLevel level = DEVICE_JSON_LEVEL_MIN) const;

    /// @brief Get named value from the device
    /// @param pParam Parameter name
    /// @param isFresh (out) true if the value is fresh
    /// @return double value
    virtual double getNamedValue(const char* pParam, bool& isFresh) const;

    /// @brief Check if device has capability
    /// @param pCapabilityStr capability string
    /// @return true if the device has the capability
    virtual bool hasCapability(const char* pCapabilityStr) const;

protected:
    // Device configuration
    RaftJson deviceConfig;

    // Device class
    String deviceClassName;

    // Publish device type
    String publishDeviceType;
};
