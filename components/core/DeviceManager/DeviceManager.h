////////////////////////////////////////////////////////////////////////////////
//
// DeviceManager.h
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftSysMod.h"
#include "BusRequestResult.h"
#include "RaftDeviceConsts.h"
#include "RaftThreading.h"

class APISourceInfo;
class RaftBus;
class RaftDevice;
class DemoDevice;

class DeviceManager : public RaftSysMod
{
public:
    DeviceManager(const char *pModuleName, RaftJsonIF& sysConfig);
    virtual ~DeviceManager();

    /// @brief Create function (for use by SysManager factory)
    /// @param pModuleName - name of the module
    /// @param sysConfig - system configuration
    /// @return RaftSysMod* - pointer to the created module
    static RaftSysMod* create(const char* pModuleName, RaftJsonIF& sysConfig)
    {
        return new DeviceManager(pModuleName, sysConfig);
    }

    /// @brief Find device in device list by ID
    /// @param deviceID Device identifier
    /// @return pointer to device if found
    RaftDevice* getDevice(RaftDeviceID deviceID) const;

    /// @brief Get device by string lookup using device ID as string
    /// @param deviceStr Device ID string (ID as string)
    /// @return pointer to device if found, nullptr otherwise
    RaftDevice* getDeviceByStringLookup(const String& deviceStr) const;

    /// @brief Register for device data notifications
    /// @param deviceID Device identifier
    /// @param dataChangeCB Callback for data change
    /// @param minTimeBetweenReportsMs Minimum time between reports (ms)
    /// @param pCallbackInfo Callback info (passed to the callback)
    void registerForDeviceData(RaftDeviceID deviceID, RaftDeviceDataChangeCB dataChangeCB, 
            uint32_t minTimeBetweenReportsMs = DEFAULT_MIN_TIME_BETWEEN_REPORTS_MS,
            const void* pCallbackInfo = nullptr);

    /// @brief Register for device status changes
    /// @param statusChangeCB Callback for status change
    void registerForDeviceStatusChange(RaftDeviceStatusChangeCB statusChangeCB);

    // Default minimum time between reports
    static constexpr uint32_t DEFAULT_MIN_TIME_BETWEEN_REPORTS_MS = 1000;

    // Get JSON debug string
    virtual String getDebugJSON() const override final;

    // Named value access (routes to devices using "DeviceName.paramName" syntax)
    virtual double getNamedValue(const char* pValueName, bool& isValid) override;
    virtual bool setNamedValue(const char* pValueName, double value) override;
    virtual String getNamedString(const char* pValueName, bool& isValid) override;
    virtual bool setNamedString(const char* pValueName, const char* value) override;

    // JSON command routing (routes to device specified in "device" field)
    virtual RaftRetCode receiveCmdJSON(const char* cmdJSON) override;

protected:

    /// @brief Setup
    virtual void setup() override final;
    
    /// @brief Post-setup - called after setup of all sysMods complete
    virtual void postSetup() override final;

    /// @brief Loop (called frequently)
    virtual void loop() override final;

    /// @brief Add REST API endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& pEndpoints) override final;

private:

    // List of instantiated devices
    struct DevicePtrAndOnline
    {
        RaftDevice* pDevice = nullptr;
        bool isOnline = false;
    };
    
    std::list<DevicePtrAndOnline> _deviceList;
    static const uint32_t DEVICE_LIST_MAX_SIZE = 100;

    // Access mutex (mutable to allow locking in const methods)
    mutable RaftMutex _accessMutex;

    // Device data change record
    class DeviceDataChangeRec
    {
    public:
        DeviceDataChangeRec(RaftDeviceID deviceID, RaftDeviceDataChangeCB dataChangeCB, 
                uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo) :
            deviceID(deviceID),
            dataChangeCB(dataChangeCB),
            minTimeBetweenReportsMs(minTimeBetweenReportsMs),
            pCallbackInfo(pCallbackInfo)
        {
        }
        RaftDeviceID deviceID;
        RaftDeviceDataChangeCB dataChangeCB = nullptr;
        uint32_t minTimeBetweenReportsMs = 1000;
        uint32_t lastReportTime = 0;
        const void* pCallbackInfo = nullptr;
    };

    // Device data change callbacks
    std::list<DeviceDataChangeRec> _deviceDataChangeCBList;

    // Device status change callbacks
    std::list<RaftDeviceStatusChangeCB> _deviceStatusChangeCBList;

    /// @brief Setup device instances
    /// @param pConfigPrefix Prefix for configuration
    /// @param devManConfig Device manager configuration
    void setupDevices(const char* pConfigPrefix, RaftJsonIF& devManConfig);

    /// @brief Setup a single device
    /// @param pDeviceClass class of the device to setup
    /// @param devConfig configuration for the device
    /// @return RaftDevice* pointer to the created device or nullptr if failed
    RaftDevice* setupDevice(const char* pDeviceClass, RaftJsonIF& devConfig);
    
    /// @brief Bus element status callback
    /// @param bus a reference to the bus which has elements with changed status
    /// @param statusChanges - list of status changes
    void busElemStatusCB(RaftBus& bus, const std::vector<BusAddrStatus>& statusChanges);

    /// @brief Bus operation status callback
    /// @param bus a reference to the bus which has changed status
    /// @param busOperationStatus - indicates bus ok/failing
    void busOperationStatusCB(RaftBus& bus, BusOperationStatus busOperationStatus);

    /// @brief Access to devices' data in JSON format
    /// @param pTopicName topic name to include in the JSON
    /// @return JSON string
    String getDevicesDataJSON(const char* pTopicName) const;

    /// @brief Access to devices' data in binary format
    /// @param pTopicName topic name to include in the data
     /// @return Binary data vector
    std::vector<uint8_t> getDevicesDataBinary(const char* pTopicName) const;

    /// @brief Get devices' status hash
    /// @param stateHash hash of the currently available data
    void getDevicesHash(std::vector<uint8_t>& stateHash) const;

    /// @brief API callback
    /// @param reqStr Request string
    /// @param respStr Response string
    /// @param sourceInfo Source of the API request
    /// @return RaftRetCode Return code
    RaftRetCode apiDevMan(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);

    /// @brief Callback for command results
    /// @param reqResult Result of the command
    void cmdResultReportCallback(BusRequestResult& reqResult);

    /// @brief Get device list frozen
    /// @param pDeviceList (out) list of devices (must be maxNumDevices long)
    /// @param maxNumDevices maximum number of devices to return
    /// @param onlyOnline true to only return online devices
    /// @param pDeviceOnlineArray pointer to array of device online flags (may be nullptr) - must be maxNumDevices long
    /// @return Number of devices
    uint32_t getDeviceListFrozen(RaftDevice** pDevices, uint32_t maxDevices, bool onlyOnline, bool *pDeviceOnlineArray = nullptr) const;

    /// @brief Call device status change callbacks
    /// @param pDevice Pointer to the device
    /// @param addrStatus Bus element address and status
    void callDeviceStatusChangeCBs(RaftDevice* pDevice, const BusAddrStatus& addrStatus);

    // Device data change record temporary
    struct DeviceDataChangeRecTmp
    {
        RaftDevice* pDevice = nullptr;
        RaftDeviceDataChangeCB dataChangeCB = nullptr;
        uint32_t minTimeBetweenReportsMs = 1000;
        const void* pCallbackInfo = nullptr;
    };

    /// @brief Get device data change temporary records
    /// @param recList List of temporary records
    void getDeviceDataChangeRecTmp(std::list<DeviceDataChangeRecTmp>& recList);

    /// @brief Register for device data change callbacks
    /// @param deviceID ID of device (isAnyDevice() true for all devices)
    /// @return number of devices registered for data change callbacks
    uint32_t registerForDeviceDataChangeCBs(RaftDeviceID deviceID);

    /// @brief Device event callback
    /// @param device Device
    /// @param eventName Name of the event
    /// @param eventData Data associated with the event
    void deviceEventCB(RaftDevice& device, const char* eventName, const char* eventData);

    /// @brief Get bus by string lookup where string is either bus name or bus number as string
    /// @param busStr Bus string (name or number)
    /// @return pointer to bus if found, nullptr otherwise
    RaftBus* getBusByNameOrNumberString(const String& busStr) const;

    // Last report time
    uint32_t _debugLastReportTimeMs = 0;

    // Debug
    static constexpr const char* MODULE_PREFIX = "DevMan";
};
