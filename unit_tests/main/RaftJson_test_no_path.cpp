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

static const char* MODULE_PREFIX = "RaftJsonNoPathTest";

static const char* DEFAULT_STRING_VALUE = "<<<DEFAULT_STRING_VALUE>>>";
static bool testGetString(const char* pSourceStr, const char* pDataPath, const char* expStr)
{
    String val = RaftJson::getStringIm(pSourceStr, pDataPath, DEFAULT_STRING_VALUE);
    String expectedStr(expStr);
    expectedStr.trim();
    if (!val.equals(expectedStr))
    {
        LOG_I(MODULE_PREFIX, "testGetString failed expected %s != %s", expStr, val.c_str());
        return false;
    }
    return true;
}

TEST_CASE("test_raftjson_no_path", "[raftjson_no_path]")
{
    const char* testJSON = R"({"unitsPerRot":360,"stepsPerRot":28000,"maxSpeed":10,"maxAcc":10})";

    // Test the getString function
    TEST_ASSERT_MESSAGE(true == testGetString(testJSON, "", testJSON), "getStringNoPath");
    TEST_ASSERT_MESSAGE(true == testGetString(testJSON, nullptr, DEFAULT_STRING_VALUE), "getStringNullPath");
}