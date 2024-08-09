////////////////////////////////////////////////////////////////////////////////
//
// RaftDevice.cpp
//
// Rob Dobson 2024
//
////////////////////////////////////////////////////////////////////////////////

#include "RaftDevice.h"
#include "RaftUtils.h"

// #define DEBUG_RAFT_DEVICE_CONSTRUCTOR

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

// @brief Setup the device
void RaftDevice::setup()
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

/// @brief Send a binary command to the device
/// @param formatCode Format code for the command
/// @param pData Pointer to the data
/// @param dataLen Length of the data
/// @return RaftRetCode
RaftRetCode RaftDevice::sendCmdBinary(uint32_t formatCode, const uint8_t* pData, uint32_t dataLen)
{
    return RAFT_NOT_IMPLEMENTED;
}

/// @brief Send a JSON command to the device
/// @param jsonCmd JSON command
/// @return RaftRetCode
RaftRetCode RaftDevice::sendCmdJSON(const char* jsonCmd)
{
    return RAFT_NOT_IMPLEMENTED;
}

/// @brief Get binary data from the device
/// @param formatCode format code for the command
/// @param buf (out) buffer to receive the binary data
/// @param bufMaxLen maximum length of data to return
/// @return RaftRetCode
RaftRetCode RaftDevice::getDataBinary(uint32_t formatCode, std::vector<uint8_t>& buf, uint32_t bufMaxLen) const
{
    return RAFT_NOT_IMPLEMENTED;
}

/// @brief Get JSON data from the device
/// @param level Level of data to return
/// @return JSON string
String RaftDevice::getDataJSON(RaftDeviceJSONLevel level) const
{
    return "{}";
}

/// @brief Get named value from the device
/// @param pParam Parameter name
/// @param isFresh (out) true if the value is fresh
/// @return double value
double RaftDevice::getNamedValue(const char* pParam, bool& isFresh) const
{
    isFresh = false;
    return 0.0;
}

/// @brief Check if device has capability
/// @param pCapabilityStr capability string
/// @return true if the device has the capability
bool RaftDevice::hasCapability(const char* pCapabilityStr) const
{
    return false;
}
