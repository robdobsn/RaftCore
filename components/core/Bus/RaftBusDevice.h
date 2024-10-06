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

class RaftBusDevice : public RaftDevice
{
public:
   /// @brief Constructor
    RaftBusDevice(const char* pBusName, BusElemAddrType address, const char* pClassName, const char* pDevConfigJson) :
        RaftDevice(pClassName, pDevConfigJson),
        _busName(pBusName),
        _address(address)
    {
    }
    
   /// @brief Destructor
    virtual ~RaftBusDevice()
    {
    }

   /// @brief Check if ID matches that passed in
    /// @param pDeviceId Device ID to check
    /// @return true if the device ID matches
    virtual bool idMatches(const char* pDeviceId) const override final
    {
        // Split name on _ and check for first part matching bus name and second matching address in hex
        String devNameStr(pDeviceId);
        int splitPos = devNameStr.indexOf('_');
        if (splitPos < 0)
            return false;
        String busNameStr = devNameStr.substring(0, splitPos);
        String addrStr = devNameStr.substring(splitPos + 1);
        if (busNameStr != _busName)
            return false;
        return String(_address, 16) == addrStr;
    }

    /// @brief Register for device data notifications
    /// @param dataChangeCB Callback for data change
    /// @param minTimeBetweenReportsMs Minimum time between reports (ms)
    /// @param pCallbackInfo Callback info (passed to the callback)
    virtual void registerForDeviceData(RaftDeviceDataChangeCB dataChangeCB, uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo)
    {
        // Register with the bus system
        RaftBus* pBus = raftBusSystem.getBusByName(_busName);
        RaftBusDevicesIF* pBusDevicesIF = pBus->getBusDevicesIF();
        if (pBusDevicesIF)
            pBusDevicesIF->registerForDeviceData(_address, dataChangeCB, minTimeBetweenReportsMs, pCallbackInfo);
    }    

protected:
    // Bus name
    String _busName;

    // Bus address
    BusElemAddrType _address = 0;

    // Debug
    static constexpr const char *MODULE_PREFIX = "RaftBusDevice";
};
