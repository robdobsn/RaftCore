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
// #define DEBUG_BINARY_DEVICE_DATA 

/// @brief Construct a new Raft Device object
/// @param pDevConfigJson JSON configuration for the device
RaftDevice::RaftDevice(const char* pClassName, const char* pDevConfigJson) :
        deviceConfig(pDevConfigJson), deviceClassName(pClassName)
{
    // Device name
    deviceName = deviceConfig.getString("name", "UNKNOWN");

    // Init publish device type (default to class name)
    publishDeviceType = deviceConfig.getString("type", pClassName);
    
#ifdef DEBUG_RAFT_DEVICE_CONSTRUCTOR
    LOG_I(MODULE_PREFIX, "RaftDevice class %s publishDeviceType %s devConfig %s", 
            pClassName, publishDeviceType.c_str(), pDevConfigJson);
#endif
}

/// @brief Destroy the Raft Device object
RaftDevice::~RaftDevice()
{
}

/// @brief Setup the device
void RaftDevice::setup()
{
}

/// @brief Main loop for the device
void RaftDevice::loop()
{
}

/// @brief Add REST API endpoints
/// @param endpointManager Manager for REST API endpoints
void RaftDevice::addRestAPIEndpoints(RestAPIEndpointManager& endpointManager)
{
}

/// @brief Add communication channels
/// @param commsCore Core interface for communications
void RaftDevice::addCommsChannels(CommsCoreIF& commsCore)
{
}

/// @brief Post-setup - called after setup of all devices is complete
void RaftDevice::postSetup()
{
}

/// @brief Get time of last device status update
/// @param includeElemOnlineStatusChanges Include element online status changes in the status update time
/// @param includePollDataUpdates Include poll data updates in the status update time
/// @return Time of last device status update in milliseconds
uint32_t RaftDevice::getDeviceInfoTimestampMs(bool includeElemOnlineStatusChanges, bool includePollDataUpdates) const
{
    return 0;
}

/// @brief Get the device status as JSON
/// @return JSON string
String RaftDevice::getStatusJSON() const
{
    return "{}";
}

/// @brief Get the device status as binary
/// @return Binary data
std::vector<uint8_t> RaftDevice::getStatusBinary() const
{
    return std::vector<uint8_t>();
}

/// @brief Generate a binary data message
/// @param binData (out) Binary data
/// @param connMode Connection mode (inc bus number / id)
/// @param address Address of the device
/// @param deviceTypeIndex Index of the device type
/// @param isOnline true if the device is online
/// @param deviceMsgData Device msg data
/// @return true if created ok
bool RaftDevice::genBinaryDataMsg(std::vector<uint8_t>& binData, 
    uint8_t connMode, 
    BusElemAddrType address, 
    uint16_t deviceTypeIndex, 
    bool isOnline, 
    std::vector<uint8_t> deviceMsgData)
    {
        // Reserve space
        uint32_t msgLen = deviceMsgData.size() + 7;
        const uint32_t origSize = binData.size();
        binData.reserve(origSize + 2 + msgLen);

        // Overall length of message section (excluding length bytes)
        binData.push_back((msgLen >> 8) & 0xff);
        binData.push_back(msgLen & 0xff);

        // Start with connection mode byte (MSB indicates online/offline)
        binData.push_back(connMode | (isOnline ? 0x80 : 0));

        // The address (32 bits)
        binData.push_back((address >> 24) & 0xff);
        binData.push_back((address >> 16) & 0xff);
        binData.push_back((address >> 8) & 0xff);
        binData.push_back(address & 0xff);

        // Add device type index
        binData.push_back((deviceTypeIndex >> 8) & 0xff);
        binData.push_back(deviceTypeIndex & 0xff);

        // Add binary data
        binData.insert(binData.end(), deviceMsgData.begin(), deviceMsgData.end());

#ifdef DEBUG_BINARY_DEVICE_DATA
        LOG_I(MODULE_PREFIX, "genBinaryDataMsg origLen %d deviceMsgLen %d binDataLen %d ", 
            origSize, deviceMsgData.size(), binData.size());
#endif

        return true;
    }

/// @brief Get device debug info JSON
/// @return JSON string
String RaftDevice::getDebugJSON(bool includeBraces) const
{
    return includeBraces ? "{}" : "";
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
