/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device settings store tests (Stage 10)
//
// Saved names, roles, devconfig settings (with their type) and polling, keyed by device ID: written as JSON and
// loaded back unchanged; an empty entry isn't written; a document that isn't a settings document is refused.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "testhdr.h"
#include "ArduinoWString.h"
#include "DeviceSettingsStore.h"

static void testRoundTrip()
{
    printf("Running testRoundTrip...\n");
    DeviceSettingsStore store;
    DeviceSettingsStore::Settings& imu = store.getOrAdd("1_68");
    imu.name = "imu \"main\"";
    imu.typeKey = "MPU6050";
    imu.devConfig.push_back(RaftJson::NameValuePair("intervalUs", "20000"));
    imu.devConfig.push_back(RaftJson::NameValuePair("numSamples", "4"));
    imu.pollEnabled = 0;
    store.getOrAdd("1_229").role = "system";
    store.getOrAdd("1_30");     // empty - not written
    String json = store.toJson();
    TEST_ASSERT(json.indexOf("1_30") < 0, "empty entry not written");

    DeviceSettingsStore loaded;
    TEST_ASSERT(loaded.loadJson(json.c_str()), "loads");
    const DeviceSettingsStore::Settings* pImu = loaded.find("1_68");
    TEST_ASSERT(pImu && (pImu->name == "imu \"main\"") && (pImu->typeKey == "MPU6050") && (pImu->pollEnabled == 0),
                "name, type and polling");
    TEST_ASSERT(pImu && (pImu->devConfig.size() == 2) && (pImu->devConfig[0].name == "intervalUs") &&
                (pImu->devConfig[0].value == "20000") && (pImu->devConfig[1].value == "4"), "devconfig settings");
    const DeviceSettingsStore::Settings* pSys = loaded.find("1_229");
    TEST_ASSERT(pSys && (pSys->role == "system") && (pSys->pollEnabled < 0) && pSys->devConfig.empty(), "role only");
    TEST_ASSERT(loaded.find("1_30") == nullptr, "no empty entry");
    TEST_ASSERT(loaded.toJson() == json, "written the same again");
}

static void testLoadEdges()
{
    printf("Running testLoadEdges...\n");
    DeviceSettingsStore store;
    TEST_ASSERT(store.loadJson("") && store.all().empty(), "empty file is no settings");
    TEST_ASSERT(store.loadJson(nullptr) && store.all().empty(), "no file is no settings");
    TEST_ASSERT(!store.loadJson("{\"other\":1}"), "not a settings document");
    TEST_ASSERT(store.loadJson("{\"devices\":{\"1_68\":{\"poll\":true}}}") && store.find("1_68") &&
                (store.find("1_68")->pollEnabled == 1), "polling enabled");
    TEST_ASSERT(store.toJson() == "{\"devices\":{\"1_68\":{\"poll\":true}}}", "minimal document");
}

int main()
{
    testRoundTrip();
    testLoadEdges();
    return testSummary("test_device_settings");
}
