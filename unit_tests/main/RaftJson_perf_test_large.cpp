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
#include "JSON_test_data_large.h"
#include "PerfTestMacros.h"

static const char* MODULE_PREFIX = "JsonPerfTestLarge";
static constexpr int NUM_LOOPS_PERF_TEST = 100;
static const int EXPECTED_WORK_Q_MAX_LEN = 50;
static const int EXPECTED_MONITOR_PERIOD_MS = 10000;
static const int EXPECTED_SAFETIES_MAX_MS = 5000;

TEST_CASE("test_json_large", "[jsonperf]")
{

    LOG_I(MODULE_PREFIX, "----------------- JSON large doc performance test --------------------");
    LOG_I(MODULE_PREFIX, "JSON doc size %d bytes, free heap at start %d", 
            strlen(JSON_test_data_large),
            esp_get_free_heap_size());

    // RaftJson_jsmn
    EVAL_PERF_START(jsmnObjCreate);
    RaftJson_jsmn jsmnObj(JSON_test_data_large, false);
    // The following line is needed to force the object to be created as it is generally lazy
    jsmnObj.getLong("", 0);
    EVAL_PERF_END(jsmnObjCreate);
    EVAL_PERF_START(jsmnObjGetIntLarge);
    int jsmnQMaxLen = 0;
    int jsmnMonitorPeriodMs = 0;
    int jsmnSafetiesMaxMs = 0;
    for (int i = 0; i < NUM_LOOPS_PERF_TEST; i++)
    {
        jsmnQMaxLen += (int) jsmnObj.getLong("[0]/Robot/WorkMgr/WorkQ/maxLen[0]/__value__", 0);
        jsmnMonitorPeriodMs += (int) jsmnObj.getLong("[0]/SysManager/monitorPeriodMs", 0);
        jsmnSafetiesMaxMs += (int) jsmnObj.getLong("[0]/Robot/Safeties/maxMs", 0);
    }
    EVAL_PERF_END(jsmnObjGetIntLarge);
    TEST_ASSERT_MESSAGE(jsmnQMaxLen == EXPECTED_WORK_Q_MAX_LEN * NUM_LOOPS_PERF_TEST, "JSMN obj getLong failed");
    TEST_ASSERT_MESSAGE(jsmnMonitorPeriodMs == EXPECTED_MONITOR_PERIOD_MS * NUM_LOOPS_PERF_TEST, "JSMN obj getLong failed");
    TEST_ASSERT_MESSAGE(jsmnSafetiesMaxMs == EXPECTED_SAFETIES_MAX_MS * NUM_LOOPS_PERF_TEST, "JSMN obj getLong failed");

    // ArduinoJson
    EVAL_PERF_START(arduinoJsonParse);
    DynamicJsonDocument doc(30000);
    DeserializationError error = deserializeJson(doc, JSON_test_data_large);
    doc.shrinkToFit();
    EVAL_PERF_END(arduinoJsonParse);
    TEST_ASSERT_MESSAGE(!error, "ArduinoJson deserializeJson failed");

    // Extract element
    EVAL_PERF_START(arduinoJsonGetInt);
    int arduinoJsonQMaxLen = 0;
    int arduinoJsonMonitorPeriodMs = 0;
    int arduinoJsonSafetiesMaxMs = 0;
    for (int i = 0; i < NUM_LOOPS_PERF_TEST; i++)
    {
        arduinoJsonQMaxLen += int(doc[0]["Robot"]["WorkMgr"]["WorkQ"]["maxLen"][0]["__value__"]);
        arduinoJsonMonitorPeriodMs += int(doc[0]["SysManager"]["monitorPeriodMs"]);
        arduinoJsonSafetiesMaxMs += int(doc[0]["Robot"]["Safeties"]["maxMs"]);
    }
    EVAL_PERF_END(arduinoJsonGetInt);
    TEST_ASSERT_MESSAGE(arduinoJsonQMaxLen == EXPECTED_WORK_Q_MAX_LEN * NUM_LOOPS_PERF_TEST, "ArduinoJson failed to extract element");
    TEST_ASSERT_MESSAGE(arduinoJsonMonitorPeriodMs == EXPECTED_MONITOR_PERIOD_MS * NUM_LOOPS_PERF_TEST, "ArduinoJson failed to extract element");
    TEST_ASSERT_MESSAGE(arduinoJsonSafetiesMaxMs == EXPECTED_SAFETIES_MAX_MS * NUM_LOOPS_PERF_TEST, "ArduinoJson failed to extract element");

    // perfTestDirectGetValUs
    EVAL_PERF_START(raftJsonDirectGetInt);
    int raftJsonQMaxLen = 0;
    int raftJsonMonitorPeriodMs = 0;
    int raftJsonSafetiesMaxMs = 0;
    for (int i = 0; i < NUM_LOOPS_PERF_TEST; i++)
    {
        raftJsonQMaxLen += RaftJson::getLongIm(JSON_test_data_large, JSON_test_data_large_end, "[0]/Robot/WorkMgr/WorkQ/maxLen[0]/__value__", -1);
        raftJsonMonitorPeriodMs += RaftJson::getLongIm(JSON_test_data_large, JSON_test_data_large_end, "[0]/SysManager/monitorPeriodMs", -1);
        raftJsonSafetiesMaxMs += RaftJson::getLongIm(JSON_test_data_large, JSON_test_data_large_end, "[0]/Robot/Safeties/maxMs", -1);
    }
    EVAL_PERF_END(raftJsonDirectGetInt);
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

    EVAL_PERF_LOG(jsmnObjCreate, "RaftJSON_jsmn Create", 1);
    EVAL_PERF_LOG(jsmnObjGetIntLarge, "RaftJson_jsmn GetInt", (NUM_LOOPS_PERF_TEST * 3));
    EVAL_PERF_LOG(arduinoJsonParse, "ArduinoJson Parse", 1);
    EVAL_PERF_LOG(arduinoJsonGetInt, "ArduinoJson GetInt", (NUM_LOOPS_PERF_TEST * 3));
    EVAL_PERF_LOG(raftJsonDirectGetInt, "RaftJson Direct GetInt", (NUM_LOOPS_PERF_TEST * 3));
    EVAL_PERF_LOG(countJsonDocLines, "Count Json Doc Lines", NUM_LOOPS_PERF_TEST);
}
