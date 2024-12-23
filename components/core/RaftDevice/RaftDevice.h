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

class RestAPIEndpointManager;
class CommsCoreIF;
class DeviceTypeRecordDynamic;

class RaftDevice
{
public:
    /// @brief Construct a new Raft Device object
    /// @param pDevConfigJson JSON configuration for the device
    RaftDevice(const char* pClassName, const char* pDevConfigJson);
    
    /// @brief Destroy the Raft Device object
    virtual ~RaftDevice();

    /// @brief Check if ID matches that passed in
    /// @param pDeviceId Device ID to check
    /// @return true if the device ID matches
    virtual bool idMatches(const char* pDeviceId) const
    {
        return deviceName.equals(pDeviceId);
    }

    /// @brief Get the name of the device instance
    /// @return Device name as a string
    virtual String getDeviceName() const
    {
        return deviceName;
    }

    /// @brief Get the class name of the device
    /// @return Device class name as a string
    virtual String getDeviceClassName() const
    {
        return deviceClassName;
    }

    /// @brief Get the publish device type
    /// @return Publish device type as a string
    virtual String getPublishDeviceType() const
    {
        return publishDeviceType;
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

    /// @brief Get the device status as JSON
    /// @return JSON string
    virtual String getStatusJSON() const;

    /// @brief Get the device status as binary
    /// @return Binary data
    virtual std::vector<uint8_t> getStatusBinary() const;

    /// @brief Generate a binary data message
    /// @param binData (out) Binary data
    /// @param connMode Connection mode (inc bus number / id)
    /// @param address Address of the device
    /// @param deviceTypeIndex Index of the device type
    /// @param isOnline true if the device is online
    /// @param deviceMsgData Device msg data
    /// @return true if created ok
    static bool genBinaryDataMsg(std::vector<uint8_t>& binData, 
        uint8_t connMode, 
        BusElemAddrType address, 
        uint16_t deviceTypeIndex, 
        bool isOnline, 
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
    virtual double getNamedValue(const char* pParam, bool& isFresh) const;

    /// @brief Check if device has capability
    /// @param pCapabilityStr capability string
    /// @return true if the device has the capability
    virtual bool hasCapability(const char* pCapabilityStr) const;

    /// @brief Handle device status change
    /// @param isChangeToOnline true if the device has changed to online
    /// @param isChangeToOffline true if the device has changed to offline
    /// @param isNewlyIdentified true if the device is newly identified
    /// @param deviceTypeIndex index of the device type
    virtual void handleStatusChange(bool isChangeToOnline, bool isChangeToOffline, bool isNewlyIdentified, uint32_t deviceTypeIndex)
    {
    }

    /// @brief Register for device data notifications
    /// @param dataChangeCB Callback for data change
    /// @param minTimeBetweenReportsMs Minimum time between reports (ms)
    /// @param pCallbackInfo Callback info (passed to the callback)
    virtual void registerForDeviceData(RaftDeviceDataChangeCB dataChangeCB, uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo)
    {
    }

protected:
    // Device configuration
    RaftJson deviceConfig;

    // Device name
    String deviceName;

    // Device class
    String deviceClassName;

    // Publish device type
    String publishDeviceType;

    // Device type record index
    uint32_t deviceTypeIndex = 0;

    // Debug
    static constexpr const char *MODULE_PREFIX = "RaftDevice";
};
