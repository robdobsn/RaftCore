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
        RaftDevice(pClassName, pDevConfigJson, address),
        _busName(pBusName)
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
        int indexOfX = addrStr.indexOf('x');
        if (indexOfX >= 0)
            addrStr = addrStr.substring(indexOfX + 1);
        String mux = "";
        splitPos = addrStr.indexOf('@');
        if (splitPos >= 0)
        {
            mux = addrStr.substring(splitPos+1);
            addrStr = addrStr.substring(0, splitPos);
        }
        // LOG_I("RaftBusDevice", "idMatches busNameStr %s busName %s addrStr %s address %s", 
        //                 busNameStr.c_str(), _busName.c_str(), (mux + addrStr).c_str(), String(_address, 16).c_str());

        if (busNameStr != _busName)
            return false;
        return String(_address, 16).equalsIgnoreCase(addrStr);
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

    /// @brief Get the bus name
    /// @return Bus name
    String getBusName() const
    {
        return _busName;
    }

protected:
    // Bus name
    String _busName;

    // Debug
    static constexpr const char *MODULE_PREFIX = "RaftBusDevice";
};
