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
        deviceTypeName_ = deviceTypeName;
        addresses_ = addresses ? addresses : "";
        detectionValues_ = detectionValues ? detectionValues : "";
        initValues_ = initValues ? initValues : "";
        pollInfo_ = pollInfo ? pollInfo : "";
        pollDataSizeBytes_ = pollDataSizeBytes;
        if (devInfoJson)
        {
            devInfoJson_.resize(strlen(devInfoJson) + 1);
            strlcpy(devInfoJson_.data(), devInfoJson, devInfoJson_.size());
        }
        else
        {
            devInfoJson_.resize(1);
            devInfoJson_[0] = 0;
        }

        // Store function
        pollResultDecodeFn_ = pollResultDecodeFn;
    }

    /// @brief Get device type record
    /// @param devTypeRec (out) device type record
    /// @return true if device type found
    bool getDeviceTypeRecord(DeviceTypeRecord& devTypeRec) const
    {
        // Check if in range
        if (deviceTypeName_.length() == 0)
            return false;

        // Update device type record
        devTypeRec.deviceType = deviceTypeName_.c_str();
        devTypeRec.addresses = addresses_.c_str();
        devTypeRec.detectionValues = detectionValues_.c_str();
        devTypeRec.initValues = initValues_.c_str();
        devTypeRec.pollInfo = pollInfo_.c_str();
        devTypeRec.pollDataSizeBytes = pollDataSizeBytes_;
        devTypeRec.devInfoJson = devInfoJson_.data();
        devTypeRec.pollResultDecodeFn = pollResultDecodeFn_;

        return true;
    }

    /// @brief Get device type name matches
    bool nameMatches(const DeviceTypeRecordDynamic& other) const
    {
        return deviceTypeName_ == other.deviceTypeName_;
    }

    // Device type storage
    String deviceTypeName_;
    String addresses_;
    String detectionValues_;
    String initValues_;
    String pollInfo_;
    uint16_t pollDataSizeBytes_ = 0;
    std::vector<char, SpiramAwareAllocator<char>> devInfoJson_;
    DeviceTypeRecordDecodeFn pollResultDecodeFn_ = nullptr;
};
