#include <stdio.h>
#include "testhdr.h"
#include "ArduinoWString.h"
#include "RaftJson.h"
#include "RaftUtils.h"
#include "PlatformUtils.h"

#include "MsgExchangeHookTest.h"

#include "JSON_test_data_large.h"
#include "JSON_test_data_small.h"

#define TEST_ASSERT(cond, msg) if (!(cond)) { printf("TEST_ASSERT failed %s\n", msg); failCount++; }

// Unit test for parseIntList function
void testParseIntList()
{
    printf("Running testParseIntList...\n");

    struct TestCase
    {
        const char* inputStr;
        const char* listSep;
        const char* rangeSep;
        uint32_t maxNum;
        std::vector<int> expectedOutput;
        bool expectedResult;
    };

    std::vector<TestCase> testCases = {
        // Basic tests
        {"1,2,3,4", ",", "-", 10, {1, 2, 3, 4}, true},
        {"1-3,5", ",", "-", 10, {1, 2, 3, 5}, true},

        // Test with maxNum
        {"1,2,3,4,5", ",", "-", 3, {1, 2, 3}, false},
        {"1-5", ",", "-", 3, {1, 2, 3}, false},

        // Custom separators
        {"1;2;3-5;6", ";", "-", 10, {1, 2, 3, 4, 5, 6}, true},
        {"1to3;4", ";", "to", 10, {1, 2, 3, 4}, true},

        // Empty input
        {"", ",", "-", 10, {}, true},

        // Invalid inputs
        // {"1-5-7,8", ",", "-", 10, {}, true}, // Malformed range
        // {"abc,2-4", ",", "-", 10, {2, 3, 4}, true}, // Invalid token
    };

    int failCount = 0;

    for (size_t i = 0; i < testCases.size(); ++i)
    {
        const TestCase& testCase = testCases[i];
        std::vector<int> output;
        bool result = Raft::parseIntList(testCase.inputStr, output, testCase.listSep, testCase.rangeSep, testCase.maxNum);

        if (result != testCase.expectedResult)
        {
            printf("Test %zu failed: Expected result %d, got %d\n", i, testCase.expectedResult, result);
            ++failCount;
            continue;
        }

        if (output != testCase.expectedOutput)
        {
            printf("Test %zu failed: Expected output ", i);
            for (int val : testCase.expectedOutput)
                printf("%d ", val);
            printf(", got ");
            for (int val : output)
                printf("%d ", val);
            printf("\n");
            ++failCount;
        }
    }

    if (failCount == 0)
    {
        printf("testParseIntList all tests passed\n");
    }
    else
    {
        printf("testParseIntList FAILED %d tests\n", failCount);
    }
}

int main()
{
    testParseIntList();

    int constsAxis = RaftJson::getLongIm(JSON_test_data_small, JSON_test_data_small+strlen(JSON_test_data_small), "consts/axis", 0);
    int minotaur = RaftJson::getLongIm(JSON_test_data_small, JSON_test_data_small+strlen(JSON_test_data_small), "consts/oxis/coo[3]/minotaur[2]", 0);
    int comarr = RaftJson::getLongIm(JSON_test_data_small, JSON_test_data_small+strlen(JSON_test_data_small), "consts/comarr[4]", 0);

    int maxQ = RaftJson::getLongIm(JSON_test_data_large, JSON_test_data_large+strlen(JSON_test_data_large), "[0]/Robot/WorkMgr/WorkQ/maxLen[0]/__value__", 0);

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
        String val = RaftJson::getStringIm(testJSON, testJSON+strlen(testJSON), findKeyTests[testIdx].dataPath, "");
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
        int val = RaftJson::getLongIm(testTinyJson, testTinyJson+strlen(testTinyJson),
                     testTinyExpectations[testIdx].dataPath, MAX_RPM_DEFAULT_VALUE);
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
    const char* testStr1234 = "1234";
    const char* testStrNull = "null";
    const char* testStrTrueInQuotes = "\"true\"";
    const char* testStrTrue = "true";
    const char* testStrFalse = "false";
    TEST_ASSERT(RaftJson::getStringIm(testStr1234, testStr1234+strlen(testStr1234), "", "<<<>>>") == "1234", "testPrimitiveStr1");
    TEST_ASSERT(RaftJson::getStringIm(testStr1234, testStr1234+strlen(testStr1234), nullptr, "<<<>>>") == "<<<>>>", "testPrimitiveStr2");
    TEST_ASSERT(RaftJson::getStringIm(testStr1234, testStr1234+strlen(testStr1234), "abc", "<<<>>>") == "<<<>>>", "testPrimitiveStr3");
    TEST_ASSERT(RaftJson::getStringIm(testStr1234, testStr1234+strlen(testStr1234), "abc", "") == "", "testPrimitiveStr5");
    TEST_ASSERT(RaftJson::getStringIm(testStrNull, testStrNull+strlen(testStrNull), "", "<<<>>>") == "null", "testPrimitiveStr6");
    TEST_ASSERT(RaftJson::getStringIm(testStrNull, testStrNull+strlen(testStrNull), nullptr, "<<<>>>") == "<<<>>>", "testPrimitiveStr7");
    TEST_ASSERT(RaftJson::getStringIm(testStrNull, testStrNull+strlen(testStrNull), "abc", "<<<>>>") == "<<<>>>", "testPrimitiveStr8");
    TEST_ASSERT(RaftJson::getStringIm(testStrNull, testStrNull+strlen(testStrNull), "abc", "") == "", "testPrimitiveStr9");
    TEST_ASSERT(RaftJson::getDoubleIm(testStr1234, testStr1234+strlen(testStr1234), "", -1000000) == 1234.0, "testPrimitiveDouble1");
    TEST_ASSERT(RaftJson::getDoubleIm(testStr1234, testStr1234+strlen(testStr1234), nullptr, -1000000) == -1000000, "testPrimitiveDouble2");
    TEST_ASSERT(RaftJson::getDoubleIm(testStr1234, testStr1234+strlen(testStr1234), "abc", -1000000) == -1000000, "testPrimitiveDouble3");
    TEST_ASSERT(RaftJson::getDoubleIm(testStr1234, testStr1234+strlen(testStr1234), "abc", 0) == 0, "testPrimitiveDouble4");
    TEST_ASSERT(RaftJson::getDoubleIm(testStrNull, testStrNull+strlen(testStrNull), "", -1000000) == -1000000, "testPrimitiveDouble5");
    TEST_ASSERT(RaftJson::getDoubleIm(testStrNull, testStrNull+strlen(testStrNull), nullptr, -1000000) == -1000000, "testPrimitiveDouble6");
    TEST_ASSERT(RaftJson::getDoubleIm(testStrNull, testStrNull+strlen(testStrNull), "abc", -1000000) == -1000000, "testPrimitiveDouble7");
    TEST_ASSERT(RaftJson::getDoubleIm(testStrNull, testStrNull+strlen(testStrNull), "abc", 0) == 0, "testPrimitiveDouble8");
    TEST_ASSERT(RaftJson::getLongIm(testStr1234, testStr1234+strlen(testStr1234), "", -1000000) == 1234, "testPrimitiveLong1");
    TEST_ASSERT(RaftJson::getLongIm(testStr1234, testStr1234+strlen(testStr1234), nullptr, -1000000) == -1000000, "testPrimitiveLong2");
    TEST_ASSERT(RaftJson::getLongIm(testStr1234, testStr1234+strlen(testStr1234), "abc", -1000000) == -1000000, "testPrimitiveLong3");
    TEST_ASSERT(RaftJson::getLongIm(testStr1234, testStr1234+strlen(testStr1234), "abc", 0) == 0, "testPrimitiveLong4");
    TEST_ASSERT(RaftJson::getLongIm(testStrNull, testStrNull+strlen(testStrNull), "", -1000000) == -1000000, "testPrimitiveLong5");
    TEST_ASSERT(RaftJson::getLongIm(testStrNull, testStrNull+strlen(testStrNull), nullptr, -1000000) == -1000000, "testPrimitiveLong6");
    TEST_ASSERT(RaftJson::getLongIm(testStrNull, testStrNull+strlen(testStrNull), "abc", -1000000) == -1000000, "testPrimitiveLong7");
    TEST_ASSERT(RaftJson::getLongIm(testStrNull, testStrNull+strlen(testStrNull), "abc", 0) == 0, "testPrimitiveLong8");
    TEST_ASSERT(RaftJson::getLongIm(testStrTrueInQuotes, testStrTrueInQuotes+strlen(testStrTrueInQuotes), "", 1234) == 0, "testPrimitiveBool13");
    TEST_ASSERT(RaftJson::getBoolIm(testStrTrue, testStrTrue+strlen(testStrTrue), "", false) == true, "testPrimitiveBool1");
    TEST_ASSERT(RaftJson::getBoolIm(testStrTrue, testStrTrue+strlen(testStrTrue), nullptr, false) == false, "testPrimitiveBool2");
    TEST_ASSERT(RaftJson::getBoolIm(testStrTrue, testStrTrue+strlen(testStrTrue), "abc", false) == false, "testPrimitiveBool3");
    TEST_ASSERT(RaftJson::getBoolIm(testStrTrue, testStrTrue+strlen(testStrTrue), "abc", true) == true, "testPrimitiveBool4");
    TEST_ASSERT(RaftJson::getBoolIm(testStrFalse, testStrFalse+strlen(testStrFalse), "", true) == false, "testPrimitiveBool5");
    TEST_ASSERT(RaftJson::getBoolIm(testStrFalse, testStrFalse+strlen(testStrFalse), nullptr, true) == true, "testPrimitiveBool6");
    TEST_ASSERT(RaftJson::getBoolIm(testStrFalse, testStrFalse+strlen(testStrFalse), "abc", true) == true, "testPrimitiveBool7");
    TEST_ASSERT(RaftJson::getBoolIm(testStrFalse, testStrFalse+strlen(testStrFalse), "abc", false) == false, "testPrimitiveBool8");
    TEST_ASSERT(RaftJson::getBoolIm(testStrNull, testStrNull+strlen(testStrNull), "", true) == true, "testPrimitiveBool9");
    TEST_ASSERT(RaftJson::getBoolIm(testStrNull, testStrNull+strlen(testStrNull), nullptr, true) == true, "testPrimitiveBool10");
    TEST_ASSERT(RaftJson::getBoolIm(testStrNull, testStrNull+strlen(testStrNull), "abc", true) == true, "testPrimitiveBool11");
    TEST_ASSERT(RaftJson::getBoolIm(testStrNull, testStrNull+strlen(testStrNull), "abc", false) == false, "testPrimitiveBool12");

    // Extract NV pair tests
    const char* testInput = R"(0x020701=&0x020801=&0x009600=&0x0097fd&0x00e301=&0x00e403=r1&0x00e502=&0x00e601=&0x00e703=0x123456&0x00f502=&0x00d905=&0x00dbce=&0x00dc03=&0x00ddf8=&0x009f00=&0x00a33c=&0x00b700=&0x00bb3c=&0x00b209=&0x00ca09=&0x019801=&0x01b017=&0x01ad00=&0x00ff05=r55;0x010005=&0x019905=&0x01a61b;0x01ac3e=&0x01a71f=&0x003000=;0x001110=&0x010a30=&0x003f46=&0x0031ff=&0x004163=&0x002e01=&0x001b09=&0x003e31=&0x001424=)";

    std::vector<RaftJson::NameValuePair> nvPairs;
    RaftJson::extractNameValues(testInput, "=", "&", ";", nvPairs);

    // printf("test_raftjson_nv_pairs numPairs %d\n", (int)nvPairs.size());
    // for (size_t i = 0; i < nvPairs.size(); i++)
    // {
    //     printf("test_raftjson_nv_pairs %d %s %s\n", (int)i, nvPairs[i].name.c_str(), nvPairs[i].value.c_str());
    // }
    TEST_ASSERT(nvPairs.size() == 39, "testNumNVPairs");
    TEST_ASSERT(nvPairs[4].name == "0x00e301", "testNVPair4Name");
    TEST_ASSERT(nvPairs[4].value == "", "testNVPair4Value");
    TEST_ASSERT(nvPairs[5].name == "0x00e403", "testNVPair5Name");
    TEST_ASSERT(nvPairs[5].value == "r1", "testNVPair5Value");
    TEST_ASSERT(nvPairs[6].name == "0x00e502", "testNVPair6Name");
    TEST_ASSERT(nvPairs[6].value == "", "testNVPair6Value");
    TEST_ASSERT(nvPairs[7].name == "0x00e601", "testNVPair7Name");
    TEST_ASSERT(nvPairs[7].value == "", "testNVPair7Value");
    TEST_ASSERT(nvPairs[8].name == "0x00e703", "testNVPair8Name");
    TEST_ASSERT(nvPairs[8].value == "0x123456", "testNVPair8Value");
    TEST_ASSERT(nvPairs[9].name == "0x00f502", "testNVPair9Name");
    TEST_ASSERT(nvPairs[9].value == "", "testNVPair9Value");
    TEST_ASSERT(nvPairs[10].name == "0x00d905", "testNVPair10Name");
    TEST_ASSERT(nvPairs[10].value == "", "testNVPair10Value");
    TEST_ASSERT(nvPairs[22].name == "0x01ad00", "testNVPair22Name");
    TEST_ASSERT(nvPairs[22].value == "", "testNVPair22Value");
    TEST_ASSERT(nvPairs[23].name == "0x00ff05", "testNVPair23Name");
    TEST_ASSERT(nvPairs[23].value == "r55", "testNVPair23Value");
    TEST_ASSERT(nvPairs[24].name == "0x010005", "testNVPair24Name");
    TEST_ASSERT(nvPairs[24].value == "", "testNVPair24Value");

    // Test array access
    const RaftJson testRaftJsonArray(testJSON);
    std::vector<String> cooArrayElems;
    bool getCooRslt = testRaftJsonArray.getArrayElems("consts/oxis/coo", cooArrayElems);
    TEST_ASSERT(getCooRslt, "testRaftJsonArray1");
    TEST_ASSERT(cooArrayElems.size() == 4, "testRaftJsonArray2");
    TEST_ASSERT(cooArrayElems[0] == "pig", "testRaftJsonArray3");
    TEST_ASSERT(cooArrayElems[2] == "dog", "testRaftJsonArray4");

    // Test array iterators
    const char* testArrayJSON = R"({"testArray":[1, 2, 3, 4, 5, 6, 7, 8, 9, 10]})";
    RaftJson testArrayJson1(testArrayJSON);
    int idx = 1;
    for (auto myTestVar : testArrayJson1.getArray("testArray"))
    {
        TEST_ASSERT(myTestVar.toInt() == idx, "testArrayJSON1");
        // printf("testArrayJSON %d\n", myTestVar.toInt());
        idx++;
    }
    RaftJson testArrayJson2(R"({"testArray":["a", "bb", "ccc", "dd\\dd\ndd\n\n", "eeeee", "ffffff", "", "bananas", "{}", {"a": 1, "b": 2, "c": 3}]})");
    idx = 1;
    for (auto myTestVar : testArrayJson2.getArray("testArray"))
    {
        switch(idx)
        {
            case 1: TEST_ASSERT(myTestVar.toString() == "a", "testArrayJSON2_1"); break;
            case 2: TEST_ASSERT(myTestVar.toString() == "bb", "testArrayJSON2_2"); break;
            case 3: TEST_ASSERT(myTestVar.toString() == "ccc", "testArrayJSON2_3"); break;
            case 4: TEST_ASSERT(myTestVar.toString() == "dd\\dd\ndd\n\n", "testArrayJSON2_4"); break;
            case 5: TEST_ASSERT(myTestVar.toString() == "eeeee", "testArrayJSON2_5"); break;
            case 6: TEST_ASSERT(myTestVar.toString() == "ffffff", "testArrayJSON2_6"); break;
            case 7: TEST_ASSERT(myTestVar.toString() == "", "testArrayJSON2_7"); break;
            case 8: TEST_ASSERT(myTestVar.toString() == "bananas", "testArrayJSON2_8"); break;
            case 9: TEST_ASSERT(myTestVar.toString() == "{}", "testArrayJSON2_9"); break;
            case 10: TEST_ASSERT(myTestVar.toString() == "{\"a\": 1, \"b\": 2, \"c\": 3}", "testArrayJSON2_10"); break;
        }
        idx++;
        // printf("testArrayJSON %s\n", myTestVar.toString().c_str());
    }

    // Test array size
    TEST_ASSERT(testArrayJson1.getArray("testArray").size() == 10, "testArrayJSON1_1");
    TEST_ASSERT(testArrayJson2.getArray("testArray").size() == 10, "testArrayJSON2_1");
    
    // Test array access
    TEST_ASSERT(testArrayJson1.getArray("testArray")[4].toInt() == 5, "testArrayJSON1_0");
    TEST_ASSERT(testArrayJson2.getArray("testArray")[4].toString() == "eeeee", "testArrayJSON2_0");

    // Test object iterator
    const char* testObjectJSON = R"({"testObject":{"a": 1, "b": "hello", "c": {"minky":"monk","dinky":"donk"}, "d": 1234, "e": [1,2,3,4,5,6], "f": 6, "g": 7, "h": 8, "i": 9}})";
    RaftJson testObjectJson(testObjectJSON);
    idx = 1;
    for (auto myTestVar : testObjectJson.getObject("testObject"))
    {
        switch(idx)
        {
            case 1: TEST_ASSERT(myTestVar.first == "a", "testObjectJSON1_1"); TEST_ASSERT(myTestVar.second.toInt() == 1, "testObjectJSON1_2"); break;
            case 2: TEST_ASSERT(myTestVar.first == "b", "testObjectJSON1_3"); TEST_ASSERT(myTestVar.second.toString() == "hello", "testObjectJSON1_4"); break;
            case 3: TEST_ASSERT(myTestVar.first == "c", "testObjectJSON1_5"); TEST_ASSERT(myTestVar.second.toString() == "{\"minky\":\"monk\",\"dinky\":\"donk\"}", "testObjectJSON1_6"); break;
            case 4: TEST_ASSERT(myTestVar.first == "d", "testObjectJSON1_7"); TEST_ASSERT(myTestVar.second.toInt() == 1234, "testObjectJSON1_8"); break;
            case 5: TEST_ASSERT(myTestVar.first == "e", "testObjectJSON1_9"); TEST_ASSERT(myTestVar.second.toString() == "[1,2,3,4,5,6]", "testObjectJSON1_10"); break;
            case 6: TEST_ASSERT(myTestVar.first == "f", "testObjectJSON1_11"); TEST_ASSERT(myTestVar.second.toInt() == 6, "testObjectJSON1_12"); break;
            case 7: TEST_ASSERT(myTestVar.first == "g", "testObjectJSON1_13"); TEST_ASSERT(myTestVar.second.toInt() == 7, "testObjectJSON1_14"); break;
            case 8: TEST_ASSERT(myTestVar.first == "h", "testObjectJSON1_15"); TEST_ASSERT(myTestVar.second.toInt() == 8, "testObjectJSON1_16"); break;
            case 9: TEST_ASSERT(myTestVar.first == "i", "testObjectJSON1_17"); TEST_ASSERT(myTestVar.second.toInt() == 9, "testObjectJSON1_18"); break;
        }
        idx++;
        // printf("testObjectJSON %s: %s\n", myTestVar.first.c_str(), myTestVar.second.toString().c_str());
    }

    // Test message handler
    MsgExchangeHookTest msgExchangeHookTest;
    msgExchangeHookTest.loop();

    // Check failCount
    if (failCount > 0)
        printf("testPrimitives FAILED %d tests\n", failCount);
    else
        printf("testPrimitives all tests passed\n");
}
