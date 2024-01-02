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
#include "JSON_test_data_large.h"
#include "PerfTestMacros.h"

#define DEBUG_RDJSON_PERF_TEST

static const char* MODULE_PREFIX = "RdJsonPerfTest";

static uint64_t perfStartTimeUs = 0;
static uint64_t perfParseUs = 0;
static uint64_t perfParseUsArduinoJson = 0;
static uint64_t perfFindKeyUs = 0;
static uint64_t perfGetString1Us = 0;
static uint64_t perfGetStringArduinoJson = 0;
static uint64_t perfTestDirectGetValUs = 0;
static uint64_t perfTestIterateOverStringUs = 0;

static constexpr int NUM_LOOPS_PERF_TEST = 100;

TEST_CASE("test_json_small_large", "[jsonperf]")
{

    // Parse json into tokens
    int numTokens = 0;
    EVAL_PERF_CLEAR_START(perfParseUs);
    jsmntok_t *pTokens = RaftJson_jsmn::parseJson(JSON_test_data_large, numTokens);
    EVAL_PERF_END(perfParseUs);

    if (pTokens == NULL)
    {
        LOG_I(MODULE_PREFIX, "testFindElemEnd parseJson failed");
        return;
    }

    // Find element
    EVAL_PERF_CLEAR_START(perfFindKeyUs);
    String testStr = RaftJson_jsmn::getStringStatic("[0]/Robot/WorkMgr/WorkQ/maxLen[0]/__value__", "", JSON_test_data_large);
    EVAL_PERF_END(perfFindKeyUs);

    // ArduinoJson
    DynamicJsonDocument doc(30000);
    EVAL_PERF_CLEAR_START(perfParseUsArduinoJson);
    DeserializationError error = deserializeJson(doc, JSON_test_data_large);
    EVAL_PERF_END(perfParseUsArduinoJson);
    if (error)
    {
        LOG_I(MODULE_PREFIX, "testFindElemEnd deserializeJson failed %s", error.c_str());
        return;
    }

    // Extract element
    EVAL_PERF_CLEAR_START(perfGetStringArduinoJson);
    long time = doc[0]["Robot"]["WorkMgr"]["WorkQ"]["maxLen"][0]["__value__"];
    EVAL_PERF_END(perfGetStringArduinoJson);
    if (time != 50)
    {
        LOG_I(MODULE_PREFIX, "testFindElemEnd ArduinoJson failed to extract element");
    }

    // perfTestDirectGetValUs
    static constexpr int NUM_LOOPS_PERF_TEST = 100;
    EVAL_PERF_CLEAR_START(perfTestDirectGetValUs);
    int workQMaxLen = 0;
    for (int i = 0; i < NUM_LOOPS_PERF_TEST; i++)
    {
        workQMaxLen = RaftJson::getLongStatic(JSON_test_data_large, "[0]/Robot/WorkMgr/WorkQ/maxLen[0]/__value__", 0, "");
    }
    EVAL_PERF_END(perfTestDirectGetValUs);

    // Time reading string end-to-end only
    EVAL_PERF_CLEAR_START(perfTestIterateOverStringUs);
    int iii = 0;
    for (int i = 0; i < NUM_LOOPS_PERF_TEST; i++)
    {
        const char* pPos = JSON_test_data_large;
        while (*pPos != '\0')
        {
            if (*pPos == '\n')
            {
                iii++;
            }
            pPos++;
        }
    }
    EVAL_PERF_END(perfTestIterateOverStringUs);


#ifdef DEBUG_RDJSON_PERF_TEST
    LOG_I(MODULE_PREFIX, "Parse %fus perfFindKeyUs %fus perfParseUsArduinoJson %fus perfGetStringArduinoJson %fus perfTestDirectGetValUs %fus perfTestIterateOverStringUs %fus workQMax %d",
                perfParseUs/1.0, 
                perfFindKeyUs/1.0, 
                perfParseUsArduinoJson/1.0, 
                perfGetStringArduinoJson/1.0, 
                perfTestDirectGetValUs/NUM_LOOPS_PERF_TEST/1.0,
                perfTestIterateOverStringUs/NUM_LOOPS_PERF_TEST/1.0,
                workQMaxLen);
#endif
}
