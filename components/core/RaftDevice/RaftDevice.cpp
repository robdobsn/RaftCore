////////////////////////////////////////////////////////////////////////////////
//
// RaftDevice.cpp
//
// Rob Dobson 2024
//
////////////////////////////////////////////////////////////////////////////////

#include "RaftDevice.h"
#include "RaftUtils.h"

#define DEBUG_RAFT_DEVICE_CONSTRUCTOR

static const char *MODULE_PREFIX = "RaftDevice";

// @brief Construct a new Raft Device object
// @param pDevConfigJson JSON configuration for the device
RaftDevice::RaftDevice(const char* pClassName, const char* pDevConfigJson) :
        deviceConfig(pDevConfigJson), deviceClassName(pClassName)
{
    // Init publish device type to class name
    publishDeviceType = deviceClassName;
#ifdef DEBUG_RAFT_DEVICE_CONSTRUCTOR
    LOG_I(MODULE_PREFIX, "RaftDevice class %s devConfig %s", pClassName, deviceConfig.c_str());
#endif
}

// @brief Destroy the Raft Device object
RaftDevice::~RaftDevice()
{
}

// @brief Main loop for the device
void RaftDevice::loop()
{
}

// @brief Add REST API endpoints
// @param endpointManager Manager for REST API endpoints
void RaftDevice::addRestAPIEndpoints(RestAPIEndpointManager& endpointManager)
{
}

// @brief Add communication channels
// @param commsCore Core interface for communications
void RaftDevice::addCommsChannels(CommsCoreIF& commsCore)
{
}

// @brief Post-setup - called after setup of all devices is complete
void RaftDevice::postSetup()
{
}

// @brief Get time of last device status update
// @param includeElemOnlineStatusChanges Include element online status changes in the status update time
// @param includePollDataUpdates Include poll data updates in the status update time
// @return Time of last device status update in milliseconds
uint32_t RaftDevice::getLastStatusUpdateMs(bool includeElemOnlineStatusChanges, bool includePollDataUpdates) const
{
    return 0;
}

// @brief Get the device status as JSON
// @return JSON string
String RaftDevice::getStatusJSON() const
{
    return "{}";
}
