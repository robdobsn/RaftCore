/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Unit test of DeviceManager device name mapping
//
// Rob Dobson 2025
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <string>
#include <unordered_map>
#include "unity.h"
#include "RaftArduino.h"
#include "RaftDeviceConsts.h"
#include "RaftJson.h"
#include "Logger.h"
#include "DeviceNames.h"

// The DeviceNames class used by DeviceManager (tested directly, so no DeviceManager instance - with its buses
// and config - is needed). The helpers keep the test cases' names for the DeviceManager methods they cover
static DeviceNames s_deviceNames;

static uint64_t packDeviceIDKey(RaftDeviceID deviceID)
{
    return DeviceNames::packDeviceIDKey(deviceID);
}

static void setDeviceName(RaftDeviceID deviceID, const String& name)
{
    s_deviceNames.setName(deviceID, name);
}

static String getDeviceNameForID(RaftDeviceID deviceID)
{
    return s_deviceNames.getNameForID(deviceID);
}

static bool resolveDeviceNameToID(const String& name, RaftDeviceID& deviceID)
{
    return s_deviceNames.resolveName(name, deviceID);
}

static void clearMaps()
{
    s_deviceNames = DeviceNames();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tests
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("packDeviceIDKey produces unique keys", "[DeviceNames]")
{
    RaftDeviceID id1(1, 0xf8);
    RaftDeviceID id2(2, 0x00);
    uint64_t key1 = packDeviceIDKey(id1);
    uint64_t key2 = packDeviceIDKey(id2);
    uint64_t expected1 = (uint64_t(1) << 32) | 0xf8;
    uint64_t expected2 = uint64_t(2) << 32;
    TEST_ASSERT_TRUE(expected1 == key1);
    TEST_ASSERT_TRUE(expected2 == key2);
    TEST_ASSERT_NOT_EQUAL(key1, key2);

    // Same device should produce same key
    RaftDeviceID id1copy(1, 0xf8);
    TEST_ASSERT_TRUE(key1 == packDeviceIDKey(id1copy));
}

TEST_CASE("setDeviceName and getDeviceNameForID roundtrip", "[DeviceNames]")
{
    clearMaps();
    RaftDeviceID id(1, 0xf8);
    setDeviceName(id, "accelerometer");
    TEST_ASSERT_EQUAL_STRING("accelerometer", getDeviceNameForID(id).c_str());
}

TEST_CASE("resolveDeviceNameToID finds named device", "[DeviceNames]")
{
    clearMaps();
    RaftDeviceID id(1, 0xf8);
    setDeviceName(id, "accelerometer");

    RaftDeviceID resolved;
    TEST_ASSERT_TRUE(resolveDeviceNameToID("accelerometer", resolved));
    TEST_ASSERT_TRUE(resolved == id);
}

TEST_CASE("rename removes old name mapping", "[DeviceNames]")
{
    clearMaps();
    RaftDeviceID id(1, 0xf8);
    setDeviceName(id, "oldname");
    setDeviceName(id, "newname");

    TEST_ASSERT_EQUAL_STRING("newname", getDeviceNameForID(id).c_str());

    RaftDeviceID resolved;
    TEST_ASSERT_FALSE(resolveDeviceNameToID("oldname", resolved));
    TEST_ASSERT_TRUE(resolveDeviceNameToID("newname", resolved));
    TEST_ASSERT_TRUE(resolved == id);
}

TEST_CASE("name reassignment to different device clears old device", "[DeviceNames]")
{
    clearMaps();
    RaftDeviceID id1(1, 0xf8);
    RaftDeviceID id2(1, 0x50);
    setDeviceName(id1, "sensor");
    setDeviceName(id2, "sensor");

    RaftDeviceID resolved;
    TEST_ASSERT_TRUE(resolveDeviceNameToID("sensor", resolved));
    TEST_ASSERT_TRUE(resolved == id2);
    TEST_ASSERT_EQUAL_STRING(id1.toString().c_str(), getDeviceNameForID(id1).c_str());
}

TEST_CASE("unmapped device returns toString", "[DeviceNames]")
{
    clearMaps();
    RaftDeviceID id(1, 0xab);
    TEST_ASSERT_EQUAL_STRING(id.toString().c_str(), getDeviceNameForID(id).c_str());
}

TEST_CASE("resolveDeviceNameToID returns false for unknown name", "[DeviceNames]")
{
    clearMaps();
    RaftDeviceID resolved;
    TEST_ASSERT_FALSE(resolveDeviceNameToID("nonexistent", resolved));
}

TEST_CASE("multiple devices with distinct names", "[DeviceNames]")
{
    clearMaps();
    RaftDeviceID id1(1, 0x10);
    RaftDeviceID id2(1, 0x20);
    RaftDeviceID id3(2, 0x10);

    setDeviceName(id1, "dev_a");
    setDeviceName(id2, "dev_b");
    setDeviceName(id3, "dev_c");

    TEST_ASSERT_EQUAL_STRING("dev_a", getDeviceNameForID(id1).c_str());
    TEST_ASSERT_EQUAL_STRING("dev_b", getDeviceNameForID(id2).c_str());
    TEST_ASSERT_EQUAL_STRING("dev_c", getDeviceNameForID(id3).c_str());

    RaftDeviceID resolved;
    resolveDeviceNameToID("dev_a", resolved);
    TEST_ASSERT_TRUE(resolved == id1);
    resolveDeviceNameToID("dev_b", resolved);
    TEST_ASSERT_TRUE(resolved == id2);
    resolveDeviceNameToID("dev_c", resolved);
    TEST_ASSERT_TRUE(resolved == id3);
}

TEST_CASE("loadDeviceNames from JSON config", "[DeviceNames]")
{
    clearMaps();
    const char* config = R"({"DeviceNames":{"1_f8":"accelerometer","1_50":"barometer","2_10":"gyro"}})";
    RaftJson json(config);

    s_deviceNames.loadFromConfig(json);
    TEST_ASSERT_EQUAL(3, s_deviceNames.nameCount());

    RaftDeviceID resolved;
    TEST_ASSERT_TRUE(resolveDeviceNameToID("accelerometer", resolved));
    TEST_ASSERT_EQUAL(1, resolved.getBusNum());
    TEST_ASSERT_EQUAL(0xf8, resolved.getAddress());

    TEST_ASSERT_TRUE(resolveDeviceNameToID("barometer", resolved));
    TEST_ASSERT_EQUAL(0x50, resolved.getAddress());

    TEST_ASSERT_TRUE(resolveDeviceNameToID("gyro", resolved));
    TEST_ASSERT_EQUAL(2, resolved.getBusNum());
    TEST_ASSERT_EQUAL(0x10, resolved.getAddress());
}
