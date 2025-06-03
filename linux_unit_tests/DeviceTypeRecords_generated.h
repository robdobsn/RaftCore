/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Type Records Generated Stub (FOR UNIT TESTING ONLY)
// 
// This is a stub implementation used only for unit testing.
// It is not used in the main build.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "DeviceTypeRecord.h"
#include "RaftBusConsts.h"

// Dummy base device type records
static const DeviceTypeRecord baseDevTypeRecords[] = {
    { 
        "TestDevice",  // deviceType
        "0x42",        // addresses
        "0x1234=0xABCD", // detectionValues
        "0x5678=0x9012", // initValues
        "{\"c\":\"0xA1=0xB2\",\"i\":100,\"s\":10}", // pollInfo
        4,            // pollDataSizeBytes
        "{\"name\":\"Test Device\"}", // devInfoJson
        nullptr       // pollResultDecodeFn
    },
    {
        "SCD40",       // deviceType
        "0x62",        // addresses
        "0x3682=XXXX{crc:crc-sensirion-8,2}XXXX{crc:crc-sensirion-8,2}XXXX{crc:crc-sensirion-8,2}", // detectionValues
        "0x3682=p10",  // initValues
        "{\"c\":\"0x3682=r9\",\"i\":1000,\"s\":5}", // pollInfo
        9,             // pollDataSizeBytes
        "{\"name\":\"SCD40 CO2 Sensor\"}", // devInfoJson
        nullptr        // pollResultDecodeFn
    }
};

// Constants for address indexing
static const BusElemAddrType BASE_DEV_INDEX_BY_ARRAY_MIN_ADDR = 0;
static const BusElemAddrType BASE_DEV_INDEX_BY_ARRAY_MAX_ADDR = 127;

// Dummy device counts by address
static const uint32_t baseDevTypeCountByAddr[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0x42 = 66 decimal (index for TestDevice)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0x62 = 98 decimal (index for SCD40)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// Dummy device type indexes by address - we need to define these for the addresses our test devices use
static uint16_t deviceIndexForAddr42[] = {0};  // TestDevice at 0x42 has index 0
static uint16_t deviceIndexForAddr62[] = {1};  // SCD40 at 0x62 has index 1

// Array of pointers to the indexes
static const uint16_t* baseDevTypeIndexByAddr[128] = {
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, deviceIndexForAddr42, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, deviceIndexForAddr62, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
};

// Dummy scan priority lists
static const uint32_t numScanPriorityLists = 1;
static const uint32_t scanPriorityListLengths[] = {2};
static const BusElemAddrType scanPriorityLists[1][2] = {{0x42, 0x62}}; 