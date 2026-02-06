////////////////////////////////////////////////////////////////////////////////
//
// RaftBusDevice.h
//
// Rob Dobson 2024
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftDevice.h"
#include "RaftBus.h"
#include "RaftBusSystem.h"

#define DEBUG_BUS_DEVICE_ID_MATCH

class RaftBusDevice : public RaftDevice
{
public:
   /// @brief Constructor
    RaftBusDevice(const char* pClassName, const char* pDevConfigJson, RaftDeviceID deviceID) :
        RaftDevice(pClassName, pDevConfigJson, deviceID)
    {
    }
    
   /// @brief Destructor
    virtual ~RaftBusDevice()
    {
    }

    /// @brief Check if ID matches that passed in
    /// @param deviceID ID to check
    /// @return true if the device ID matches
    virtual bool idMatches(RaftDeviceID deviceID) const override final
    {
        bool isMatch = (_deviceID == deviceID);
#ifdef DEBUG_BUS_DEVICE_ID_MATCH
        LOG_I(MODULE_PREFIX, "idMatches check %s against device %s - isMatch %s", 
            deviceID.toString(), _deviceID.toString(), isMatch ? "Y" : "N");
#endif
        return isMatch;
    }

    /// @brief Register for device data notifications
    /// @param dataChangeCB Callback for data change
    /// @param minTimeBetweenReportsMs Minimum time between reports (ms)
    /// @param pCallbackInfo Callback info (passed to the callback)
    virtual void registerForDeviceData(RaftDeviceDataChangeCB dataChangeCB, uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo)
    {
        // Register with the bus system
        RaftBus* pBus = raftBusSystem.getBusByNumber(_deviceID.getBusNum());
        RaftBusDevicesIF* pBusDevicesIF = pBus->getBusDevicesIF();
        if (pBusDevicesIF)
            pBusDevicesIF->registerForDeviceData(_deviceID.getAddress(), dataChangeCB, minTimeBetweenReportsMs, pCallbackInfo);
    }

    /// @brief Get the bus name
    /// @return Bus name
    BusNumType getBusNum() const
    {
        return _deviceID.getBusNum();
    }

protected:

    // Debug
    static constexpr const char *MODULE_PREFIX = "RaftBusDevice";
};
