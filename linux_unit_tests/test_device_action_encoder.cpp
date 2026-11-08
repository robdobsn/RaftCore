/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DeviceActionEncoder tests
//
// The shared vectors (scripts/tests/action_encode_vectors.json, made with raftjs's encoder) - so the firmware's
// devman/action encodes actions exactly as raftjs does - and struct packing.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include "testhdr.h"
#include "ArduinoWString.h"
#include "RaftJson.h"
#include "RaftUtils.h"
#include "DeviceActionEncoder.h"

static String toHex(const std::vector<uint8_t>& bytes)
{
    String s;
    char buf[3];
    for (uint8_t b : bytes)
    {
        snprintf(buf, sizeof(buf), "%02x", b);
        s += buf;
    }
    return s;
}

static std::vector<String> valuesOf(const RaftJson& vec, const String& path)
{
    std::vector<String> values;
    int len = 0;
    vec.getType(path.c_str(), len);
    for (int i = 0; i < len; i++)
        values.push_back(vec.getString((path + "[" + String(i) + "]").c_str(), ""));
    return values;
}

static void testVectors(const char* vectorsPath)
{
    printf("Running testVectors (%s)...\n", vectorsPath);
    std::ifstream f(vectorsPath);
    TEST_ASSERT(f.good(), "vectors file opened");
    if (!f.good())
        return;
    std::stringstream ss;
    ss << f.rdbuf();
    RaftJson vec(ss.str().c_str());

    int numCases = 0;
    vec.getType("cases", numCases);
    TEST_ASSERT(numCases > 0, "cases present");
    for (int i = 0; i < numCases; i++)
    {
        String prefix = "cases[" + String(i) + "]";
        String desc = vec.getString((prefix + "/desc").c_str(), "");
        RaftJson action(vec.getString((prefix + "/action").c_str(), "{}").c_str());
        std::vector<String> values = valuesOf(vec, prefix + "/values");
        std::vector<std::vector<uint8_t>> writes;
        DeviceActionEncoder::Result rslt = DeviceActionEncoder::encodeAction(action, values, writes);
        String got;
        for (size_t w = 0; w < writes.size(); w++)
            got += (w ? "," : "") + toHex(writes[w]);
        std::vector<String> expected = valuesOf(vec, prefix + "/writes");
        String exp;
        for (size_t w = 0; w < expected.size(); w++)
            exp += (w ? "," : "") + expected[w];
        bool ok = (rslt == DeviceActionEncoder::Result::OK) && (got == exp);
        if (!ok)
            printf("  case \"%s\": result %s got [%s] expected [%s]\n", desc.c_str(),
                        DeviceActionEncoder::getResultStr(rslt), got.c_str(), exp.c_str());
        TEST_ASSERT(ok, desc.c_str());
    }

    int numErrors = 0;
    vec.getType("errors", numErrors);
    for (int i = 0; i < numErrors; i++)
    {
        String prefix = "errors[" + String(i) + "]";
        String desc = vec.getString((prefix + "/desc").c_str(), "");
        String expectedErr = vec.getString((prefix + "/error").c_str(), "");
        RaftJson action(vec.getString((prefix + "/action").c_str(), "{}").c_str());
        std::vector<String> values = valuesOf(vec, prefix + "/values");
        std::vector<std::vector<uint8_t>> writes;
        DeviceActionEncoder::Result rslt = DeviceActionEncoder::encodeAction(action, values, writes);
        String got = DeviceActionEncoder::getResultStr(rslt);
        bool ok = got == ("fail" + expectedErr);
        if (!ok)
            printf("  error case \"%s\": got %s expected fail%s\n", desc.c_str(), got.c_str(), expectedErr.c_str());
        TEST_ASSERT(ok, desc.c_str());
    }
}

// Encoding by action name from a whole record
static void testEncodeByName()
{
    printf("Running testEncodeByName...\n");
    const char* devInfo = R"({"name":"Synth","actions":[{"n":"noteOn","t":"B","w":"01"},{"n":"bend","t":"<h","w":"08"}]})";
    std::vector<std::vector<uint8_t>> writes;
    TEST_ASSERT(DeviceActionEncoder::encode(devInfo, "bend", {"100"}, writes) == DeviceActionEncoder::Result::OK &&
                writes.size() == 1 && toHex(writes[0]) == "086400", "encode by name");
    TEST_ASSERT(DeviceActionEncoder::encode(devInfo, "volume", {"5"}, writes) == DeviceActionEncoder::Result::NoSuchAction,
                "no such action");
    TEST_ASSERT(DeviceActionEncoder::encode(R"({"name":"x"})", "noteOn", {"60"}, writes) == DeviceActionEncoder::Result::NoActions,
                "record without actions");
}

int main(int argc, char** argv)
{
    testVectors(argc > 1 ? argv[1] : "../scripts/tests/action_encode_vectors.json");
    testEncodeByName();
    return testSummary("test_device_action_encoder");
}
