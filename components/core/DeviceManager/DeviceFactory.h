/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Factory for Raft Devices
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <list>
#include <vector>
#include "RaftArduino.h"
#include "RaftJsonIF.h"

class RaftDevice;

// Device create function
typedef RaftDevice* (*RaftDeviceCreateFn)(const char* pClassName, const char* pDevConfigJson);

class DeviceFactory
{
public:

    /// Forward definition
    class RaftDeviceClassDef;

    ///////////////////////////////////////////////////////////////////////
    /// @brief Register a Device in the factory
    /// @param pClassName name of the Raft Device class
    /// @param pCreateFn Function to create the device
    void registerDevice(const char* pClassName, RaftDeviceCreateFn pCreateFn)
    {
        // Check valid
        if (!pClassName || !pCreateFn)
            return;

        // Find if already registered and change the create function if so
        for (auto& deviceClassDef : raftDeviceClassDefs)
        {
            if (deviceClassDef.name.equals(pClassName))
            {
#ifdef WARN_ON_DEVICE_CLASS_NOT_FOUND
                LOG_W("DeviceFactory", "registerDevice %s replacing create function", pClassName);
#endif
                // Replace existing create function
                if (deviceClassDef.pCreateFn != pCreateFn)
                {
                    deviceClassDef.pCreateFn = pCreateFn;
                }
                return;
            }
        }

        // If not found, add a new device class definition
#ifdef DEBUG_DEVICE_FACTORY
        LOG_I("DeviceFactory", "registerDevice %s", pClassName);
#endif
        raftDeviceClassDefs.push_back(RaftDeviceClassDef(pClassName, pCreateFn));
    }

    ///////////////////////////////////////////////////////////////////////
    /// @brief Find a Device class in the factory
    /// @param pClassName name of the Raft Device class
    /// @return Pointer to the Device class definition or nullptr if not found
    const RaftDeviceClassDef* findDeviceClass(const char* pClassName)
    {
        for (auto& deviceClassDef : raftDeviceClassDefs)
        {
            if (deviceClassDef.name.equals(pClassName))
                return &deviceClassDef;
        }
        return nullptr;
    }

    class RaftDeviceClassDef
    {
    public:
        ///////////////////////////////////////////////////////////////////////
        /// @brief Constructor
        /// @param pClassName name of the Raft Device class
        /// @param pCreateFn Function to create the device
        RaftDeviceClassDef(const char* pClassName, RaftDeviceCreateFn pCreateFn)
        {
            this->name = pClassName;
            this->pCreateFn = pCreateFn;
        }
        String name;
        RaftDeviceCreateFn pCreateFn = nullptr;
    };

    // List of Device classes
    std::list<RaftDeviceClassDef> raftDeviceClassDefs;
};

// Access to single instance
extern DeviceFactory deviceFactory;

