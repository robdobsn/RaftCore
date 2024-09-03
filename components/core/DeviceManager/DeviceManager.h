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

protected:

    // Setup
    virtual void setup() override final;
    
    // Post-setup - called after setup of all sysMods complete
    virtual void postSetup() override final;

    // Loop (called frequently)
    virtual void loop() override final;

    // Get JSON debug string
    virtual String getDebugJSON() const override final;

    // Add endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& pEndpoints) override final;

private:

    // List of instantiated devices
    std::list<RaftDevice*> _deviceList;

    // Setup device instances
    void setupDevices(const char* pConfigPrefix, RaftJsonIF& devManConfig);
    
    // Bus operation and status functions
    void busElemStatusCB(RaftBus& bus, const std::vector<BusElemAddrAndStatus>& statusChanges);
    void busOperationStatusCB(RaftBus& bus, BusOperationStatus busOperationStatus);

    // Access to devices' data
    String getDevicesDataJSON() const;
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
