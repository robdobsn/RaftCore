/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DeviceManager regression tests
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "unity.h"
#include "APISourceInfo.h"
#include "BusRequestInfo.h"
#include "DeviceManager.h"
#include "HWElemReq.h"
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

TEST_CASE("listdevs retains identified online devices without poll responses", "[DeviceManager][R12]")
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
        requestedOnlyPolledAddresses =
                pBus->devices().lastOnlyAddressesWithIdentPollResponses;
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

TEST_CASE("direct devices reject bus-only devconfig and cmdraw", "[DeviceManager][R13]")
{
    raftBusSystem.deinit();
    RaftJson config = R"({"DeviceManager":{}})";
    TestableDeviceManager deviceManager("DeviceManager", config);

    String response = request(deviceManager,
            "devman/devconfig?deviceid=0_1&intervalUs=5000000");
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

TEST_CASE("callback task affinity does not leak across reused bus requests", "[BusRequestInfo]")
{
    BusRequestInfo request;
    request.setCallbackFromBusTask(true);
    TEST_ASSERT_TRUE(request.isCallbackFromBusTask());

    HWElemReq hwElemReq({}, 0, HWElemReq::UNNUM, nullptr, 0);
    request.set(BUS_REQ_TYPE_STD, hwElemReq);
    TEST_ASSERT_FALSE(request.isCallbackFromBusTask());

    request.setCallbackFromBusTask(true);
    request.clear();
    TEST_ASSERT_FALSE(request.isCallbackFromBusTask());
}
