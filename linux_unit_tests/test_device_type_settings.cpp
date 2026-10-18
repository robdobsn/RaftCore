/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device type settings tests (DevMan "typeSettings" - record management plan section 3.9)
//
// - Settings are read for each type, with values given as numbers or text; busHzSlots as a list or text.
// - Unknown setting names and invalid values are reported and left out.
// - scanPriority is read as a level (high, medium, low, none), not as a devconfig setting.
// - Layering: the device's own settings replace the type's with the same name.
// - Names starting with "_" (notes) are ignored.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <string>
#include "testhdr.h"
#include "ArduinoWString.h"
#include "RaftJson.h"
#include "DeviceTypeSettings.h"

static String valueOf(const std::vector<RaftJson::NameValuePair>& values, const char* name)
{
    for (const auto& nv : values)
        if (nv.name == name)
            return nv.value;
    return "<none>";
}

static void testParse()
{
    printf("Running testParse...\n");
    RaftJson config(R"({"typeSettings":{
        "LSM6DS3":{"busHz":400000,"busHzSlots":[1,2,3,4,7],"intervalUs":"20000"},
        "VL6180":{"scanPriority":"low","sampleRateHz":50,"numSamples":4},
        "MCP9808":{"busHzSlots":"4, 5","scanPriority":"none","_notes":"why"},"_notes":"about these"}})");
    DeviceTypeSettings settings;
    std::vector<String> problems;
    settings.parse(config, "typeSettings", problems);
    TEST_ASSERT(problems.empty(), problems.empty() ? "" : problems[0].c_str());
    TEST_ASSERT(settings.all().size() == 3, "three types");
    const DeviceTypeSettings::Entry* pLsm = settings.find("LSM6DS3");
    TEST_ASSERT(pLsm && (pLsm->devConfig.size() == 3), "LSM6DS3 settings");
    TEST_ASSERT(pLsm && (valueOf(pLsm->devConfig, "busHz") == "400000"), "busHz as text");
    TEST_ASSERT(pLsm && (valueOf(pLsm->devConfig, "busHzSlots") == "1,2,3,4,7"), "busHzSlots list as text");
    TEST_ASSERT(pLsm && (valueOf(pLsm->devConfig, "intervalUs") == "20000"), "intervalUs given as text");
    TEST_ASSERT(pLsm && (pLsm->scanPriority == DeviceTypeSettings::SCAN_PRIORITY_UNSET), "no scan priority");
    const DeviceTypeSettings::Entry* pVl = settings.find("VL6180");
    TEST_ASSERT(pVl && (pVl->scanPriority == 2) && (pVl->devConfig.size() == 2), "scanPriority low, not a devconfig setting");
    const DeviceTypeSettings::Entry* pMcp = settings.find("MCP9808");
    TEST_ASSERT(pMcp && (pMcp->scanPriority == DeviceTypeRecord::SCAN_PRIORITY_NONE), "scanPriority none");
    TEST_ASSERT(pMcp && (valueOf(pMcp->devConfig, "busHzSlots") == "4, 5"), "busHzSlots text kept");
    TEST_ASSERT(settings.find("Nope") == nullptr, "no settings for another type");
}

static void testProblems()
{
    printf("Running testProblems...\n");
    RaftJson config(R"({"typeSettings":{"A":{"busHz":"fast","intervalUs":0,"numSamples":-1,"busHzSlots":[1,64],
        "scanPriority":"urgent","sampleRateHz":0,"colour":"red","pollInfo":{"i":5}},"B":{"intervalUs":1000}}})");
    DeviceTypeSettings settings;
    std::vector<String> problems;
    settings.parse(config, "typeSettings", problems);
    TEST_ASSERT(problems.size() == 8, ("eight problems, got " + std::to_string(problems.size())).c_str());
    const DeviceTypeSettings::Entry* pA = settings.find("A");
    TEST_ASSERT(pA && pA->devConfig.empty() && (pA->scanPriority == DeviceTypeSettings::SCAN_PRIORITY_UNSET),
                "invalid settings left out");
    bool named = false;
    for (const String& p : problems)
        named = named || p.startsWith("A.colour: not a type setting");
    TEST_ASSERT(named, "a problem names the type and setting");
    TEST_ASSERT(settings.find("B") && (settings.find("B")->devConfig.size() == 1), "valid type kept");
    RaftJson empty(R"({"other":1})");
    settings.parse(empty, "typeSettings", problems);
    TEST_ASSERT(settings.all().empty(), "no settings");
}

static void testLayer()
{
    printf("Running testLayer...\n");
    std::vector<RaftJson::NameValuePair> typeValues = {{"busHz", "400000"}, {"intervalUs", "20000"}};
    std::vector<RaftJson::NameValuePair> deviceValues = {{"intervalUs", "5000"}, {"numSamples", "8"}};
    std::vector<RaftJson::NameValuePair> layered = DeviceTypeSettings::layer(typeValues, deviceValues);
    TEST_ASSERT(layered.size() == 3, "three settings");
    TEST_ASSERT(valueOf(layered, "busHz") == "400000", "the type's setting");
    TEST_ASSERT(valueOf(layered, "intervalUs") == "5000", "the device's setting replaces the type's");
    TEST_ASSERT(valueOf(layered, "numSamples") == "8", "the device's own setting");
    TEST_ASSERT(DeviceTypeSettings::layer({}, {}).empty(), "nothing to apply");
    TEST_ASSERT(DeviceTypeSettings::layer(typeValues, {}).size() == 2, "type settings only");
}

int main()
{
    testParse();
    testProblems();
    testLayer();
    return testSummary("test_device_type_settings");
}
