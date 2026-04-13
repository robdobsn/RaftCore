#pragma once

#include <stdio.h>
#include <string>
#include <unordered_map>
#include "RaftDeviceConsts.h"

/// @brief Test harness for DeviceManager device-name-map operations
/// Tests loadDeviceNames, setDeviceName, getDeviceNameForID, resolveDeviceNameToID, packDeviceIDKey
/// without requiring full DeviceManager instantiation
class DeviceNamesTest
{
public:
    // Mirrors the maps from DeviceManager
    std::unordered_map<std::string, RaftDeviceID> _deviceNameToID;
    std::unordered_map<uint64_t, std::string> _deviceIDToName;

    static uint64_t packDeviceIDKey(RaftDeviceID deviceID)
    {
        return (static_cast<uint64_t>(deviceID.getBusNum()) << 32) | deviceID.getAddress();
    }

    void setDeviceName(RaftDeviceID deviceID, const String& name)
    {
        std::string nameStr(name.c_str());
        uint64_t key = packDeviceIDKey(deviceID);

        auto itOld = _deviceIDToName.find(key);
        if (itOld != _deviceIDToName.end())
        {
            _deviceNameToID.erase(itOld->second);
            _deviceIDToName.erase(itOld);
        }

        auto itOldName = _deviceNameToID.find(nameStr);
        if (itOldName != _deviceNameToID.end())
        {
            _deviceIDToName.erase(packDeviceIDKey(itOldName->second));
            _deviceNameToID.erase(itOldName);
        }

        _deviceNameToID[nameStr] = deviceID;
        _deviceIDToName[key] = nameStr;
    }

    String getDeviceNameForID(RaftDeviceID deviceID) const
    {
        uint64_t key = packDeviceIDKey(deviceID);
        auto it = _deviceIDToName.find(key);
        if (it != _deviceIDToName.end())
            return String(it->second.c_str());
        return deviceID.toString();
    }

    bool resolveDeviceNameToID(const String& name, RaftDeviceID& deviceID) const
    {
        auto it = _deviceNameToID.find(std::string(name.c_str()));
        if (it != _deviceNameToID.end())
        {
            deviceID = it->second;
            return true;
        }
        return false;
    }

    void loadDeviceNamesFromJson(const char* jsonStr)
    {
        // Parse using RaftJson
        RaftJson config(jsonStr);
        std::vector<String> keys;
        if (!config.getKeys("DeviceNames", keys) || keys.empty())
            return;

        for (const auto& key : keys)
        {
            String path = "DeviceNames/" + key;
            String friendlyName = config.getString(path.c_str(), "");
            if (friendlyName.length() == 0)
                continue;

            RaftDeviceID deviceID = RaftDeviceID::fromString(key);
            if (!deviceID.isValid())
                continue;

            setDeviceName(deviceID, friendlyName);
        }
    }

    void runTests()
    {
        printf("Running DeviceNamesTest...\n");
        int failCount = 0;

        testPackDeviceIDKey(failCount);
        testSetAndGetDeviceName(failCount);
        testResolveDeviceNameToID(failCount);
        testRenameDevice(failCount);
        testRenameConflict(failCount);
        testLoadDeviceNamesFromConfig(failCount);
        testUnmappedDeviceReturnsIDString(failCount);
        testResolveNonExistentName(failCount);
        testMultipleDevices(failCount);

        if (failCount == 0)
            printf("DeviceNamesTest all tests passed\n");
        else
            printf("DeviceNamesTest FAILED %d tests\n", failCount);
    }

private:

#define DN_ASSERT(cond, msg) if (!(cond)) { printf("  FAIL: %s\n", msg); failCount++; }

    void testPackDeviceIDKey(int& failCount)
    {
        printf("  testPackDeviceIDKey\n");
        RaftDeviceID id1(1, 0xf8);
        uint64_t key1 = packDeviceIDKey(id1);
        DN_ASSERT(key1 == ((uint64_t(1) << 32) | 0xf8), "pack bus=1 addr=0xf8");

        RaftDeviceID id2(2, 0x00);
        uint64_t key2 = packDeviceIDKey(id2);
        DN_ASSERT(key2 == (uint64_t(2) << 32), "pack bus=2 addr=0x00");

        // Different bus+addr should produce different keys
        DN_ASSERT(key1 != key2, "different devices should have different keys");

        // Same bus+addr should produce same key
        RaftDeviceID id1copy(1, 0xf8);
        DN_ASSERT(packDeviceIDKey(id1copy) == key1, "same device should have same key");
    }

    void testSetAndGetDeviceName(int& failCount)
    {
        printf("  testSetAndGetDeviceName\n");
        _deviceNameToID.clear();
        _deviceIDToName.clear();

        RaftDeviceID id(1, 0xf8);
        setDeviceName(id, "accelerometer");

        String name = getDeviceNameForID(id);
        DN_ASSERT(name == "accelerometer", "getDeviceNameForID should return 'accelerometer'");
    }

    void testResolveDeviceNameToID(int& failCount)
    {
        printf("  testResolveDeviceNameToID\n");
        _deviceNameToID.clear();
        _deviceIDToName.clear();

        RaftDeviceID id(1, 0xf8);
        setDeviceName(id, "accelerometer");

        RaftDeviceID resolved;
        bool found = resolveDeviceNameToID("accelerometer", resolved);
        DN_ASSERT(found, "resolveDeviceNameToID should find 'accelerometer'");
        DN_ASSERT(resolved == id, "resolved ID should match original");
    }

    void testRenameDevice(int& failCount)
    {
        printf("  testRenameDevice\n");
        _deviceNameToID.clear();
        _deviceIDToName.clear();

        RaftDeviceID id(1, 0xf8);
        setDeviceName(id, "oldname");
        setDeviceName(id, "newname");

        // New name should work
        String name = getDeviceNameForID(id);
        DN_ASSERT(name == "newname", "after rename, getDeviceNameForID should return 'newname'");

        // Old name should no longer resolve
        RaftDeviceID resolved;
        bool found = resolveDeviceNameToID("oldname", resolved);
        DN_ASSERT(!found, "old name should no longer resolve");

        // New name should resolve
        found = resolveDeviceNameToID("newname", resolved);
        DN_ASSERT(found, "new name should resolve");
        DN_ASSERT(resolved == id, "resolved ID should match");
    }

    void testRenameConflict(int& failCount)
    {
        printf("  testRenameConflict\n");
        _deviceNameToID.clear();
        _deviceIDToName.clear();

        RaftDeviceID id1(1, 0xf8);
        RaftDeviceID id2(1, 0x50);
        setDeviceName(id1, "sensor");
        setDeviceName(id2, "sensor");  // reassign name to different device

        // Name should now point to id2
        RaftDeviceID resolved;
        bool found = resolveDeviceNameToID("sensor", resolved);
        DN_ASSERT(found, "name should resolve after conflict");
        DN_ASSERT(resolved == id2, "name should resolve to id2 after reassign");

        // id1 should no longer have a name
        String name1 = getDeviceNameForID(id1);
        DN_ASSERT(name1 == id1.toString(), "id1 should fall back to toString() after name taken");
    }

    void testLoadDeviceNamesFromConfig(int& failCount)
    {
        printf("  testLoadDeviceNamesFromConfig\n");
        _deviceNameToID.clear();
        _deviceIDToName.clear();

        const char* config = R"({
            "DeviceNames": {
                "1_f8": "accelerometer",
                "1_50": "barometer",
                "2_10": "gyro"
            }
        })";

        loadDeviceNamesFromJson(config);

        DN_ASSERT(_deviceNameToID.size() == 3, "should have 3 name entries");
        DN_ASSERT(_deviceIDToName.size() == 3, "should have 3 id entries");

        RaftDeviceID resolved;
        bool found = resolveDeviceNameToID("accelerometer", resolved);
        DN_ASSERT(found, "accelerometer should resolve");
        DN_ASSERT(resolved.getBusNum() == 1, "accelerometer busNum should be 1");
        DN_ASSERT(resolved.getAddress() == 0xf8, "accelerometer addr should be 0xf8");

        found = resolveDeviceNameToID("barometer", resolved);
        DN_ASSERT(found, "barometer should resolve");
        DN_ASSERT(resolved.getAddress() == 0x50, "barometer addr should be 0x50");

        found = resolveDeviceNameToID("gyro", resolved);
        DN_ASSERT(found, "gyro should resolve");
        DN_ASSERT(resolved.getBusNum() == 2, "gyro busNum should be 2");
        DN_ASSERT(resolved.getAddress() == 0x10, "gyro addr should be 0x10");
    }

    void testUnmappedDeviceReturnsIDString(int& failCount)
    {
        printf("  testUnmappedDeviceReturnsIDString\n");
        _deviceNameToID.clear();
        _deviceIDToName.clear();

        RaftDeviceID id(1, 0xab);
        String name = getDeviceNameForID(id);
        DN_ASSERT(name == id.toString(), "unmapped device should return toString()");
    }

    void testResolveNonExistentName(int& failCount)
    {
        printf("  testResolveNonExistentName\n");
        _deviceNameToID.clear();
        _deviceIDToName.clear();

        RaftDeviceID resolved;
        bool found = resolveDeviceNameToID("nonexistent", resolved);
        DN_ASSERT(!found, "non-existent name should not resolve");
    }

    void testMultipleDevices(int& failCount)
    {
        printf("  testMultipleDevices\n");
        _deviceNameToID.clear();
        _deviceIDToName.clear();

        RaftDeviceID id1(1, 0x10);
        RaftDeviceID id2(1, 0x20);
        RaftDeviceID id3(2, 0x10);  // same addr, different bus

        setDeviceName(id1, "dev_a");
        setDeviceName(id2, "dev_b");
        setDeviceName(id3, "dev_c");

        DN_ASSERT(getDeviceNameForID(id1) == "dev_a", "dev_a lookup");
        DN_ASSERT(getDeviceNameForID(id2) == "dev_b", "dev_b lookup");
        DN_ASSERT(getDeviceNameForID(id3) == "dev_c", "dev_c lookup");

        // All should resolve back
        RaftDeviceID resolved;
        resolveDeviceNameToID("dev_a", resolved);
        DN_ASSERT(resolved == id1, "dev_a should resolve to id1");
        resolveDeviceNameToID("dev_b", resolved);
        DN_ASSERT(resolved == id2, "dev_b should resolve to id2");
        resolveDeviceNameToID("dev_c", resolved);
        DN_ASSERT(resolved == id3, "dev_c should resolve to id3");
    }

#undef DN_ASSERT
};
