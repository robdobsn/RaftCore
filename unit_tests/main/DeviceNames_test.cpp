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

// Standalone name-map logic mirroring DeviceManager private members
// (avoids full DeviceManager instantiation which requires buses, config, etc.)
static std::unordered_map<std::string, RaftDeviceID> s_deviceNameToID;
static std::unordered_map<uint64_t, std::string> s_deviceIDToName;

static uint64_t packDeviceIDKey(RaftDeviceID deviceID)
{
    return (static_cast<uint64_t>(deviceID.getBusNum()) << 32) | deviceID.getAddress();
}

static void setDeviceName(RaftDeviceID deviceID, const String& name)
{
    std::string nameStr(name.c_str());
    uint64_t key = packDeviceIDKey(deviceID);
    auto itOld = s_deviceIDToName.find(key);
    if (itOld != s_deviceIDToName.end())
    {
        s_deviceNameToID.erase(itOld->second);
        s_deviceIDToName.erase(itOld);
    }
    auto itOldName = s_deviceNameToID.find(nameStr);
    if (itOldName != s_deviceNameToID.end())
    {
        s_deviceIDToName.erase(packDeviceIDKey(itOldName->second));
        s_deviceNameToID.erase(itOldName);
    }
    s_deviceNameToID[nameStr] = deviceID;
    s_deviceIDToName[key] = nameStr;
}

static String getDeviceNameForID(RaftDeviceID deviceID)
{
    uint64_t key = packDeviceIDKey(deviceID);
    auto it = s_deviceIDToName.find(key);
    if (it != s_deviceIDToName.end())
        return String(it->second.c_str());
    return deviceID.toString();
}

static bool resolveDeviceNameToID(const String& name, RaftDeviceID& deviceID)
{
    auto it = s_deviceNameToID.find(std::string(name.c_str()));
    if (it != s_deviceNameToID.end())
    {
        deviceID = it->second;
        return true;
    }
    return false;
}

static void clearMaps()
{
    s_deviceNameToID.clear();
    s_deviceIDToName.clear();
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
    TEST_ASSERT_EQUAL_UINT64((uint64_t(1) << 32) | 0xf8, key1);
    TEST_ASSERT_EQUAL_UINT64(uint64_t(2) << 32, key2);
    TEST_ASSERT_NOT_EQUAL(key1, key2);

    // Same device should produce same key
    RaftDeviceID id1copy(1, 0xf8);
    TEST_ASSERT_EQUAL_UINT64(key1, packDeviceIDKey(id1copy));
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

    std::vector<String> keys;
    TEST_ASSERT_TRUE(json.getKeys("DeviceNames", keys));
    TEST_ASSERT_EQUAL(3, keys.size());

    for (const auto& key : keys)
    {
        String path = "DeviceNames/" + key;
        String name = json.getString(path.c_str(), "");
        RaftDeviceID deviceID = RaftDeviceID::fromString(key);
        if (deviceID.isValid() && name.length() > 0)
            setDeviceName(deviceID, name);
    }

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
