/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Unit tests of RaftJson value extraction
//
// Rob Dobson 2017-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "unity_test_runner.h"
#include "RaftArduino.h"
#include "RaftJson.h"
#include "Logger.h"
#include "RaftUtils.h"

// static const char* MODULE_PREFIX = "RaftJsonNVPairTest";

TEST_CASE("test_raftjson_nv_pairs", "[raftjson_nv_pairs]")
{
    const char* testInput = R"(0x020701=&0x020801=&0x009600=&0x0097fd&0x00e301=&0x00e403=r1&0x00e502=&0x00e601=&0x00e703=0x123456&0x00f502=&0x00d905=&0x00dbce=&0x00dc03=&0x00ddf8=&0x009f00=&0x00a33c=&0x00b700=&0x00bb3c=&0x00b209=&0x00ca09=&0x019801=&0x01b017=&0x01ad00=&0x00ff05=r55;0x010005=&0x019905=&0x01a61b;0x01ac3e=&0x01a71f=&0x003000=;0x001110=&0x010a30=&0x003f46=&0x0031ff=&0x004163=&0x002e01=&0x001b09=&0x003e31=&0x001424=)";

    std::vector<RaftJson::NameValuePair> nvPairs;
    RaftJson::extractNameValues(testInput, "=", "&", ";", nvPairs);

    // LOG_I(MODULE_PREFIX, "test_raftjson_nv_pairs numPairs %d", nvPairs.size());
    // for (size_t i = 0; i < nvPairs.size(); i++)
    // {
    //     LOG_I(MODULE_PREFIX, "test_raftjson_nv_pairs %d %s %s", i, nvPairs[i].name.c_str(), nvPairs[i].value.c_str());
    // }

    // Tests
    TEST_ASSERT_MESSAGE(nvPairs.size() == 39, "testNumNVPairs");
    TEST_ASSERT_MESSAGE(nvPairs[4].name == "0x00e301", "testNVPair4Name");
    TEST_ASSERT_MESSAGE(nvPairs[4].value == "", "testNVPair4Value");
    TEST_ASSERT_MESSAGE(nvPairs[5].name == "0x00e403", "testNVPair5Name");
    TEST_ASSERT_MESSAGE(nvPairs[5].value == "r1", "testNVPair5Value");
    TEST_ASSERT_MESSAGE(nvPairs[6].name == "0x00e502", "testNVPair6Name");
    TEST_ASSERT_MESSAGE(nvPairs[6].value == "", "testNVPair6Value");
    TEST_ASSERT_MESSAGE(nvPairs[7].name == "0x00e601", "testNVPair7Name");
    TEST_ASSERT_MESSAGE(nvPairs[7].value == "", "testNVPair7Value");
    TEST_ASSERT_MESSAGE(nvPairs[8].name == "0x00e703", "testNVPair8Name");
    TEST_ASSERT_MESSAGE(nvPairs[8].value == "0x123456", "testNVPair8Value");
    TEST_ASSERT_MESSAGE(nvPairs[9].name == "0x00f502", "testNVPair9Name");
    TEST_ASSERT_MESSAGE(nvPairs[9].value == "", "testNVPair9Value");
    TEST_ASSERT_MESSAGE(nvPairs[10].name == "0x00d905", "testNVPair10Name");
    TEST_ASSERT_MESSAGE(nvPairs[10].value == "", "testNVPair10Value");
    TEST_ASSERT_MESSAGE(nvPairs[22].name == "0x01ad00", "testNVPair22Name");
    TEST_ASSERT_MESSAGE(nvPairs[22].value == "", "testNVPair22Value");
    TEST_ASSERT_MESSAGE(nvPairs[23].name == "0x00ff05", "testNVPair23Name");
    TEST_ASSERT_MESSAGE(nvPairs[23].value == "r55", "testNVPair23Value");
    TEST_ASSERT_MESSAGE(nvPairs[24].name == "0x010005", "testNVPair24Name");
    TEST_ASSERT_MESSAGE(nvPairs[24].value == "", "testNVPair24Value");
}
