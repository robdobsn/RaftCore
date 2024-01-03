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

static const char* MODULE_PREFIX = "JsonPerfTestSmall";
static constexpr int NUM_LOOPS_PERF_TEST = 100;
static const int EXPECTED_CONSTS_AXIS = 1;
static const int EXPECTED_MINOTAUR = 4;
static const int EXPECTED_COMARR = 3;

TEST_CASE("test_json_small", "[jsonperf]")
{
    LOG_I(MODULE_PREFIX, "----------------- JSON small doc performance test --------------------");
    LOG_I(MODULE_PREFIX, "JSON doc size %d bytes, free heap at start %d", 
            strlen(JSON_test_data_small),
            esp_get_free_heap_size());

    // RaftJson_jsmn
    EVAL_PERF_START(jsmnObjCreate);
    RaftJson_jsmn jsmnObj(JSON_test_data_small, false);
    // The following line is needed to force the object to be created as it is generally lazy
    jsmnObj.getLong("", 0);
    EVAL_PERF_END(jsmnObjCreate);
    EVAL_PERF_START(jsmnObjGetIntSmall);
    int jsmnConstsAxis = 0;
    int jsmnMinotaur = 0;
    int jsmnComarr = 0;
    for (int i = 0; i < NUM_LOOPS_PERF_TEST; i++)
    {
        jsmnConstsAxis += (int) jsmnObj.getLong("consts/axis", 0);
        jsmnMinotaur += (int) jsmnObj.getLong("consts/oxis/coo[3]/minotaur[2]", 0);
        jsmnComarr += (int) jsmnObj.getLong("consts/comarr[4]", 0);
    }
    EVAL_PERF_END(jsmnObjGetIntSmall);
    TEST_ASSERT_MESSAGE(jsmnConstsAxis == EXPECTED_CONSTS_AXIS * NUM_LOOPS_PERF_TEST, "JSMN obj getLong failed");
    TEST_ASSERT_MESSAGE(jsmnMinotaur == EXPECTED_MINOTAUR * NUM_LOOPS_PERF_TEST, "JSMN obj getLong failed");
    TEST_ASSERT_MESSAGE(jsmnComarr == EXPECTED_COMARR * NUM_LOOPS_PERF_TEST, "JSMN obj getLong failed");

    // ArduinoJson parse
    EVAL_PERF_START(arduinoJsonParse);
    DynamicJsonDocument doc(30000);
    DeserializationError error = deserializeJson(doc, JSON_test_data_small);
    doc.shrinkToFit();
    EVAL_PERF_END(arduinoJsonParse);
    TEST_ASSERT_MESSAGE(!error, "ArduinoJson deserializeJson failed");

    // ArduinoJson extract elements
    EVAL_PERF_START(arduinoJsonGetInt);
    int arduinoJsonConstsAxis = 0;
    int arduinoJsonMinotaur = 0;
    int arduinoJsonComarr = 0;
    for (int i = 0; i < NUM_LOOPS_PERF_TEST; i++)
    {
        arduinoJsonConstsAxis += int(doc["consts"]["axis"]);
        arduinoJsonMinotaur += int(doc["consts"]["oxis"]["coo"][3]["minotaur"][2]);
        arduinoJsonComarr += int(doc["consts"]["comarr"][4]);
    }
    EVAL_PERF_END(arduinoJsonGetInt);
    TEST_ASSERT_MESSAGE(arduinoJsonConstsAxis == EXPECTED_CONSTS_AXIS * NUM_LOOPS_PERF_TEST, "ArduinoJson getInt failed");
    TEST_ASSERT_MESSAGE(arduinoJsonMinotaur == EXPECTED_MINOTAUR * NUM_LOOPS_PERF_TEST, "ArduinoJson getInt failed");
    TEST_ASSERT_MESSAGE(arduinoJsonComarr == EXPECTED_COMARR * NUM_LOOPS_PERF_TEST, "ArduinoJson getInt failed");

    // Test direct extraction
    EVAL_PERF_START(raftJsonDirectGetInt);
    int raftJsonConstsAxis = 0;
    int raftJsonMinotaur = 0;
    int raftJsonComarr = 0;
    for (int i = 0; i < NUM_LOOPS_PERF_TEST; i++)
    {
        raftJsonConstsAxis += RaftJson::getLong(JSON_test_data_small, "consts/axis", -1);
        raftJsonMinotaur += RaftJson::getLong(JSON_test_data_small, "consts/oxis/coo[3]/minotaur[2]", -1);
        raftJsonComarr += RaftJson::getLong(JSON_test_data_small, "consts/comarr[4]", -1);
    }
    EVAL_PERF_END(raftJsonDirectGetInt);
    TEST_ASSERT_MESSAGE(raftJsonConstsAxis == EXPECTED_CONSTS_AXIS * NUM_LOOPS_PERF_TEST, "RaftJson failed to extract element");
    TEST_ASSERT_MESSAGE(raftJsonMinotaur == EXPECTED_MINOTAUR * NUM_LOOPS_PERF_TEST, "RaftJson failed to extract element");
    TEST_ASSERT_MESSAGE(raftJsonComarr == EXPECTED_COMARR * NUM_LOOPS_PERF_TEST, "RaftJson failed to extract element");
    // LOG_I(MODULE_PREFIX, "RaftJson constsAxis %d minotaur %d comarr %d", raftJsonConstsAxis/NUM_LOOPS_PERF_TEST, raftJsonMinotaur/NUM_LOOPS_PERF_TEST, raftJsonComarr/NUM_LOOPS_PERF_TEST);

    EVAL_PERF_LOG(jsmnObjCreate, "RaftJSON_jsmn Create", 1);
    EVAL_PERF_LOG(jsmnObjGetIntSmall, "RaftJson_jsmn GetInt", (NUM_LOOPS_PERF_TEST * 3));
    EVAL_PERF_LOG(arduinoJsonParse, "ArduinoJson Parse", 1);
    EVAL_PERF_LOG(arduinoJsonGetInt, "ArduinoJson GetInt", (NUM_LOOPS_PERF_TEST * 3));
    EVAL_PERF_LOG(raftJsonDirectGetInt, "RaftJson Direct GetInt", (NUM_LOOPS_PERF_TEST * 3));
}
