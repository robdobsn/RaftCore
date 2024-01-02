/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Unit test of RaftJson Performance
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "Logger.h"
#include "RaftArduino.h"
#include "RaftUtils.h"
#include "RaftJson.h"
#include "ArduinoJson.h"
#include "RaftJson_jsmn.h"
#include "JSON_test_data_small.h"
#include "PerfTestMacros.h"

#define DEBUG_RDJSON_PERF_TEST

static const char* MODULE_PREFIX = "RdJsonPerfTestJsmn";
static constexpr int NUM_LOOPS_PERF_TEST = 100;

TEST_CASE("test_json_perf_small", "[jsonperf]")
{
    LOG_I(MODULE_PREFIX, "--------------------------------- free heap %d", esp_get_free_heap_size());

    // Parse json into tokens
    int numTokens = 0;
    EVAL_PERF_START(perfParse);
    jsmntok_t *pTokens = RaftJson_jsmn::parseJson(JSON_test_data_small, numTokens);
    EVAL_PERF_END(perfParse);

    TEST_ASSERT_MESSAGE(pTokens != NULL, "JSMN parseJson failed");

    // Debug
    // RaftJson::debugDumpParseResult(testJSON, pTokens, numTokens);

    // // Test the findElemEnd function
    // int endTokens[] = {44,2,44,4,5,
    //                 6,30,8,30,10,
    //                 11,12,30,14,18,
    //                 16,17,18,19,20,
    //                 21,28,23,24,25,
    //                 26,27,28,29,30,
    //                 31,32,33,42,35,
    //                 36,37,38,39,42,
    //                 41,42,43,44};
    // EVAL_PERF_CLEAR(perfFindElemEndUs);
    // const int END_TOKENS_SIZE = sizeof(endTokens)/sizeof(int);
    // for (int tokStart = 0; tokStart < END_TOKENS_SIZE; tokStart++)
    // {
    //     String tokStartStr = "testFindElem start=" + String(tokStart);
    //     EVAL_PERF_START();
    //     testFindElemEnd(pTokens, numTokens, tokStart, endTokens[tokStart], testJSON);
    //     EVAL_PERF_ACCUM(perfFindElemEndUs);
    // }

    // Test the findKeyInJson function
    struct TestElem
    {
        const char* dataPath;
        const char* expStr;
    };
    TestElem findKeyTests [] = {
        { "consts/axis", "1" },
        { "consts/oxis/coo[2]", "dog" },
        { "consts/oxis/coo[3]/minotaur", "[1, 3, 4]" },
        { "consts/oxis/coo[3]/combine", "aaargh" },
        { "consts/oxis/coo[3]/slippery/nice", "{}" },
        { "consts/oxis/coo[3]/foo", "bar" },
        { "consts/exis", "banana" },
        { "consts/comarr/[0]", "6" },
        { "consts/comarr/[4]", "3" },
        { "consts/comarr/[5]/fish", "stew" },
        { "consts/lastly", "elephant" },
    };
    EVAL_PERF_START(jsmnFindKey);
    const int FIND_KEY_TESTS_SIZE = sizeof(findKeyTests)/sizeof(findKeyTests[0]);
    for (int testIdx = 0; testIdx < FIND_KEY_TESTS_SIZE; testIdx++)
    {
        String tokStartStr = "testFindElem testkeyIdx=" + String(testIdx);
        // testFindKeyInJson(pTokens, numTokens, findKeyTests[testIdx].dataPath, findKeyTests[testIdx].expStr, testJSON);
    }
    EVAL_PERF_END(jsmnFindKey);

    // Cleanup
    delete[] pTokens;

    String testJsonHw = R"({"name":"LeftTwist","type":"SmartServo","busName":"I2CA","addr":"0x11","idx":"1","whoAmI":"","serialNo":"4f7aa220974cadc7","versionStr":"0.0.0","commsOk":1,"pos":107.70,"curr":0,"state":0,"velo":-26804})";
    // // Test higher level methods
    EVAL_PERF_START(jsmnGetString);
    for (int i = 0; i < NUM_LOOPS_PERF_TEST; i++)
        RaftJson::getLongStatic(JSON_test_data_small, "idx", 0);
    EVAL_PERF_END(jsmnGetString);

    // TEST_ASSERT_MESSAGE(true == testGetString("consts/lastly", "elephant", testJSON), "getString2");
    // TEST_ASSERT_MESSAGE(5 == RaftJson::getLong("consts/comarr/[1]", -1, testJSON), "getLong1");

    // // Test array elements
    // const char* expectedStrs[] = {"6", "5", "4", "3", "3", "{\"fish\": \"stew\"}"};
    // TEST_ASSERT_MESSAGE(true == testGetArrayElems("consts/comarr", expectedStrs, sizeof(expectedStrs)/sizeof(expectedStrs[0]), testJSON), "getArrayElems1");

    // // Test object keys
    // const char* expectedKeys[] = {"axis", "oxis", "exis", "comarr", "lastly"};
    // TEST_ASSERT_MESSAGE(true == testGetObjectKeys("consts", expectedKeys, sizeof(expectedKeys)/sizeof(expectedKeys[0]), testJSON), "getKeys1");

    // Dump timings
#ifdef DEBUG_RDJSON_PERF_TEST
    EVAL_PERF_LOG(perfParse, "JSMN Parse", 1);
    EVAL_PERF_LOG(jsmnFindKey, "JSMN FindKey", 1);
    EVAL_PERF_LOG(jsmnGetString, "JSMN GetString", NUM_LOOPS_PERF_TEST);


    // LOG_I(MODULE_PREFIX, "Parse %fus FindKeyAvg %fus GetStr1 %fus", 
    //             perfParseUs/1.0, 
    //             perfFindKeyUs/FIND_KEY_TESTS_SIZE/1.0, 
    //             perfGetString1Us/NUM_LOOPS_PERF_TEST/1.0);
#endif
}