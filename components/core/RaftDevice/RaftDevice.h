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

protected:
    // Device configuration
    RaftJson deviceConfig;

    // Device class
    String deviceClassName;

    // Publish device type
    String publishDeviceType;
};
