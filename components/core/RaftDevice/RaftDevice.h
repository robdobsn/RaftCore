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
#include "RaftDeviceConsts.h"
#include "RaftBusConsts.h"
#include "BusAddrStatus.h"

class RestAPIEndpointManager;
class CommsCoreIF;
class DeviceTypeRecordDynamic;

#define DEBUG_INCLUDE_RAFT_DEVICE_CLASS_NAME

class RaftDevice
{
public:
    /// @brief Construct a new Raft Device object
    /// @param pDevConfigJson JSON configuration for the device
    RaftDevice(const char* pClassName, const char* pDevConfigJson, RaftDeviceID deviceID = RaftDeviceID());
    
    /// @brief Destroy the Raft Device object
    virtual ~RaftDevice();

    /// @brief Set device ID
    /// @param deviceID Device ID to set
    void setDeviceID(RaftDeviceID deviceID)
    {
        _deviceID = deviceID;
    }

    /// @brief Check if ID matches that passed in
    /// @param deviceID ID to check
    /// @return true if the device ID matches
    virtual bool idMatches(RaftDeviceID deviceID) const
    {
        return _deviceID == deviceID;
    }

#ifdef DEBUG_INCLUDE_RAFT_DEVICE_CLASS_NAME
    /// @brief Get the class name of the device
    /// @return Device class name as a string
    virtual String getDeviceClassName() const
    {
        return deviceClassName;
    }
#endif

    /// @brief Get the configured device type
    /// @return Configured device type as a string
    virtual String getConfiguredDeviceType() const
    {
        return configuredDeviceType;
    }

    /// @brief Get the configured device name
    /// @return Configured device name as a string
    virtual String getConfiguredDeviceName() const
    {
        return configuredDeviceName;
    }

    /// @brief Get the device type record for this device so that it can be added to the device type records
    /// @param devTypeRec (out) Device type record
    /// @return true if the device has a device type record
    virtual bool getDeviceTypeRecord(DeviceTypeRecordDynamic& devTypeRec) const
    {
        return false;
    }

    /// @brief Get the device type index for this device
    /// @return Device type index
    virtual uint32_t getDeviceTypeIndex() const
    {
        return deviceTypeIndex;
    }

    /// @brief Set the device type index for this device
    /// @param deviceTypeIndex Device type index
    virtual void setDeviceTypeIndex(uint32_t deviceTypeIndex)
    {
        this->deviceTypeIndex = deviceTypeIndex;
    }

    /// @brief Setup the device
    virtual void setup();

    /// @brief Main loop for the device (called frequently)
    virtual void loop();

    /// @brief Add REST API endpoints
    /// @param endpointManager Manager for REST API endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& endpointManager);

    /// @brief Add communication channels
    /// @param commsCore Core interface for communications
    virtual void addCommsChannels(CommsCoreIF& commsCore);

    /// @brief Post-setup - called after setup of all devices is complete
    virtual void postSetup();

    /// @brief Get time of last device status update
    /// @param includeElemOnlineStatusChanges Include element online status changes in the status update time
    /// @param includePollDataUpdates Include poll data updates in the status update time
    /// @return Time of last device status update in milliseconds
    virtual uint32_t getDeviceInfoTimestampMs(bool includeElemOnlineStatusChanges, bool includePollDataUpdates) const;

    /// @brief Get a hash value representing the current device state for change detection
    /// @return Hash value (only lower 16 bits are used by DeviceManager)
    /// @note Default implementation returns getDeviceInfoTimestampMs(true, true)
    ///       Override this to provide custom state change detection based on device-specific data
    virtual uint32_t getDeviceStateHash() const
    {
        return getDeviceInfoTimestampMs(true, true);
    }

    /// @brief Get the device status as JSON
    /// @return JSON string
    virtual String getStatusJSON() const;

    /// @brief Get the device status as binary
    /// @return Binary data
    virtual std::vector<uint8_t> getStatusBinary() const;

    /// @brief Generate a binary data message
    /// @param binData (out) Binary data
    /// @param busNumber Bus number (0-63)
    /// @param address Address of the device
    /// @param deviceTypeIndex Index of the device type
    /// @param onlineState Device online state
    /// @param deviceMsgData Device msg data
    /// @return true if created ok
    static bool genBinaryDataMsg(std::vector<uint8_t>& binData, 
        uint8_t busNumber, 
        BusElemAddrType address, 
        uint16_t deviceTypeIndex, 
        DeviceOnlineState onlineState, 
        std::vector<uint8_t> deviceMsgData);

    /// @brief Get device debug info JSON
    /// @return JSON string
    virtual String getDebugJSON(bool includeBraces) const;

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
    virtual double getNamedValue(const char* pParam, bool& isFresh) const
    {
        isFresh = false;
        return 0.0;
    }

    /// @brief Set named value in the device
    /// @param pParam Parameter name
    /// @param value Value to set
    /// @return true if set ok
    virtual bool setNamedValue(const char* pParam, double value)
    {
        return false;
    }

        /// @brief Get named string
    /// @param pParam Parameter name
    /// @param isValid (out) true if value is valid
    /// @return Named string
    virtual String getNamedString(const char* pParam, bool& isValid) const
    {
        isValid = false;
        return "";
    }

    /// @brief Set named string
    /// @param pParam Parameter name
    /// @param value Value to set
    /// @return true if successful, false otherwise
    virtual bool setNamedString(const char* pParam, const char* value)
    {
        return false;
    }

    /// @brief Check if device has capability
    /// @param pCapabilityStr capability string
    /// @return true if the device has the capability
    virtual bool hasCapability(const char* pCapabilityStr) const;

    /// @brief Send JSON command to device with optional error message
    /// @param jsonCmd JSON command string
    /// @param respMsg Optional pointer to string for error message (default nullptr)
    /// @return RaftRetCode
    virtual RaftRetCode sendCmdJSON(const char* jsonCmd, String* respMsg)
    {
        // Default calls version without respMsg for backwards compatibility
        return sendCmdJSON(jsonCmd);
    }

    /// @brief Handle device status change
    /// @param addrStatus Address and status of the bus element that changed
    virtual void handleStatusChange(const BusAddrStatus& addrStatus)
    {
    }

    /// @brief Register for device data notifications
    /// @param dataChangeCB Callback for data change
    /// @param minTimeBetweenReportsMs Minimum time between reports (ms)
    /// @param pCallbackInfo Callback info (passed to the callback)
    virtual void registerForDeviceData(RaftDeviceDataChangeCB dataChangeCB, uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo);
        
    /// @brief Register for device events
    /// @param eventCB Callback for device events
    virtual void registerForDeviceStatusChange(RaftDeviceEventCB eventCB)
    {
    }

    /// @brief Get the ID of the device
    /// @return Device ID
    RaftDeviceID getDeviceID() const
    {
        return _deviceID;
    }

    /// @brief Get the bus name
    /// @return Bus name
    BusNumType getBusNum() const
    {
        return _deviceID.getBusNum();
    }
    
protected:
    // Device configuration
    RaftJson deviceConfig;

#ifdef DEBUG_INCLUDE_RAFT_DEVICE_CLASS_NAME
    // Device class
    String deviceClassName;
#endif

    // Configured device type
    String configuredDeviceType;

    // Configured device name
    String configuredDeviceName;

    // Device type record index
    DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;

    // Device ID
    RaftDeviceID _deviceID;

    // Debug
    static constexpr const char *MODULE_PREFIX = "RaftDevice";
};
