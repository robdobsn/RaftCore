/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Unit test of RaftJson Performance
//
// Rob Dobson 2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "unity.h"
#include "Logger.h"
#include "RaftJson.h"
#include "PerfTestMacros.h"

static const char* MODULE_PREFIX = "RdJsonPerfTestSmall";

static uint64_t perfStartTimeUs = 0;
static uint64_t perfTestExtractTestUs = 0;
static constexpr int NUM_LOOPS_PERF_TEST = 100;

String testVarName;
double testVal;
void testAddVar(const char* varName, double val)
{
    testVarName = varName;
    testVal = val;
}

String testJSONHWElem = 
R"("hw":[{"name":"LeftTwist","type":"SmartServo","busName":"I2CA","addr":"0x11","idx":1,
"whoAmI":"","serialNo":"4f7aa220974cadc7","versionStr":"0.0.0","commsOk":1,
"pos":107.40,"curr":0,"state":0,"velo":-26804}])";

TEST_CASE("test_json_perf_extract", "[jsonperf]")
{
    EVAL_PERF_START();

    String elemName = "testElem";
    // Setup the variables
    for (int i = 0; i < NUM_LOOPS_PERF_TEST; i++)
    {
        // Extract important info from each element and add to variables
        RaftJson elemConf = testJSONHWElem;
        RaftJson elemHw = elemConf.getString("hw[0]", "{}");

        // Debug
#ifdef DEBUG_TRAJECTORY_MANAGER
        LOG_I(MODULE_PREFIX, "exec add elemName %s elemHw %s", elemName.c_str(), elemHw.c_str());
#endif

        // Index
        int elemIdx = elemHw.getLong("idx", -1);
        if (elemIdx >= 0)
        {
            // Add variable
            String idxVarName = elemName;
            // _evaluator.addVariable(elemName.c_str(), elemIdx);
            testAddVar(elemName.c_str(), elemIdx);
        }

        // Hardware specific values
        String elemType = elemHw.getString("type", "");
        if (elemType == "SmartServo")
        {
            double pos = elemHw.getDouble("pos", -1e10);
            if (pos > -360)
            {
                String posVarName = elemName + "Pos";
                // _evaluator.addVariable(posVarName.c_str(), pos);
                testAddVar(elemName.c_str(), pos);
            }
        }
    }

    EVAL_PERF_END(perfTestExtractTestUs);

    LOG_I(MODULE_PREFIX, "Loop100Ms %fms", perfTestExtractTestUs/1000.0);
}