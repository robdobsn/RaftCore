/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Unit tests of RaftJson - wrapper for jsmnr
//
// Rob Dobson 2017-2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "unity_test_runner.h"
#include "RaftArduino.h"
#include "RaftJson_jsmn.h"
#include "Logger.h"
#include "RaftUtils.h"

static const char* MODULE_PREFIX = "RaftJsonJSMNUnitTest";

static bool testFindElemEnd(jsmntok_t* pTokens, int numTokens, int tokenIdx, int expEndPos, const char* pSourceStr)
{
    
    // Find element end
    int endPos = RaftJson_jsmn::findElemEnd(pSourceStr, pTokens, numTokens, tokenIdx);
    // LOG_I(MODULE_PREFIX, "testFindElemEnd startTok %d endTok %d", tokenIdx, endPos);
    if (expEndPos != endPos)
    {
        LOG_W(MODULE_PREFIX, "testFindElemEnd endPos expected %d != %d, tokenIdx %d", expEndPos, endPos, tokenIdx);
        return false;
    }
    return true;
}

static bool testFindKeyInJson(jsmntok_t* pTokens, int numTokens, 
                const char* pDataPath, const char* expStr, const char* pSourceStr)
{
    // find key
    int endTokenIdx = 0;
    jsmntype_t keyType = JSMN_UNDEFINED;
    int foundTokenIdx = RaftJson_jsmn::findKeyInJson(pSourceStr, pDataPath, "", pTokens, numTokens, endTokenIdx, keyType);
    String elemStr;
    if (foundTokenIdx >= 0)
        elemStr = String((uint8_t*)pSourceStr + pTokens[foundTokenIdx].start, 
                pTokens[foundTokenIdx].end - pTokens[foundTokenIdx].start
                // ((endTokenIdx < numTokens) && (endTokenIdx >= 0)) ? pTokens[foundTokenIdx].end - pTokens[foundTokenIdx].start : strlen(pSourceStr) - pTokens[foundTokenIdx].start,
                );
    // LOG_I(MODULE_PREFIX, "testFindKeyInJson pDataPath %s elemStr %s startTok %d endTok %d", pDataPath, elemStr.c_str(), 
    //                         foundTokenIdx, endTokenIdx);
    if (!elemStr.equals(expStr))
    {
        LOG_W(MODULE_PREFIX, "testFindKeyInJson failed expected %s != %s", expStr, elemStr.c_str());
        return false;
    }
    return true;
}

static bool testGetString(const char* pSourceStr, const char* pDataPath, const char* expStr)
{
    String val = RaftJson_jsmn::getString(pSourceStr, pDataPath, "");
    // LOG_I(MODULE_PREFIX, "testGetString dataPath %s val %s", pDataPath, val.c_str());
    if (!val.equals(expStr))
    {
        LOG_W(MODULE_PREFIX, "testGetString failed expected %s != %s", expStr, val.c_str());
        return false;
    }
    return true;
}

static bool testGetArrayElems(const char* pSourceStr, const char* pDataPath, const char* expStrs[], int numStrs)
{
    std::vector<String> arrayElems;
    bool isValid = RaftJson_jsmn::getArrayElems(pSourceStr, pDataPath, arrayElems);
    // LOG_I(MODULE_PREFIX, "testGetArrayElems pDataPath %s got len %d", pDataPath, arrayElems.size());
    if (!isValid)
    {
        LOG_W(MODULE_PREFIX, "testGetArrayElems failed");
        return false;
    }
    if (numStrs != arrayElems.size())
    {
        LOG_W(MODULE_PREFIX, "testGetArrayElems failed expected len %d != %d", numStrs, arrayElems.size());
        return false;
    }
    for (int i = 0; i < numStrs; i++)
    {
        if (!arrayElems[i].equals(expStrs[i]))
        {
            LOG_W(MODULE_PREFIX, "testGetArrayElems failed idx %d expected %s != %s", i, expStrs[i], arrayElems[i].c_str());
            return false;
        }
    }
    return true;
}

static bool testGetObjectKeys(const char* pSourceStr, const char* pDataPath, const char* expStrs[], int numStrs)
{
    std::vector<String> arrayElems;
    bool isValid = RaftJson_jsmn::getKeys(pSourceStr, pDataPath, arrayElems);
    // LOG_I(MODULE_PREFIX, "testGetObjectKeys pDataPath %s got len %d", pDataPath, arrayElems.size());
    if (!isValid)
    {
        LOG_W(MODULE_PREFIX, "testGetObjectKeys failed");
        return false;
    }
    if (numStrs != arrayElems.size())
    {
        LOG_W(MODULE_PREFIX, "testGetObjectKeys failed expected len %d != %d (dataPath %s)", numStrs, arrayElems.size(), pDataPath);
        return false;
    }
    for (int i = 0; i < numStrs; i++)
    {
        if (!arrayElems[i].equals(expStrs[i]))
        {
            LOG_W(MODULE_PREFIX, "testGetObjectKeys failed idx %d expected %s != %s", i, expStrs[i], arrayElems[i].c_str());
            return false;
        }
    }
    return true;
}

TEST_CASE("test_raftjson_jsmn", "[raftjsonjsmn]")
{
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
            R"( 	"lastly": "elephant",               )"
            R"( 	"bool1":false,                      )"
            R"( 	"bool2": 	  true                  )"
            R"( }                                       )"
            R"(}                                        )";

    // Test primitives

    // Parse json into tokens
    int numTokens = 0;
    jsmntok_t *pTokens = RaftJson_jsmn::parseJson(testJSON, numTokens);
    if (pTokens == NULL)
    {
        LOG_I(MODULE_PREFIX, "testFindElemEnd parseJson failed");
        return;
    }

    // Debug
    // RaftJson_jsmn::debugDumpParseResult(testJSON, pTokens, numTokens);

    // Test the findElemEnd function
    int endTokens[] = {48,2,48,4,5,
                    6,30,8,30,10,
                    11,12,30,14,18,
                    16,17,18,19,20,
                    21,28,23,24,25,
                    26,27,28,29,30,
                    31,32,33,42,35,
                    36,37,38,39,42,
                    41,42,43,44};
    for (int tokStart = 0; tokStart < sizeof(endTokens)/sizeof(int); tokStart++)
    {
        String tokStartStr = "testFindElem start=" + String(tokStart);
        TEST_ASSERT_MESSAGE(true == testFindElemEnd(pTokens, numTokens, tokStart, endTokens[tokStart], testJSON), tokStartStr.c_str());
    }

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
        { "consts/bool1", "false" },
        { "consts/bool2", "true" },
    };
    for (int testIdx = 0; testIdx < sizeof(findKeyTests)/sizeof(findKeyTests[0]); testIdx++)
    {
        String tokStartStr = "testFindElem testkeyIdx=" + String(testIdx);
        TEST_ASSERT_MESSAGE(true == testFindKeyInJson(pTokens, numTokens, findKeyTests[testIdx].dataPath, 
                    findKeyTests[testIdx].expStr, testJSON), tokStartStr.c_str());
    }

    // Cleanup
    delete[] pTokens;

    // Test higher level methods
    TEST_ASSERT_MESSAGE(true == testGetString(testJSON, "consts/oxis/coo[3]/minotaur", "[1, 3, 4]"), "getString1");
    TEST_ASSERT_MESSAGE(true == testGetString(testJSON, "consts/lastly", "elephant"), "getString2");
    TEST_ASSERT_MESSAGE(5 == RaftJson_jsmn::getLong(testJSON, "consts/comarr/[1]", -1), "getLong1");
    TEST_ASSERT_MESSAGE(0 == RaftJson_jsmn::getLong(testJSON, "consts/bool1", -1), "getLongBool1");
    TEST_ASSERT_MESSAGE(1 == RaftJson_jsmn::getLong(testJSON, "consts/bool2", -1), "getLongBool2");

    // Test array elements
    const char* expectedStrs[] = {"6", "5", "4", "3", "3", "{\"fish\": \"stew\"}"};
    TEST_ASSERT_MESSAGE(true == testGetArrayElems(testJSON, "consts/comarr", expectedStrs, sizeof(expectedStrs)/sizeof(expectedStrs[0])), "getArrayElems1");

    // Test object keys
    const char* expectedKeys[] = {"axis", "oxis", "exis", "comarr", "lastly", "bool1", "bool2"};
    TEST_ASSERT_MESSAGE(true == testGetObjectKeys(testJSON, "consts", expectedKeys, sizeof(expectedKeys)/sizeof(expectedKeys[0])), "getKeys1");

}
