////////////////////////////////////////////////////////////////////////////////
//
// DeviceManager.h
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftArduino.h"
#include "RaftSysMod.h"
#include "RaftBusSystem.h"
#include "DeviceFactory.h"
#include "BusRequestResult.h"

class APISourceInfo;

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

    // Setup
    virtual void setup() override final;
    
    // Post-setup - called after setup of all sysMods complete
    virtual void postSetup() override final;

    // Loop (called frequently)
    virtual void loop() override final;

    // Add endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& pEndpoints) override final;

private:

    // List of instantiated devices
    std::list<RaftDevice*> _deviceList;

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

    // Setup device instances
    void setupDevices(const char* pConfigPrefix, RaftJsonIF& devManConfig);
    
    // Bus operation and status functions
    void busElemStatusCB(RaftBus& bus, const std::vector<BusElemAddrAndStatus>& statusChanges);
    void busOperationStatusCB(RaftBus& bus, BusOperationStatus busOperationStatus);

    // Access to devices' data
    String getDevicesDataJSON() const;
    std::vector<uint8_t> getDevicesDataBinary() const;
    void getDevicesHash(std::vector<uint8_t>& stateHash) const;

    // API callback
    RaftRetCode apiDevMan(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);

    // Callback for command results
    void cmdResultReportCallback(BusRequestResult& reqResult);

    // Last report time
    uint32_t _debugLastReportTimeMs = 0;

    // Debug
    static constexpr const char* MODULE_PREFIX = "DevMan";
};
