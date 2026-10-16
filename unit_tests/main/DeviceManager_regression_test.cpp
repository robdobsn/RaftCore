/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DeviceManager regression tests
//
// Ported from the feature/device-library branch (Nikos). They cover fixes already on main:
// - devman/listdevs reports identified online devices, including ones with no poll response yet
// - devman/devconfig and devman/cmdraw reject directly connected devices (which are not on a bus)
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "unity.h"
#include "APISourceInfo.h"
#include "DeviceManager.h"
#include "RaftBusSystem.h"
#include "RaftJson.h"
#include "RestAPIEndpointManager.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test bus devices interface
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class DeviceManagerRegressionBusDevices : public RaftBusDevicesIF
{
public:
    void getDeviceAddresses(std::vector<BusElemAddrType>& addresses,
            bool onlyAddressesWithIdentPollResponses) const override
    {
        lastOnlyAddressesWithIdentPollResponses = onlyAddressesWithIdentPollResponses;
        addresses.clear();
        if (!onlyAddressesWithIdentPollResponses)
        {
            // 0x53 is identified, online and deliberately has no poll response.
            // 0x54 is online but unidentified; 0x55 is identified but offline.
            addresses = {0x53, 0x54, 0x55};
        }
    }

    String getDevTypeInfoJsonByAddr(BusElemAddrType, bool, DeviceTypeIndexType&) const override
    {
        return "{}";
    }

    String getDevTypeInfoJsonByTypeName(const String&, bool, DeviceTypeIndexType&) const override
    {
        return "{}";
    }

    String getDevTypeInfoJsonByTypeIdx(DeviceTypeIndexType, bool) const override
    {
        return "{}";
    }

    String getQueuedDeviceDataJson() override
    {
        return "{}";
    }

    std::vector<uint8_t> getQueuedDeviceDataBinary(uint32_t) override
    {
        return {};
    }

    uint32_t getDecodedPollResponses(BusElemAddrType, void*, uint32_t, uint16_t,
            RaftBusDeviceDecodeState&) const override
    {
        return 0;
    }

    mutable bool lastOnlyAddressesWithIdentPollResponses = true;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test bus
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class DeviceManagerRegressionBus : public RaftBus
{
public:
    DeviceManagerRegressionBus(BusElemStatusCB busElemStatusCB,
            BusOperationStatusCB busOperationStatusCB) :
        RaftBus(busElemStatusCB, busOperationStatusCB)
    {
    }

    bool setup(BusNumType busNum, const RaftJsonIF& config) override
    {
        RaftBus::setup(busNum, config);
        return true;
    }

    String getBusName() const override
    {
        return "RegressionBus";
    }

    RaftBusDevicesIF* getBusDevicesIF() override
    {
        return &_devices;
    }

    DeviceTypeIndexType getDeviceTypeIndex(BusElemAddrType address) const override
    {
        if ((address == 0x53) || (address == 0x55))
            return 7;
        return DEVICE_TYPE_INDEX_INVALID;
    }

    bool isElemResponding(BusElemAddrType address, bool* pIsValid = nullptr) const override
    {
        if (pIsValid)
            *pIsValid = true;
        return address != 0x55;
    }

    DeviceManagerRegressionBusDevices& devices()
    {
        return _devices;
    }

private:
    DeviceManagerRegressionBusDevices _devices;
};

static RaftBus* createDeviceManagerRegressionBus(BusElemStatusCB busElemStatusCB,
        BusOperationStatusCB busOperationStatusCB)
{
    return new DeviceManagerRegressionBus(busElemStatusCB, busOperationStatusCB);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DeviceManager test access
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class TestableDeviceManager : public DeviceManager
{
public:
    using DeviceManager::DeviceManager;
    using DeviceManager::addRestAPIEndpoints;
    using DeviceManager::setup;
};

static String request(TestableDeviceManager& deviceManager, const char* requestStr)
{
    RestAPIEndpointManager endpoints;
    deviceManager.addRestAPIEndpoints(endpoints);
    String response;
    APISourceInfo sourceInfo(1);
    endpoints.handleApiRequest(requestStr, response, sourceInfo);
    return response;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tests
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("listdevs retains identified online devices without poll responses", "[DeviceManager]")
{
    raftBusSystem.deinit();
    raftBusSystem.registerBus("DeviceManagerRegressionBus", createDeviceManagerRegressionBus);

    RaftJson config = R"({"DeviceManager":{"Buses":{"buslist":[{"type":"DeviceManagerRegressionBus"}]}}})";
    TestableDeviceManager deviceManager("DeviceManager", config);
    deviceManager.setup();

    String response = request(deviceManager, "devman/listdevs");
    const auto& buses = raftBusSystem.getBusList();
    const uint32_t busCount = buses.size();
    bool requestedOnlyPolledAddresses = true;
    if (!buses.empty())
    {
        auto* pBus = static_cast<DeviceManagerRegressionBus*>(buses.front());
        requestedOnlyPolledAddresses = pBus->devices().lastOnlyAddressesWithIdentPollResponses;
    }

    raftBusSystem.deinit();

    TEST_ASSERT_EQUAL_UINT32(1, busCount);
    RaftJson responseJson(response.c_str());
    TEST_ASSERT_EQUAL_STRING("ok", responseJson.getString("rslt", "").c_str());

    std::vector<String> devices;
    TEST_ASSERT_TRUE(responseJson.getArrayElems("devices", devices));
    TEST_ASSERT_EQUAL_UINT32(1, devices.size());
    RaftJson deviceJson(devices[0].c_str());
    TEST_ASSERT_EQUAL_STRING("1_53", deviceJson.getString("deviceid", "").c_str());
    TEST_ASSERT_EQUAL_INT(7, deviceJson.getLong("dtIdx", -1));
    TEST_ASSERT_FALSE(requestedOnlyPolledAddresses);
}

TEST_CASE("a bus-tagging Devices entry records the expected type", "[DeviceManager]")
{
    raftBusSystem.deinit();
    raftBusSystem.registerBus("DeviceManagerRegressionBus", createDeviceManagerRegressionBus);

    RaftJson config = R"({"DeviceManager":{"Buses":{"buslist":[{"type":"DeviceManagerRegressionBus"}]},)"
                      R"("Devices":[{"name":"FuelGauge","bus":"RegressionBus","addr":"0x53","role":"system","expectType":"MAX17048"},)"
                      R"({"name":"Other","bus":"RegressionBus","addr":"0x54"}]}})";
    TestableDeviceManager deviceManager("DeviceManager", config);
    deviceManager.setup();
    const auto& buses = raftBusSystem.getBusList();
    uint32_t busCount = buses.size();
    BusNumType busNum = buses.empty() ? 0 : buses.front()->getBusNum();
    String expected53 = deviceManager.getDeviceExpectedType(RaftDeviceID(busNum, 0x53));
    String expected54 = deviceManager.getDeviceExpectedType(RaftDeviceID(busNum, 0x54));
    String role53 = deviceManager.getDeviceRole(RaftDeviceID(busNum, 0x53));
    raftBusSystem.deinit();

    TEST_ASSERT_EQUAL_UINT32(1, busCount);
    TEST_ASSERT_EQUAL_STRING("MAX17048", expected53.c_str());
    TEST_ASSERT_EQUAL_STRING("", expected54.c_str());
    TEST_ASSERT_EQUAL_STRING("system", role53.c_str());
}

TEST_CASE("direct devices reject bus-only devconfig and cmdraw", "[DeviceManager]")
{
    raftBusSystem.deinit();
    RaftJson config = R"({"DeviceManager":{}})";
    TestableDeviceManager deviceManager("DeviceManager", config);

    String response = request(deviceManager, "devman/devconfig?deviceid=0_1&intervalUs=5000000");
    {
        RaftJson responseJson(response.c_str());
        TEST_ASSERT_EQUAL_STRING("fail", responseJson.getString("rslt", "").c_str());
        TEST_ASSERT_EQUAL_STRING("failDeviceNotOnBus", responseJson.getString("error", "").c_str());
    }

    response = request(deviceManager, "devman/cmdraw?deviceid=0_1&hexWr=00&numToRd=1");
    {
        RaftJson responseJson(response.c_str());
        TEST_ASSERT_EQUAL_STRING("fail", responseJson.getString("rslt", "").c_str());
        TEST_ASSERT_EQUAL_STRING("failDeviceNotOnBus", responseJson.getString("error", "").c_str());
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Static devices and their device type records
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DemoDevice.h"
#include "DeviceFactory.h"
#include "DeviceTypeRecords.h"

// Set up a DeviceManager with static DemoDevices from a Devices config array and return their type indexes
static void setupStaticDemoDevices(const char* devicesJson, std::vector<uint32_t>& typeIdxs, uint32_t numDevices)
{
    raftBusSystem.deinit();
    deviceFactory.registerDevice("DemoDevice", DemoDevice::create);
    String configStr = String(R"({"DeviceManager":{"Devices":)") + devicesJson + "}}";
    RaftJson config = configStr.c_str();
    TestableDeviceManager deviceManager("DeviceManager", config);
    deviceManager.setup();
    typeIdxs.clear();
    for (uint32_t i = 0; i < numDevices; i++)
    {
        RaftDevice* pDevice = deviceManager.getDevice(RaftDeviceID(RaftDeviceID::BUS_NUM_DIRECT_CONN, i));
        typeIdxs.push_back(pDevice ? pDevice->getDeviceTypeIndex() : UINT32_MAX);
    }
}

TEST_CASE("static devices of the same type share its record", "[DeviceManager]")
{
    std::vector<uint32_t> typeIdxs;
    setupStaticDemoDevices(R"([{"class":"DemoDevice","name":"A","type":"RegressSameType"},)"
                           R"({"class":"DemoDevice","name":"B","type":"RegressSameType"}])", typeIdxs, 2);
    TEST_ASSERT_EQUAL_UINT32(2, typeIdxs.size());
    TEST_ASSERT_NOT_EQUAL(DEVICE_TYPE_INDEX_INVALID, typeIdxs[0]);
    TEST_ASSERT_EQUAL_UINT32(typeIdxs[0], typeIdxs[1]);
    DeviceTypeIndexType idx = 0;
    TEST_ASSERT_NOT_NULL(deviceTypeRecords.getRecord("RegressSameType", idx));
    TEST_ASSERT_EQUAL_UINT32(typeIdxs[0], idx);
}

TEST_CASE("a static device whose type conflicts with an existing one gets no type", "[DeviceManager]")
{
    // The second device has the same type key but a different data size: its record differs, so it is
    // rejected (D17) and the device has no type - never index 0 or the first device's record (C7, C8)
    std::vector<uint32_t> typeIdxs;
    setupStaticDemoDevices(R"([{"class":"DemoDevice","name":"A","type":"RegressConflictType"},)"
                           R"({"class":"DemoDevice","name":"B","type":"RegressConflictType","sampleBytes":64}])", typeIdxs, 2);
    TEST_ASSERT_EQUAL_UINT32(2, typeIdxs.size());
    TEST_ASSERT_NOT_EQUAL(DEVICE_TYPE_INDEX_INVALID, typeIdxs[0]);
    TEST_ASSERT_EQUAL_UINT32(DEVICE_TYPE_INDEX_INVALID, typeIdxs[1]);
    const DeviceTypeRecord* pRec = deviceTypeRecords.getRecord(typeIdxs[0]);
    TEST_ASSERT_NOT_NULL(pRec);
    TEST_ASSERT_EQUAL_UINT16(14, pRec->pollDataSizeBytes);
}

#ifdef DEVICE_MANAGER_ENABLE_DEMO_DEVICE
TEST_CASE("devman/demo starts one demo device per type", "[DeviceManager]")
{
    raftBusSystem.deinit();
    RaftJson config = R"({"DeviceManager":{}})";
    TestableDeviceManager deviceManager("DeviceManager", config);
    deviceManager.setup();

    // Started: a static device named after its type, with that type's record
    RaftJson resp1 = request(deviceManager, "devman/demo?type=RegressDemo&rate=50").c_str();
    TEST_ASSERT_EQUAL_STRING("ok", resp1.getString("rslt", "").c_str());
    TEST_ASSERT_TRUE(resp1.getBool("demoStarted", false));
    RaftDevice* pDevice = deviceManager.getDevice("RegressDemo", true);
    TEST_ASSERT_NOT_NULL(pDevice);
    DeviceTypeIndexType idx = 0;
    TEST_ASSERT_NOT_NULL(deviceTypeRecords.getRecord("RegressDemo", idx));
    TEST_ASSERT_EQUAL_UINT32(idx, pDevice->getDeviceTypeIndex());

    // A second demo of the same type is refused
    RaftJson resp2 = request(deviceManager, "devman/demo?type=RegressDemo").c_str();
    TEST_ASSERT_EQUAL_STRING("failDemoDeviceExists", resp2.getString("error", "").c_str());

    // A type containing a quote gives valid JSON
    RaftJson resp3 = request(deviceManager, "devman/demo?type=Quote%22Demo").c_str();
    TEST_ASSERT_EQUAL_STRING("ok", resp3.getString("rslt", "").c_str());
    TEST_ASSERT_EQUAL_STRING("Quote\"Demo", resp3.getString("type", "").c_str());
}
#endif
