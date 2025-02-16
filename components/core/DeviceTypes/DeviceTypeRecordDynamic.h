/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftCore Device Type Record
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "DeviceTypeRecord.h"
#include "SpiramAwareAllocator.h"

class DeviceTypeRecordDynamic
{
public:
    /// @brief Constructor
    DeviceTypeRecordDynamic()
    {
    }

    /// @brief Constructor
    /// @param deviceTypeName 
    /// @param addresses - comma separated list of addresses (e.g. "0x10,0x11") - ranges are not accepted
    /// @param detectionValues 
    /// @param initValues 
    /// @param pollInfo 
    /// @param pollDataSizeBytes 
    /// @param devInfoJson 
    /// @param pollResultDecodeFn 
    DeviceTypeRecordDynamic(const char* deviceTypeName,
            const char* addresses,
            const char* detectionValues,
            const char* initValues,
            const char* pollInfo,
            uint16_t pollDataSizeBytes,
            const char* devInfoJson,
            DeviceTypeRecordDecodeFn pollResultDecodeFn)
    {
        // Check valid
        if (!deviceTypeName)
            return;
        // Store values
        this->deviceTypeName = deviceTypeName;
        this->addresses = addresses ? addresses : "";
        this->detectionValues = detectionValues ? detectionValues : "";
        this->initValues = initValues ? initValues : "";
        this->pollInfo = pollInfo ? pollInfo : "";
        this->pollDataSizeBytes = pollDataSizeBytes;
        this->devInfoJson = devInfoJson ? devInfoJson : "";

        // Store decode function
        this->pollResultDecodeFn = pollResultDecodeFn;
    }

    /// @brief Get device type record
    /// @param devTypeRec (out) device type record
    /// @return true if device type found
    bool getDeviceTypeRecord(DeviceTypeRecord& devTypeRec) const
    {
        // Check if in range
        if (deviceTypeName.length() == 0)
            return false;

        // Update device type record
        devTypeRec.deviceType = deviceTypeName.c_str();
        devTypeRec.addresses = addresses.c_str();
        devTypeRec.detectionValues = detectionValues.c_str();
        devTypeRec.initValues = initValues.c_str();
        devTypeRec.pollInfo = pollInfo.c_str();
        devTypeRec.pollDataSizeBytes = pollDataSizeBytes;
        devTypeRec.devInfoJson = devInfoJson.data();
        devTypeRec.pollResultDecodeFn = pollResultDecodeFn;

        return true;
    }

    /// @brief Get device type name matches
    bool nameMatches(const DeviceTypeRecordDynamic& other) const
    {
        return deviceTypeName == other.deviceTypeName;
    }

    // Device type storage
    String deviceTypeName;
    String addresses;
    String detectionValues;
    String initValues;
    String pollInfo;
    uint16_t pollDataSizeBytes = 0;
    SpiramAwareString devInfoJson;
    DeviceTypeRecordDecodeFn pollResultDecodeFn = nullptr;
};
