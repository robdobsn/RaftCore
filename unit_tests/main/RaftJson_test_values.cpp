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

static const char* MODULE_PREFIX = "RaftJsonValuesTest";

static const char* DEFAULT_STRING_VALUE = "<<<DEFAULT_STRING_VALUE>>>";
static bool testGetString(const char* pSourceStr, const char* pDataPath, const char* expStr)
{
    String val = RaftJson::getStringIm(pSourceStr, pSourceStr+strlen(pSourceStr), pDataPath, DEFAULT_STRING_VALUE);
    String expectedStr(expStr);
    expectedStr.trim();
    // LOG_I(MODULE_PREFIX, "testGetString dataPath %s val %s", pDataPath, val.c_str());
    if (!val.equals(expStr))
    {
        LOG_I(MODULE_PREFIX, "testGetString failed expected %s != %s", expStr, val.c_str());
        return false;
    }
    return true;
}

static bool testGetArrayElems(const char* pSourceStr, const char* pDataPath, const char* expStrs[], int numStrs)
{
    std::vector<String> arrayElems;
    bool isValid = RaftJson::getArrayElemsIm(pSourceStr, pSourceStr+strlen(pSourceStr), pDataPath, arrayElems);
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
    std::vector<String> objectKeys;
    bool isValid = RaftJson::getKeysIm(pSourceStr, pSourceStr+strlen(pSourceStr), pDataPath, objectKeys);
    // LOG_I(MODULE_PREFIX, "testGetObjectKeys pDataPath %s got len %d", pDataPath, objectKeys.size());
    if (!isValid)
    {
        LOG_W(MODULE_PREFIX, "testGetObjectKeys failed");
        return false;
    }
    if (numStrs != objectKeys.size())
    {
        LOG_W(MODULE_PREFIX, "testGetObjectKeys failed expected len %d != %d (dataPath %s)", numStrs, objectKeys.size(), pDataPath);
        return false;
    }
    for (int i = 0; i < numStrs; i++)
    {
        if (!objectKeys[i].equals(expStrs[i]))
        {
            LOG_W(MODULE_PREFIX, "testGetObjectKeys failed idx %d expected %s != %s", i, expStrs[i], objectKeys[i].c_str());
            return false;
        }
    }
    return true;
}

static bool testObjectType(const char* pSourceStr, const char* pDataPath, RaftJson::RaftJsonType expType, int expArrayLen)
{
    int arrayLen = 0;
    RaftJson::RaftJsonType objType = RaftJson::getTypeIm(pSourceStr, pSourceStr+strlen(pSourceStr), pDataPath, arrayLen);
    // LOG_I(MODULE_PREFIX, "testObjectType pDataPath %s got type %s arrayLen %d", pDataPath, RaftJson::getElemTypeStr(objType), arrayLen);
    if (objType != expType)
    {
        LOG_W(MODULE_PREFIX, "testObjectType failed expected type %d != %d", expType, objType);
        return false;
    }
    if ((objType == RaftJson::RAFT_JSON_ARRAY) && (expArrayLen != arrayLen))
    {
        LOG_W(MODULE_PREFIX, "testObjectType failed expected arrayLen %d != %d", expArrayLen, arrayLen);
        return false;
    }
    return true;
}

TEST_CASE("test_raftjson_values", "[raftjson_values]")
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


    // Test the getString function
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
        String keyStartStr = "testGetString testkeyIdx=" + String(testIdx);
        TEST_ASSERT_MESSAGE(true == testGetString(testJSON, findKeyTests[testIdx].dataPath, 
                    findKeyTests[testIdx].expStr), keyStartStr.c_str());
    }

    // Test higher level methods
    TEST_ASSERT_MESSAGE(4.0 == RaftJson::getDoubleIm(testJSON, testJSON+strlen(testJSON),"consts/oxis/coo[3]/minotaur/[2]", 0), "getDouble1");
    TEST_ASSERT_MESSAGE(true == testGetString(testJSON, "consts/lastly", "elephant"), "getString1");
    TEST_ASSERT_MESSAGE(5 == RaftJson::getLongIm(testJSON, testJSON+strlen(testJSON),"consts/comarr/[1]", -1), "getLong1");
    TEST_ASSERT_MESSAGE(0 == RaftJson::getLongIm(testJSON, testJSON+strlen(testJSON),"consts/bool1", -1), "getLongBool1");
    TEST_ASSERT_MESSAGE(1 == RaftJson::getLongIm(testJSON, testJSON+strlen(testJSON),"consts/bool2", -1), "getLongBool2");

    // Test array elements
    const char* expectedStrs[] = {"6", "5", "4", "3", "3", "{\"fish\": \"stew\"}"};
    TEST_ASSERT_MESSAGE(true == testGetArrayElems(testJSON, "consts/comarr", expectedStrs, sizeof(expectedStrs)/sizeof(expectedStrs[0])), "getArrayElems1");

    // Test object keys
    const char* expectedKeys[] = {"axis", "oxis", "exis", "comarr", "lastly", "bool1", "bool2"};
    TEST_ASSERT_MESSAGE(true == testGetObjectKeys(testJSON, "consts", expectedKeys, sizeof(expectedKeys)/sizeof(expectedKeys[0])), "getKeys1");

    // Test object types
    TEST_ASSERT_MESSAGE(true == testObjectType(testJSON, "consts/axis", RaftJson::RAFT_JSON_STRING, 0), "getType1");
    TEST_ASSERT_MESSAGE(true == testObjectType(testJSON, "consts/oxis", RaftJson::RAFT_JSON_OBJECT, 0), "getType2");
    TEST_ASSERT_MESSAGE(true == testObjectType(testJSON, "consts/oxis/coo", RaftJson::RAFT_JSON_ARRAY, 4), "getType3");
    TEST_ASSERT_MESSAGE(true == testObjectType(testJSON, "consts/oxis/coo[3]", RaftJson::RAFT_JSON_OBJECT, 0), "getType4");
    TEST_ASSERT_MESSAGE(true == testObjectType(testJSON, "consts/oxis/coo[3]/minotaur", RaftJson::RAFT_JSON_ARRAY, 3), "getType5");
    TEST_ASSERT_MESSAGE(true == testObjectType(testJSON, "consts/oxis/coo[3]/combine", RaftJson::RAFT_JSON_STRING, 0), "getType6");
    TEST_ASSERT_MESSAGE(true == testObjectType(testJSON, "consts/oxis/coo[3]/slippery", RaftJson::RAFT_JSON_OBJECT, 0), "getType7");
    TEST_ASSERT_MESSAGE(true == testObjectType(testJSON, "consts/oxis/coo[3]/slippery/nice", RaftJson::RAFT_JSON_OBJECT, 0), "getType8");
    TEST_ASSERT_MESSAGE(true == testObjectType(testJSON, "consts/oxis/coo[3]/slippery/nice/animal", RaftJson::RAFT_JSON_UNDEFINED, 0), "getType9");
    TEST_ASSERT_MESSAGE(true == testObjectType(testJSON, "consts/oxis/coo[3]/slippery/polish", RaftJson::RAFT_JSON_STRING, 0), "getType10");
    TEST_ASSERT_MESSAGE(true == testObjectType(testJSON, "consts/oxis/coo[3]/foo", RaftJson::RAFT_JSON_STRING, 0), "getType11");
    TEST_ASSERT_MESSAGE(true == testObjectType(testJSON, "consts/exis", RaftJson::RAFT_JSON_STRING, 0), "getType12");
    TEST_ASSERT_MESSAGE(true == testObjectType(testJSON, "consts/comarr", RaftJson::RAFT_JSON_ARRAY, 6), "getType13");
    TEST_ASSERT_MESSAGE(true == testObjectType(testJSON, "consts/comarr/[0]", RaftJson::RAFT_JSON_NUMBER, 0), "getType14");
    TEST_ASSERT_MESSAGE(true == testObjectType(testJSON, "consts/comarr/[5]", RaftJson::RAFT_JSON_OBJECT, 0), "getType15");
    TEST_ASSERT_MESSAGE(true == testObjectType(testJSON, "consts/comarr/[5]/fish", RaftJson::RAFT_JSON_STRING, 0), "getType16");
    TEST_ASSERT_MESSAGE(true == testObjectType(testJSON, "consts/lastly", RaftJson::RAFT_JSON_STRING, 0), "getType17");
    TEST_ASSERT_MESSAGE(true == testObjectType(testJSON, "consts/bool1", RaftJson::RAFT_JSON_BOOLEAN, 0), "getType18");
    TEST_ASSERT_MESSAGE(true == testObjectType(testJSON, "consts/bool2", RaftJson::RAFT_JSON_BOOLEAN, 0), "getType19");

    // Test get non-existent strings
    TEST_ASSERT_MESSAGE(true == testGetString(testJSON, "consts/oxis/coo[3]/slippery/nice/animal", DEFAULT_STRING_VALUE), "getString2");
    TEST_ASSERT_MESSAGE(1234.567 == RaftJson::getDoubleIm(testJSON, testJSON+strlen(testJSON),"consts/oxis/coo[3]/slippery/nice/animal", 1234.567), "getDouble2");
    TEST_ASSERT_MESSAGE(1234 == RaftJson::getLongIm(testJSON, testJSON+strlen(testJSON),"consts/oxis/coo[3]/slippery/nice/animal", 1234), "getLong2");

    // Test documents containing only primitives
    const char* test1234 = "1234";
    const char* test1234End = test1234 + strlen(test1234);
    const char* test1234pt567 = "1234.567";
    const char* test1234pt567End = test1234pt567 + strlen(test1234pt567);
    const char* testtrue = "true";
    const char* testtrueEnd = testtrue + strlen(testtrue);
    const char* testfalse = "false";
    const char* testfalseEnd = testfalse + strlen(testfalse);
    const char* testnull = "null";
    const char* testnullEnd = testnull + strlen(testnull);
    const char* test1234quotes = "\"1234\"";
    const char* test1234quotesEnd = test1234quotes + strlen(test1234quotes);
    const char* test1234pt567quotes = "\"1234.567\"";
    const char* test1234pt567quotesEnd = test1234pt567quotes + strlen(test1234pt567quotes);
    const char* testtruequotes = "\"true\"";
    const char* testtruequotesEnd = testtruequotes + strlen(testtruequotes);
    const char* testfalsequotes = "\"false\"";
    const char* testfalsequotesEnd = testfalsequotes + strlen(testfalsequotes);
    const char* testnullquotes = "\"null\"";
    const char* testnullquotesEnd = testnullquotes + strlen(testnullquotes);
    TEST_ASSERT_MESSAGE(true == testGetString(test1234, "", test1234), "getString3");
    TEST_ASSERT_MESSAGE(1234.0 == RaftJson::getDoubleIm(test1234, test1234End, "", 1234.0), "getDouble3");
    TEST_ASSERT_MESSAGE(1234 == RaftJson::getLongIm(test1234, test1234End, "", 1234), "getLong3");
    TEST_ASSERT_MESSAGE(true == testGetString(test1234pt567, "", "1234.567"), "getString4");
    TEST_ASSERT_MESSAGE(1234.567 == RaftJson::getDoubleIm(test1234pt567, test1234pt567End, "", 1234.567), "getDouble4");
    TEST_ASSERT_MESSAGE(1234 == RaftJson::getLongIm(test1234pt567, test1234pt567End, "", 1234), "getLong4");
    TEST_ASSERT_MESSAGE(true == testGetString(testtrue, "", testtrue), "getString5");
    TEST_ASSERT_MESSAGE(1 == RaftJson::getLongIm(testtrue, testtrueEnd, "", 1234), "getLong5");
    TEST_ASSERT_MESSAGE(true == testGetString(testfalse, "", "false"), "getString6");
    TEST_ASSERT_MESSAGE(0 == RaftJson::getLongIm(testfalse, testfalseEnd, "", 1234), "getLong6");
    TEST_ASSERT_MESSAGE(true == testGetString(testnull, "", "null"), "getString7");
    TEST_ASSERT_MESSAGE(1234.567 == RaftJson::getDoubleIm(testnull, testnullEnd, "", 1234.567), "getDouble7");
    TEST_ASSERT_MESSAGE(1234 == RaftJson::getLongIm(testnull, testnullEnd, "", 1234), "getLong7");
    TEST_ASSERT_MESSAGE(true == testGetString(test1234quotes, "", "1234"), "getString8");
    TEST_ASSERT_MESSAGE(1234.0 == RaftJson::getDoubleIm(test1234quotes, test1234quotesEnd, "", 1234.0), "getDouble8");
    TEST_ASSERT_MESSAGE(1234 == RaftJson::getLongIm(test1234quotes, test1234quotesEnd, "", 1234), "getLong8");
    TEST_ASSERT_MESSAGE(true == testGetString(test1234pt567quotes, "", "1234.567"), "getString9");
    TEST_ASSERT_MESSAGE(1234.567 == RaftJson::getDoubleIm(test1234pt567quotes, test1234pt567quotesEnd, "", 1234.567), "getDouble9");
    TEST_ASSERT_MESSAGE(1234 == RaftJson::getLongIm(test1234pt567quotes, test1234pt567quotesEnd, "", 1234), "getLong9");
    TEST_ASSERT_MESSAGE(true == testGetString(testtruequotes, "", "true"), "getString10");
    TEST_ASSERT_MESSAGE(0 == RaftJson::getLongIm(testtruequotes, testtruequotesEnd, "", 1234), "getLong10");
    TEST_ASSERT_MESSAGE(true == testGetString(testfalsequotes, "", "false"), "getString11");
    TEST_ASSERT_MESSAGE(0 == RaftJson::getLongIm(testfalsequotes, testfalsequotesEnd, "", 1234), "getLong11");
    TEST_ASSERT_MESSAGE(true == testGetString(testnullquotes, "", "null"), "getString12");
}
