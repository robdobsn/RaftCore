/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftCore Device Type Record (extended)
//
// Holds the data of an extended device type record: one added at runtime (e.g. by a static device),
// as opposed to a base record, which is generated at build time and held in flash
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
    /// @param deviceTypeKey device type key
    /// @param addresses - comma separated list of addresses and/or ranges (e.g. "0x10,0x11" or "0x10-0x17")
    /// @param detectionValues 
    /// @param initValues 
    /// @param pollInfo 
    /// @param pollDataSizeBytes 
    /// @param devInfoJson 
    /// @param pollResultDecodeFn 
    DeviceTypeRecordDynamic(const char* deviceTypeKey,
            const char* addresses,
            const char* detectionValues,
            const char* initValues,
            const char* pollInfo,
            uint16_t pollDataSizeBytes,
            const char* devInfoJson,
            DeviceTypeRecordDecodeFn pollResultDecodeFn)
    {
        // Check valid
        if (!deviceTypeKey)
            return;
        // Store values
        deviceTypeName = deviceTypeKey;
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
        devTypeRec.scanPriority = scanPriority;
        devTypeRec.flags = flags;
        devTypeRec.identPriority = identPriority;
        devTypeRec.hwTypeId = hwTypeId;

        return true;
    }

    /// @brief Check whether the device type keys match
    bool nameMatches(const DeviceTypeRecordDynamic& other) const
    {
        return deviceTypeName == other.deviceTypeName;
    }

    // Device type storage (deviceTypeName holds the device type key - the name is kept for compatibility)
    String deviceTypeName;
    String addresses;
    String detectionValues;
    String initValues;
    String pollInfo;
    uint16_t pollDataSizeBytes = 0;
    SpiramAwareString devInfoJson;
    DeviceTypeRecordDecodeFn pollResultDecodeFn = nullptr;

    // Identification and scanning (see DeviceTypeRecord). An extended record's addresses are scanned at high
    // priority unless set otherwise
    uint8_t scanPriority = DeviceTypeRecord::SCAN_PRIORITY_HIGH;
    uint8_t flags = 0;
    int8_t identPriority = 0;
    uint16_t hwTypeId = 0;

    /// @brief Set the hardware type ID (see DeviceTypeRecord::hwTypeId)
    void setHwTypeId(uint16_t id)
    {
        hwTypeId = id;
        flags |= DeviceTypeRecord::FLAG_HAS_HW_TYPE_ID;
    }
};
