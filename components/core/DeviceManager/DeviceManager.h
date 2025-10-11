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

    /// @brief Get a device by name
    /// @param pDeviceName Name of the device
    /// @return RaftDevice* Pointer to the device or nullptr if not found
    RaftDevice* getDevice(const char* pDeviceName) const;

    /// @brief Register for device data notifications
    /// @param pDeviceName Name of the device
    /// @param dataChangeCB Callback for data change
    /// @param minTimeBetweenReportsMs Minimum time between reports (ms)
    /// @param pCallbackInfo Callback info (passed to the callback)
    void registerForDeviceData(const char* pDeviceName, RaftDeviceDataChangeCB dataChangeCB, 
            uint32_t minTimeBetweenReportsMs = DEFAULT_MIN_TIME_BETWEEN_REPORTS_MS,
            const void* pCallbackInfo = nullptr);

    /// @brief Register for device status changes
    /// @param statusChangeCB Callback for status change
    void registerForDeviceStatusChange(RaftDeviceStatusChangeCB statusChangeCB);

    // Default minimum time between reports
    static constexpr uint32_t DEFAULT_MIN_TIME_BETWEEN_REPORTS_MS = 1000;

    // Get JSON debug string
    virtual String getDebugJSON() const override final;

    // Get code for device connection mode
    static const uint8_t DEVICE_CONN_MODE_DIRECT = 0;
    static const uint8_t DEVICE_CONN_MODE_FIRST_BUS = 1;

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
    std::list<RaftDevice*> _deviceList;
    static const uint32_t DEVICE_LIST_MAX_SIZE = 50;

    // Access mutex (mutable to allow locking in const methods)
    mutable RaftMutex _accessMutex;

    // Device data change record
    class DeviceDataChangeRec
    {
    public:
        DeviceDataChangeRec(const char* pDeviceName, RaftDeviceDataChangeCB dataChangeCB, 
                uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo) :
            deviceName(pDeviceName),
            dataChangeCB(dataChangeCB),
            minTimeBetweenReportsMs(minTimeBetweenReportsMs),
            pCallbackInfo(pCallbackInfo)
        {
        }
        String deviceName;
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
    void busElemStatusCB(RaftBus& bus, const std::vector<BusElemAddrAndStatus>& statusChanges);

    /// @brief Bus operation status callback
    /// @param bus a reference to the bus which has changed status
    /// @param busOperationStatus - indicates bus ok/failing
    void busOperationStatusCB(RaftBus& bus, BusOperationStatus busOperationStatus);

    /// @brief Access to devices' data in JSON format
    /// @return JSON string
    String getDevicesDataJSON() const;

    /// @brief Access to devices' data in binary format
    /// @return Binary data vector
    std::vector<uint8_t> getDevicesDataBinary() const;

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
    /// @param pDevices Pointer to array to receive devices
    /// @param maxDevices Maximum number of devices to return
    /// @return Number of devices
    uint32_t getDeviceListFrozen(RaftDevice** pDevices, uint32_t maxDevices) const;

    /// @brief Find device in device list by ID
    /// @param pDeviceID ID of the device
    /// @return pointer to device if found
    RaftDevice* getDeviceByID(const char* pDeviceName) const;

    /// @brief Call device status change callbacks
    /// @param pDevice Pointer to the device
    /// @param el Bus element address and status
    /// @param newlyCreated True if the device was newly created
    void callDeviceStatusChangeCBs(RaftDevice* pDevice, const BusElemAddrAndStatus& el, bool newlyCreated);

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
    /// @param pDeviceName Name of the device (nullptr for all devices)
    /// @return number of devices registered
    uint32_t registerForDeviceDataChangeCBs(const char* pDeviceName = nullptr);

    /// @brief Device event callback
    /// @param device Device
    /// @param eventName Name of the event
    /// @param eventData Data associated with the event
    void deviceEventCB(RaftDevice& device, const char* eventName, const char* eventData);

    // Last report time
    uint32_t _debugLastReportTimeMs = 0;

    // Debug
    static constexpr const char* MODULE_PREFIX = "DevMan";
};
