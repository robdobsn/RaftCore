/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device names and roles tests
//
// Tests DeviceNames, the class DeviceManager uses for device names and device roles (setname/setrole
// APIs, the DevMan DeviceNames config and name lookups).
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "testhdr.h"
#include "ArduinoWString.h"
#include "RaftJson.h"
#include "DeviceNames.h"

static void testPackDeviceIDKey()
{
    printf("Running testPackDeviceIDKey...\n");
    RaftDeviceID id1(1, 0xf8);
    uint64_t key1 = DeviceNames::packDeviceIDKey(id1);
    TEST_ASSERT(key1 == ((uint64_t(1) << 32) | 0xf8), "pack bus=1 addr=0xf8");
    RaftDeviceID id2(2, 0x00);
    uint64_t key2 = DeviceNames::packDeviceIDKey(id2);
    TEST_ASSERT(key2 == (uint64_t(2) << 32), "pack bus=2 addr=0x00");
    TEST_ASSERT(key1 != key2, "different devices have different keys");
    TEST_ASSERT(DeviceNames::packDeviceIDKey(RaftDeviceID(1, 0xf8)) == key1, "same device has the same key");
}

static void testSetAndGetName()
{
    printf("Running testSetAndGetName...\n");
    DeviceNames names;
    RaftDeviceID id(1, 0xf8);
    names.setName(id, "accelerometer");
    TEST_ASSERT(names.getNameForID(id) == "accelerometer", "getNameForID returns the name");
    TEST_ASSERT(names.lookupName(id) == "accelerometer", "lookupName returns the name");
    RaftDeviceID resolved;
    TEST_ASSERT(names.resolveName("accelerometer", resolved) && (resolved == id), "resolveName finds the device");
}

static void testUnnamedDevice()
{
    printf("Running testUnnamedDevice...\n");
    DeviceNames names;
    RaftDeviceID id(1, 0xab);
    TEST_ASSERT(names.getNameForID(id) == id.toString(), "unnamed device falls back to its device ID");
    TEST_ASSERT(names.lookupName(id) == "", "lookupName has no fallback");
    RaftDeviceID resolved;
    TEST_ASSERT(!names.resolveName("nonexistent", resolved), "unknown name does not resolve");
}

static void testRenameDevice()
{
    printf("Running testRenameDevice...\n");
    DeviceNames names;
    RaftDeviceID id(1, 0xf8);
    names.setName(id, "oldname");
    names.setName(id, "newname");
    TEST_ASSERT(names.getNameForID(id) == "newname", "renamed device has the new name");
    RaftDeviceID resolved;
    TEST_ASSERT(!names.resolveName("oldname", resolved), "old name no longer resolves");
    TEST_ASSERT(names.resolveName("newname", resolved) && (resolved == id), "new name resolves");
    TEST_ASSERT(names.nameCount() == 1, "rename leaves one entry");
}

// Names are one-to-one: giving a name to a second device takes it from the first
static void testNameMovesToNewDevice()
{
    printf("Running testNameMovesToNewDevice...\n");
    DeviceNames names;
    RaftDeviceID id1(1, 0xf8);
    RaftDeviceID id2(1, 0x50);
    names.setName(id1, "sensor");
    names.setName(id2, "sensor");
    RaftDeviceID resolved;
    TEST_ASSERT(names.resolveName("sensor", resolved) && (resolved == id2), "name resolves to the new device");
    TEST_ASSERT(names.getNameForID(id1) == id1.toString(), "first device no longer has the name");
    TEST_ASSERT(names.nameCount() == 1, "one entry after the name moved");
}

static void testLoadFromConfig()
{
    printf("Running testLoadFromConfig...\n");
    DeviceNames names;
    RaftJson config(R"({"DeviceNames":{"1_f8":"accelerometer","1_50":"barometer","2_10":"gyro","1_20":"","notanid":"x"}})");
    names.loadFromConfig(config);
    TEST_ASSERT(names.nameCount() == 3, "three entries loaded (empty name and invalid device ID skipped)");
    RaftDeviceID resolved;
    TEST_ASSERT(names.resolveName("accelerometer", resolved) && (resolved == RaftDeviceID(1, 0xf8)), "accelerometer");
    TEST_ASSERT(names.resolveName("barometer", resolved) && (resolved == RaftDeviceID(1, 0x50)), "barometer");
    TEST_ASSERT(names.resolveName("gyro", resolved) && (resolved == RaftDeviceID(2, 0x10)), "gyro");

    // No DeviceNames section is not an error
    DeviceNames empty;
    empty.loadFromConfig(RaftJson("{}"));
    TEST_ASSERT(empty.nameCount() == 0, "no DeviceNames section loads nothing");
}

static void testMultipleDevices()
{
    printf("Running testMultipleDevices...\n");
    DeviceNames names;
    RaftDeviceID id1(1, 0x10);
    RaftDeviceID id2(1, 0x20);
    RaftDeviceID id3(2, 0x10);  // same address, different bus
    names.setName(id1, "dev_a");
    names.setName(id2, "dev_b");
    names.setName(id3, "dev_c");
    TEST_ASSERT(names.getNameForID(id1) == "dev_a", "dev_a lookup");
    TEST_ASSERT(names.getNameForID(id2) == "dev_b", "dev_b lookup");
    TEST_ASSERT(names.getNameForID(id3) == "dev_c", "dev_c lookup (same address, other bus)");
    RaftDeviceID resolved;
    TEST_ASSERT(names.resolveName("dev_a", resolved) && (resolved == id1), "dev_a resolves");
    TEST_ASSERT(names.resolveName("dev_b", resolved) && (resolved == id2), "dev_b resolves");
    TEST_ASSERT(names.resolveName("dev_c", resolved) && (resolved == id3), "dev_c resolves");
}

static void testRoles()
{
    printf("Running testRoles...\n");
    DeviceNames names;
    RaftDeviceID id(1, 0xf8);
    TEST_ASSERT(names.getRole(id) == "normal", "unset role is normal");
    TEST_ASSERT(!names.isSystem(id), "unset role is not system");
    names.setRole(id, "system");
    TEST_ASSERT(names.getRole(id) == "system", "role set to system");
    TEST_ASSERT(names.isSystem(id), "isSystem true");
    names.setRole(id, "diagnostic");
    TEST_ASSERT(names.getRole(id) == "diagnostic", "role overwritten");
    TEST_ASSERT(!names.isSystem(id), "diagnostic is not system");

    // "normal" (any case) or empty removes the role
    names.setRole(id, "system");
    TEST_ASSERT(names.roleCount() == 1, "one role entry");
    names.setRole(id, "Normal");
    TEST_ASSERT(names.roleCount() == 0, "normal removes the entry");
    TEST_ASSERT(names.getRole(id) == "normal", "role reads normal after removal");
    names.setRole(id, "system");
    names.setRole(id, "");
    TEST_ASSERT(names.roleCount() == 0, "empty role removes the entry");

    // isSystem is case-sensitive on the stored value
    RaftDeviceID id2(3, 0x40);
    names.setRole(id2, "System");
    TEST_ASSERT(!names.isSystem(id2), "stored 'System' is not 'system'");

    // Roles and names are independent
    names.setName(id2, "named");
    TEST_ASSERT(names.getRole(id2) == "System", "naming a device keeps its role");
}

// Device ID strings, as used in REST requests (deviceid=), config (DeviceNames) and published data
static void testDeviceIDFromString()
{
    printf("Running testDeviceIDFromString...\n");
    struct ValidCase { const char* str; BusNumType busNum; BusElemAddrType address; };
    const ValidCase validCases[] = {
        {"1_68", 1, 0x68},
        {"1_0x68", 1, 0x68},
        {"1_6A", 1, 0x6a},
        {"1_325", 1, 0x325},            // slot 3, I2C address 0x25
        {"12_ff", 12, 0xff},
        {"0_1", 0, 1},                  // directly connected device
        {"68", 0, 0x68},                // address only: bus 0
        {"0x68", 0, 0x68},
        {"bad", 0, 0xbad},              // a hex address, not a name
        {"1_ffffffff", 1, 0xffffffff},
    };
    for (const auto& tc : validCases)
    {
        RaftDeviceID id = RaftDeviceID::fromString(tc.str);
        std::string name = std::string("valid \"") + tc.str + "\"";
        TEST_ASSERT(id.isValid() && (id.getBusNum() == tc.busNum) && (id.getAddress() == tc.address), name.c_str());
    }
    TEST_ASSERT(RaftDeviceID::fromString("ANY").isAnyDevice(), "ANY");
    TEST_ASSERT(RaftDeviceID::fromString("any").isAnyDevice(), "any (case)");

    // Malformed strings give an invalid ID rather than a guess
    const char* invalidCases[] = {
        "", "_68", "1_", "1_0x", "a_68", "-1_68", "1_68x", "1_6 8", "imu", "notanid", "1_123456789",
        "4294967294_1", "4294967295_1", "99999999999_1", "1_2_3",
    };
    for (const char* str : invalidCases)
    {
        std::string name = std::string("invalid \"") + str + "\"";
        TEST_ASSERT(!RaftDeviceID::fromString(str).isValid(), name.c_str());
    }

    // toString and fromString round trip
    const RaftDeviceID roundTrip[] = { RaftDeviceID(1, 0x68), RaftDeviceID(0, 3), RaftDeviceID(7, 0x325),
            RaftDeviceID(RaftDeviceID::ALL_DEVICES_ANY_BUS, 0) };
    for (const auto& id : roundTrip)
        TEST_ASSERT(RaftDeviceID::fromString(id.toString()) == id, ("round trip " + id.toString()).c_str());
}

static void testExpectedType()
{
    printf("Running testExpectedType...\n");
    DeviceNames names;
    RaftDeviceID id(1, 0x36);
    TEST_ASSERT(names.getExpectedType(id) == "", "no expected type by default");
    names.setExpectedType(id, "MAX17048");
    TEST_ASSERT(names.getExpectedType(id) == "MAX17048", "expected type is returned");
    TEST_ASSERT(names.getExpectedType(RaftDeviceID(2, 0x36)) == "", "other bus has no expected type");
    TEST_ASSERT(names.getExpectedType(RaftDeviceID(1, 0x37)) == "", "other address has no expected type");
    names.setExpectedType(id, "LC709203F");
    TEST_ASSERT(names.getExpectedType(id) == "LC709203F", "expected type can be changed");
    names.setExpectedType(id, "");
    TEST_ASSERT(names.getExpectedType(id) == "", "empty removes the expected type");
    TEST_ASSERT(names.nameCount() == 0, "expected type does not name the device");
}

int main()
{
    testDeviceIDFromString();
    testPackDeviceIDKey();
    testSetAndGetName();
    testUnnamedDevice();
    testRenameDevice();
    testNameMovesToNewDevice();
    testLoadFromConfig();
    testMultipleDevices();
    testRoles();
    testExpectedType();
    return testSummary("test_device_names");
}
