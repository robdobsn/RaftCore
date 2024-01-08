#include <stdio.h>
#include "utils.h"
#include "ArduinoWString.h"
#include "RaftJson.h"

#include "JSON_test_data_large.h"
#include "JSON_test_data_small.h"

#define TEST_ASSERT(cond, msg) if (!(cond)) { printf("TEST_ASSERT failed %s\n", msg); failCount++; }

int main()
{
    int constsAxis = RaftJson::getLongIm(JSON_test_data_small, "consts/axis", 0);
    int minotaur = RaftJson::getLongIm(JSON_test_data_small, "consts/oxis/coo[3]/minotaur[2]", 0);
    int comarr = RaftJson::getLongIm(JSON_test_data_small, "consts/comarr[4]", 0);

    int maxQ = RaftJson::getLongIm(JSON_test_data_large, "[0]/Robot/WorkMgr/WorkQ/maxLen[0]/__value__", 0);

    printf("Parse ConstsAxis %d minotaur %d maxQ %d comarr %d\n", constsAxis, minotaur, maxQ, comarr);

    const char* testJSON = 
            R"({                                        )"
            R"( "consts": {                             )"
            R"( 	"axis": "1",                        )"
            R"( 	"oxis": {                           )"
            R"( 		"coo": ["pig", 4, "dog", {      )"
            R"( 			"minotaur": [1, 3, 4],      )"
            R"( 			"combine": "aaargh",        )"
            R"( 			"slippery": {               )"
            R"( 				"animal": "goat",       )"
            R"( 				"nice": {},             )"
            R"( 				"polish": "shoes"       )"
            R"( 			},                          )"
            R"( 			"foo": "bar"                )"
            R"( 		}]                              )"
            R"( 	},                                  )"
            R"( 	"exis": "banana",                   )"
            R"( 	"comarr": [6, 5, 4, 3, 3,           )"
            R"( 		{"fish": "stew"}                )"
            R"( 	],                                  )"
            R"( 	"lastly": "elephant"    ,           )"
            R"( 	"bool1":false    ,                  )"
            R"( 	"bool2": 	  true                  )"
            R"( }                                       )"
            R"(}                                        )";

    // Test the getString function
    struct TestElem
    {
        const char* dataPath;
        const char* expStr;
    };
    TestElem findKeyTests [] = {
        { "", testJSON },
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
        { "consts/bool1", "false" },
        { "consts/bool2", "true" },
    };
    bool anyFailed = false;
    int findKeyTestsLen = sizeof(findKeyTests)/sizeof(findKeyTests[0]);
    for (int testIdx = 0; testIdx < findKeyTestsLen; testIdx++)
    {
        String keyStartStr = "testGetString testkeyIdx=" + String(testIdx);
        String val = RaftJson::getStringIm(testJSON, findKeyTests[testIdx].dataPath, "");
        String expStr = String(findKeyTests[testIdx].expStr);
        expStr.trim();
        if (val != expStr)
        {
            printf("testGetString failed %s <<<%s>>> != <<<%s>>>\n", findKeyTests[testIdx].dataPath, val.c_str(), findKeyTests[testIdx].expStr);
            anyFailed = true;
        }
    }
    if (!anyFailed)
        printf("testGetString all tests passed\n");

    // Test on a small document
    const char* testTinyJson = R"({"unitsPerRot":360,"stepsPerRot":28000,"maxSpeed":10,"maxAcc":10})";
    int MAX_RPM_DEFAULT_VALUE = -100000;
    struct TestTinyElem
    {
        const char* dataPath;
        int expInt;
    };
    const TestTinyElem testTinyExpectations [] = {
        { "maxRPM", MAX_RPM_DEFAULT_VALUE}
    };

    int testTinyExpectationsLen = sizeof(testTinyExpectations)/sizeof(testTinyExpectations[0]);
    for (int testIdx = 0; testIdx < testTinyExpectationsLen; testIdx++)
    {
        String keyStartStr = "testTinyJson testkeyIdx=" + String(testIdx);
        int val = RaftJson::getLongIm(testTinyJson, testTinyExpectations[testIdx].dataPath, MAX_RPM_DEFAULT_VALUE);
        if (val != testTinyExpectations[testIdx].expInt)
        {
            printf("testTinyJson failed %s <<<%d>>> != <<<%d>>>\n", testTinyExpectations[testIdx].dataPath, val, testTinyExpectations[testIdx].expInt);
            anyFailed = true;
        }
    }

    if (!anyFailed)
        printf("testTinyJson all tests passed\n");

    // Test on a document containing only primitives
    int failCount = 0;
    TEST_ASSERT(RaftJson::getStringIm("1234", "", "<<<>>>") == "1234", "testPrimitiveStr1");
    TEST_ASSERT(RaftJson::getStringIm("1234", nullptr, "<<<>>>") == "<<<>>>", "testPrimitiveStr2");
    TEST_ASSERT(RaftJson::getStringIm("1234", "abc", "<<<>>>") == "<<<>>>", "testPrimitiveStr3");
    TEST_ASSERT(RaftJson::getStringIm("1234", "abc", "") == "", "testPrimitiveStr5");
    TEST_ASSERT(RaftJson::getStringIm("null", "", "<<<>>>") == "null", "testPrimitiveStr6");
    TEST_ASSERT(RaftJson::getStringIm("null", nullptr, "<<<>>>") == "<<<>>>", "testPrimitiveStr7");
    TEST_ASSERT(RaftJson::getStringIm("null", "abc", "<<<>>>") == "<<<>>>", "testPrimitiveStr8");
    TEST_ASSERT(RaftJson::getStringIm("null", "abc", "") == "", "testPrimitiveStr9");
    TEST_ASSERT(RaftJson::getDoubleIm("1234", "", -1000000) == 1234.0, "testPrimitiveDouble1");
    TEST_ASSERT(RaftJson::getDoubleIm("1234", nullptr, -1000000) == -1000000, "testPrimitiveDouble2");
    TEST_ASSERT(RaftJson::getDoubleIm("1234", "abc", -1000000) == -1000000, "testPrimitiveDouble3");
    TEST_ASSERT(RaftJson::getDoubleIm("1234", "abc", 0) == 0, "testPrimitiveDouble4");
    TEST_ASSERT(RaftJson::getDoubleIm("null", "", -1000000) == -1000000, "testPrimitiveDouble5");
    TEST_ASSERT(RaftJson::getDoubleIm("null", nullptr, -1000000) == -1000000, "testPrimitiveDouble6");
    TEST_ASSERT(RaftJson::getDoubleIm("null", "abc", -1000000) == -1000000, "testPrimitiveDouble7");
    TEST_ASSERT(RaftJson::getDoubleIm("null", "abc", 0) == 0, "testPrimitiveDouble8");
    TEST_ASSERT(RaftJson::getLongIm("1234", "", -1000000) == 1234, "testPrimitiveLong1");
    TEST_ASSERT(RaftJson::getLongIm("1234", nullptr, -1000000) == -1000000, "testPrimitiveLong2");
    TEST_ASSERT(RaftJson::getLongIm("1234", "abc", -1000000) == -1000000, "testPrimitiveLong3");
    TEST_ASSERT(RaftJson::getLongIm("1234", "abc", 0) == 0, "testPrimitiveLong4");
    TEST_ASSERT(RaftJson::getLongIm("null", "", -1000000) == -1000000, "testPrimitiveLong5");
    TEST_ASSERT(RaftJson::getLongIm("null", nullptr, -1000000) == -1000000, "testPrimitiveLong6");
    TEST_ASSERT(RaftJson::getLongIm("null", "abc", -1000000) == -1000000, "testPrimitiveLong7");
    TEST_ASSERT(RaftJson::getLongIm("null", "abc", 0) == 0, "testPrimitiveLong8");
    TEST_ASSERT(RaftJson::getLongIm("\"true\"", "", 1234) == 0, "testPrimitiveBool13");
    TEST_ASSERT(RaftJson::getBoolIm("true", "", false) == true, "testPrimitiveBool1");
    TEST_ASSERT(RaftJson::getBoolIm("true", nullptr, false) == false, "testPrimitiveBool2");
    TEST_ASSERT(RaftJson::getBoolIm("true", "abc", false) == false, "testPrimitiveBool3");
    TEST_ASSERT(RaftJson::getBoolIm("true", "abc", true) == true, "testPrimitiveBool4");
    TEST_ASSERT(RaftJson::getBoolIm("false", "", true) == false, "testPrimitiveBool5");
    TEST_ASSERT(RaftJson::getBoolIm("false", nullptr, true) == true, "testPrimitiveBool6");
    TEST_ASSERT(RaftJson::getBoolIm("false", "abc", true) == true, "testPrimitiveBool7");
    TEST_ASSERT(RaftJson::getBoolIm("false", "abc", false) == false, "testPrimitiveBool8");
    TEST_ASSERT(RaftJson::getBoolIm("null", "", true) == true, "testPrimitiveBool9");
    TEST_ASSERT(RaftJson::getBoolIm("null", nullptr, true) == true, "testPrimitiveBool10");
    TEST_ASSERT(RaftJson::getBoolIm("null", "abc", true) == true, "testPrimitiveBool11");
    TEST_ASSERT(RaftJson::getBoolIm("null", "abc", false) == false, "testPrimitiveBool12");

    // Check failCount
    if (failCount > 0)
        printf("testPrimitives FAILED %d tests\n", failCount);
    else
        printf("testPrimitives all tests passed\n");

}
