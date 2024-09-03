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
        if (!_deviceTypeName)
            return;
        // Store values
        _deviceTypeName = deviceTypeName;
        _addresses = addresses ? addresses : "";
        _detectionValues = detectionValues ? detectionValues : "";
        _initValues = initValues ? initValues : "";
        _pollInfo = pollInfo ? pollInfo : "";
        _pollDataSizeBytes = pollDataSizeBytes;
        if (devInfoJson)
        {
            _devInfoJson.resize(strlen(devInfoJson) + 1);
            strlcpy(_devInfoJson.data(), devInfoJson, _devInfoJson.size());
        }

        // Store function
        _pollResultDecodeFn = pollResultDecodeFn;

        // Update device type record
        _devTypeRec = {
            _deviceTypeName.c_str(),
            _addresses.c_str(),
            _detectionValues.c_str(),
            _initValues.c_str(),
            _pollInfo.c_str(),
            _pollDataSizeBytes,
            _devInfoJson.data(),
            _pollResultDecodeFn
        };
    }

    /// @brief Get device type record
    const DeviceTypeRecord* getDeviceTypeRecord() const
    {
        return &_devTypeRec;
    }

    /// @brief Get device type name matches
    bool nameMatches(const DeviceTypeRecordDynamic& other) const
    {
        return _deviceTypeName == other._deviceTypeName;
    }

private:
    // Device type storage
    String _deviceTypeName;
    String _addresses;
    String _detectionValues;
    String _initValues;
    String _pollInfo;
    uint16_t _pollDataSizeBytes;
    std::vector<char, SpiramAwareAllocator<char>> _devInfoJson;
    DeviceTypeRecordDecodeFn _pollResultDecodeFn;

    // Device type record (which contains pointers to the above)
    DeviceTypeRecord _devTypeRec;
};
