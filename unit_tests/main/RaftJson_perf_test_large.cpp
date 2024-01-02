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
// #include "JSON_test_data_small.h"
#include "JSON_test_data_large.h"
#include "PerfTestMacros.h"

#define DEBUG_RDJSON_PERF_TEST

static const char* MODULE_PREFIX = "RdJsonPerfTest";
static constexpr int NUM_LOOPS_PERF_TEST = 100;
static const int EXPECTED_WORK_Q_MAX_LEN = 50;
static const int EXPECTED_MONITOR_PERIOD_MS = 10000;
static const int EXPECTED_SAFETIES_MAX_MS = 5000;

TEST_CASE("test_json_large", "[jsonperf]")
{

    // Parse json into tokens
    int numTokens = 0;
    EVAL_PERF_START(jsmnParse);
    jsmntok_t *pTokens = RaftJson_jsmn::parseJson(JSON_test_data_large, numTokens);
    EVAL_PERF_END(jsmnParse);
    TEST_ASSERT_MESSAGE(pTokens != NULL, "JSMN parseJson failed");

    // Find element
    EVAL_PERF_START(jsmnGetString);
    String testStr = RaftJson_jsmn::getStringStatic("[0]/Robot/WorkMgr/WorkQ/maxLen[0]/__value__", "", JSON_test_data_large);
    EVAL_PERF_END(jsmnGetString);
    TEST_ASSERT_MESSAGE(testStr.equals(String(EXPECTED_WORK_Q_MAX_LEN)), "JSMN getString failed");

    // ArduinoJson
    EVAL_PERF_START(arduinoJsonParse);
    DynamicJsonDocument doc(30000);
    DeserializationError error = deserializeJson(doc, JSON_test_data_large);
    doc.shrinkToFit();
    EVAL_PERF_END(arduinoJsonParse);
    TEST_ASSERT_MESSAGE(!error, "ArduinoJson deserializeJson failed");

    // Extract element
    EVAL_PERF_START(arduinoJsonGetString);
    int arduinoJsonQMaxLen = 0;
    int arduinoJsonMonitorPeriodMs = 0;
    int arduinoJsonSafetiesMaxMs = 0;
    for (int i = 0; i < NUM_LOOPS_PERF_TEST; i++)
    {
        arduinoJsonQMaxLen += int(doc[0]["Robot"]["WorkMgr"]["WorkQ"]["maxLen"][0]["__value__"]);
        arduinoJsonMonitorPeriodMs += int(doc[0]["SysManager"]["monitorPeriodMs"]);
        arduinoJsonSafetiesMaxMs += int(doc[0]["Robot"]["Safeties"]["maxMs"]);
    }
    EVAL_PERF_END(arduinoJsonGetString);
    TEST_ASSERT_MESSAGE(arduinoJsonQMaxLen == EXPECTED_WORK_Q_MAX_LEN * NUM_LOOPS_PERF_TEST, "ArduinoJson failed to extract element");
    TEST_ASSERT_MESSAGE(arduinoJsonMonitorPeriodMs == EXPECTED_MONITOR_PERIOD_MS * NUM_LOOPS_PERF_TEST, "ArduinoJson failed to extract element");
    TEST_ASSERT_MESSAGE(arduinoJsonSafetiesMaxMs == EXPECTED_SAFETIES_MAX_MS * NUM_LOOPS_PERF_TEST, "ArduinoJson failed to extract element");

    // perfTestDirectGetValUs
    EVAL_PERF_START(raftJsonDirectGetString);
    int raftJsonQMaxLen = 0;
    int raftJsonMonitorPeriodMs = 0;
    int raftJsonSafetiesMaxMs = 0;
    for (int i = 0; i < NUM_LOOPS_PERF_TEST; i++)
    {
        raftJsonQMaxLen += RaftJson::getLongStatic(JSON_test_data_large, "[0]/Robot/WorkMgr/WorkQ/maxLen[0]/__value__", -1);
        raftJsonMonitorPeriodMs += RaftJson::getLongStatic(JSON_test_data_large, "[0]/SysManager/monitorPeriodMs", -1);
        raftJsonSafetiesMaxMs += RaftJson::getLongStatic(JSON_test_data_large, "[0]/Robot/Safeties/maxMs", -1);
    }
    EVAL_PERF_END(raftJsonDirectGetString);
    TEST_ASSERT_MESSAGE(raftJsonQMaxLen == EXPECTED_WORK_Q_MAX_LEN * NUM_LOOPS_PERF_TEST, "RaftJson failed to extract element");
    TEST_ASSERT_MESSAGE(raftJsonMonitorPeriodMs == EXPECTED_MONITOR_PERIOD_MS * NUM_LOOPS_PERF_TEST, "RaftJson failed to extract element");
    TEST_ASSERT_MESSAGE(raftJsonSafetiesMaxMs == EXPECTED_SAFETIES_MAX_MS * NUM_LOOPS_PERF_TEST, "RaftJson failed to extract element");

    // Time reading string end-to-end only
    EVAL_PERF_START(countJsonDocLines);
    int jsonDocNumLines = 0;
    for (int i = 0; i < NUM_LOOPS_PERF_TEST; i++)
    {
        const char* pPos = JSON_test_data_large;
        while (*pPos != '\0')
        {
            if (*pPos == '\n')
            {
                jsonDocNumLines++;
            }
            pPos++;
        }
    }
    EVAL_PERF_END(countJsonDocLines);


#ifdef DEBUG_RDJSON_PERF_TEST
    EVAL_PERF_LOG(jsmnParse, "JSMN Parse", 1);
    EVAL_PERF_LOG(jsmnGetString, "JSMN GetString", 1);
    EVAL_PERF_LOG(arduinoJsonParse, "ArduinoJson Parse", 1);
    EVAL_PERF_LOG(arduinoJsonGetString, "ArduinoJson GetString", NUM_LOOPS_PERF_TEST * 3);
    EVAL_PERF_LOG(raftJsonDirectGetString, "RaftJson Direct GetString", NUM_LOOPS_PERF_TEST * 3);
    EVAL_PERF_LOG(countJsonDocLines, "Count Json Doc Lines", NUM_LOOPS_PERF_TEST);

    // LOG_I(MODULE_PREFIX, "JSMN parse %fus jsmnGetString %fus perfParseUsArduinoJson %fus perfGetStringArduinoJson %fus perfTestDirectGetValUs %fus perfTestIterateOverStringUs %fus workQMax %d",
    //             jsmnParseUs/1.0, 
    //             perfFindKeyUs/1.0, 
    //             perfParseUsArduinoJson/1.0, 
    //             perfGetStringArduinoJson/1.0, 
    //             perfTestDirectGetValUs/NUM_LOOPS_PERF_TEST/1.0,
    //             perfTestIterateOverStringUs/NUM_LOOPS_PERF_TEST/1.0,
    //             workQMaxLen);
#endif
}
