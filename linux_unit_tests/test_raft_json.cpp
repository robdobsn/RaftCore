/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftJson tests
//
// Path lookup, numeric/bool/string conversion, name-value pair extraction, array and object iteration,
// field appending and escaped-quote handling.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include "testhdr.h"
#include "ArduinoWString.h"
#include "RaftJson.h"

#include "JSON_test_data_large.h"
#include "JSON_test_data_small.h"

static const char* testJSON =
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

// Values from the test data documents (checked independently against a standard JSON parser)
static void testDataDocuments()
{
    printf("Running testDataDocuments...\n");
    const char* small = JSON_test_data_small;
    const char* smallEnd = small + strlen(small);
    const char* large = JSON_test_data_large;
    const char* largeEnd = large + strlen(large);
    TEST_ASSERT(RaftJson::getLongIm(small, smallEnd, "consts/axis", 0) == 1, "dataSmallConstsAxis");
    TEST_ASSERT(RaftJson::getLongIm(small, smallEnd, "consts/oxis/coo[3]/minotaur[2]", 0) == 4, "dataSmallMinotaur");
    TEST_ASSERT(RaftJson::getLongIm(small, smallEnd, "consts/comarr[4]", 0) == 3, "dataSmallComarr");
    TEST_ASSERT(RaftJson::getLongIm(large, largeEnd, "[0]/Robot/WorkMgr/WorkQ/maxLen[0]/__value__", 0) == 50, "dataLargeMaxQ");
}

static void testGetString()
{
    printf("Running testGetString...\n");
    struct TestElem
    {
        const char* dataPath;
        const char* expStr;
    };
    const TestElem tests[] = {
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
    for (const auto& test : tests)
    {
        String val = RaftJson::getStringIm(testJSON, testJSON + strlen(testJSON), test.dataPath, "");
        String expStr = String(test.expStr);
        expStr.trim();
        if (val != expStr)
            printf("  path %s got <<<%s>>> expected <<<%s>>>\n", test.dataPath, val.c_str(), expStr.c_str());
        TEST_ASSERT(val == expStr, test.dataPath);
    }
}

static void testMissingKeyDefault()
{
    printf("Running testMissingKeyDefault...\n");
    const char* tinyJson = R"({"unitsPerRot":360,"stepsPerRot":28000,"maxSpeed":10,"maxAcc":10})";
    const char* tinyEnd = tinyJson + strlen(tinyJson);
    TEST_ASSERT(RaftJson::getLongIm(tinyJson, tinyEnd, "maxRPM", -100000) == -100000, "tinyMissingKeyDefault");
    TEST_ASSERT(RaftJson::getLongIm(tinyJson, tinyEnd, "stepsPerRot", -100000) == 28000, "tinyPresentKey");
}

// A document that is just a primitive value
static void testPrimitives()
{
    printf("Running testPrimitives...\n");
    const char* str1234 = "1234";
    const char* strNull = "null";
    const char* strTrueInQuotes = "\"true\"";
    const char* strFalseInQuotes = "\"false\"";
    const char* strTrue = "true";
    const char* strFalse = "false";
    #define END(s) (s + strlen(s))
    TEST_ASSERT(RaftJson::getStringIm(str1234, END(str1234), "", "<<<>>>") == "1234", "primitiveStr1");
    TEST_ASSERT(RaftJson::getStringIm(str1234, END(str1234), nullptr, "<<<>>>") == "<<<>>>", "primitiveStr2");
    TEST_ASSERT(RaftJson::getStringIm(str1234, END(str1234), "abc", "<<<>>>") == "<<<>>>", "primitiveStr3");
    TEST_ASSERT(RaftJson::getStringIm(str1234, END(str1234), "abc", "") == "", "primitiveStr5");
    TEST_ASSERT(RaftJson::getStringIm(strNull, END(strNull), "", "<<<>>>") == "null", "primitiveStr6");
    TEST_ASSERT(RaftJson::getStringIm(strNull, END(strNull), nullptr, "<<<>>>") == "<<<>>>", "primitiveStr7");
    TEST_ASSERT(RaftJson::getStringIm(strNull, END(strNull), "abc", "<<<>>>") == "<<<>>>", "primitiveStr8");
    TEST_ASSERT(RaftJson::getStringIm(strNull, END(strNull), "abc", "") == "", "primitiveStr9");
    TEST_ASSERT(RaftJson::getDoubleIm(str1234, END(str1234), "", -1000000) == 1234.0, "primitiveDouble1");
    TEST_ASSERT(RaftJson::getDoubleIm(str1234, END(str1234), nullptr, -1000000) == -1000000, "primitiveDouble2");
    TEST_ASSERT(RaftJson::getDoubleIm(str1234, END(str1234), "abc", -1000000) == -1000000, "primitiveDouble3");
    TEST_ASSERT(RaftJson::getDoubleIm(str1234, END(str1234), "abc", 0) == 0, "primitiveDouble4");
    TEST_ASSERT(RaftJson::getDoubleIm(strNull, END(strNull), "", -1000000) == -1000000, "primitiveDouble5");
    TEST_ASSERT(RaftJson::getDoubleIm(strNull, END(strNull), nullptr, -1000000) == -1000000, "primitiveDouble6");
    TEST_ASSERT(RaftJson::getDoubleIm(strNull, END(strNull), "abc", -1000000) == -1000000, "primitiveDouble7");
    TEST_ASSERT(RaftJson::getDoubleIm(strNull, END(strNull), "abc", 0) == 0, "primitiveDouble8");
    TEST_ASSERT(RaftJson::getLongIm(str1234, END(str1234), "", -1000000) == 1234, "primitiveLong1");
    TEST_ASSERT(RaftJson::getLongIm(str1234, END(str1234), nullptr, -1000000) == -1000000, "primitiveLong2");
    TEST_ASSERT(RaftJson::getLongIm(str1234, END(str1234), "abc", -1000000) == -1000000, "primitiveLong3");
    TEST_ASSERT(RaftJson::getLongIm(str1234, END(str1234), "abc", 0) == 0, "primitiveLong4");
    TEST_ASSERT(RaftJson::getLongIm(strNull, END(strNull), "", -1000000) == -1000000, "primitiveLong5");
    TEST_ASSERT(RaftJson::getLongIm(strNull, END(strNull), nullptr, -1000000) == -1000000, "primitiveLong6");
    TEST_ASSERT(RaftJson::getLongIm(strNull, END(strNull), "abc", -1000000) == -1000000, "primitiveLong7");
    TEST_ASSERT(RaftJson::getLongIm(strNull, END(strNull), "abc", 0) == 0, "primitiveLong8");
    // Quoted "true" and "false" are booleans when read as numbers (REST query values are all strings),
    // as documented on RaftJson::getNumericValue (since 3d99054)
    TEST_ASSERT(RaftJson::getLongIm(strTrueInQuotes, END(strTrueInQuotes), "", 1234) == 1, "primitiveQuotedTrueAsLong");
    TEST_ASSERT(RaftJson::getLongIm(strFalseInQuotes, END(strFalseInQuotes), "", 1234) == 0, "primitiveQuotedFalseAsLong");
    TEST_ASSERT(RaftJson::getBoolIm(strTrueInQuotes, END(strTrueInQuotes), "", false) == true, "primitiveQuotedTrueAsBool");
    TEST_ASSERT(RaftJson::getBoolIm(strTrue, END(strTrue), "", false) == true, "primitiveBool1");
    TEST_ASSERT(RaftJson::getBoolIm(strTrue, END(strTrue), nullptr, false) == false, "primitiveBool2");
    TEST_ASSERT(RaftJson::getBoolIm(strTrue, END(strTrue), "abc", false) == false, "primitiveBool3");
    TEST_ASSERT(RaftJson::getBoolIm(strTrue, END(strTrue), "abc", true) == true, "primitiveBool4");
    TEST_ASSERT(RaftJson::getBoolIm(strFalse, END(strFalse), "", true) == false, "primitiveBool5");
    TEST_ASSERT(RaftJson::getBoolIm(strFalse, END(strFalse), nullptr, true) == true, "primitiveBool6");
    TEST_ASSERT(RaftJson::getBoolIm(strFalse, END(strFalse), "abc", true) == true, "primitiveBool7");
    TEST_ASSERT(RaftJson::getBoolIm(strFalse, END(strFalse), "abc", false) == false, "primitiveBool8");
    TEST_ASSERT(RaftJson::getBoolIm(strNull, END(strNull), "", true) == true, "primitiveBool9");
    TEST_ASSERT(RaftJson::getBoolIm(strNull, END(strNull), nullptr, true) == true, "primitiveBool10");
    TEST_ASSERT(RaftJson::getBoolIm(strNull, END(strNull), "abc", true) == true, "primitiveBool11");
    TEST_ASSERT(RaftJson::getBoolIm(strNull, END(strNull), "abc", false) == false, "primitiveBool12");
    #undef END
}

// Name-value pairs as used by device type records (e.g. initValues), with & and ; as pair separators
static void testExtractNameValues()
{
    printf("Running testExtractNameValues...\n");
    const char* testInput = R"(0x020701=&0x020801=&0x009600=&0x0097fd&0x00e301=&0x00e403=r1&0x00e502=&0x00e601=&0x00e703=0x123456&0x00f502=&0x00d905=&0x00dbce=&0x00dc03=&0x00ddf8=&0x009f00=&0x00a33c=&0x00b700=&0x00bb3c=&0x00b209=&0x00ca09=&0x019801=&0x01b017=&0x01ad00=&0x00ff05=r55;0x010005=&0x019905=&0x01a61b;0x01ac3e=&0x01a71f=&0x003000=;0x001110=&0x010a30=&0x003f46=&0x0031ff=&0x004163=&0x002e01=&0x001b09=&0x003e31=&0x001424=)";
    std::vector<RaftJson::NameValuePair> nvPairs;
    RaftJson::extractNameValues(testInput, "=", "&", ";", nvPairs);
    TEST_ASSERT(nvPairs.size() == 39, "numNVPairs");
    if (nvPairs.size() != 39)
        return;
    TEST_ASSERT(nvPairs[3].name == "0x0097fd", "nvPair3NameNoSeparator");
    TEST_ASSERT(nvPairs[3].value == "", "nvPair3ValueNoSeparator");
    TEST_ASSERT(nvPairs[4].name == "0x00e301", "nvPair4Name");
    TEST_ASSERT(nvPairs[4].value == "", "nvPair4Value");
    TEST_ASSERT(nvPairs[5].name == "0x00e403", "nvPair5Name");
    TEST_ASSERT(nvPairs[5].value == "r1", "nvPair5Value");
    TEST_ASSERT(nvPairs[6].name == "0x00e502", "nvPair6Name");
    TEST_ASSERT(nvPairs[6].value == "", "nvPair6Value");
    TEST_ASSERT(nvPairs[7].name == "0x00e601", "nvPair7Name");
    TEST_ASSERT(nvPairs[7].value == "", "nvPair7Value");
    TEST_ASSERT(nvPairs[8].name == "0x00e703", "nvPair8Name");
    TEST_ASSERT(nvPairs[8].value == "0x123456", "nvPair8Value");
    TEST_ASSERT(nvPairs[9].name == "0x00f502", "nvPair9Name");
    TEST_ASSERT(nvPairs[9].value == "", "nvPair9Value");
    TEST_ASSERT(nvPairs[10].name == "0x00d905", "nvPair10Name");
    TEST_ASSERT(nvPairs[10].value == "", "nvPair10Value");
    TEST_ASSERT(nvPairs[22].name == "0x01ad00", "nvPair22Name");
    TEST_ASSERT(nvPairs[22].value == "", "nvPair22Value");
    TEST_ASSERT(nvPairs[23].name == "0x00ff05", "nvPair23Name");
    TEST_ASSERT(nvPairs[23].value == "r55", "nvPair23Value");
    TEST_ASSERT(nvPairs[24].name == "0x010005", "nvPair24NameAfterSemicolon");
    TEST_ASSERT(nvPairs[24].value == "", "nvPair24Value");
    TEST_ASSERT(nvPairs[38].name == "0x001424", "nvPairLastName");
}

static void testArrays()
{
    printf("Running testArrays...\n");
    const RaftJson testRaftJsonArray(testJSON);
    std::vector<String> cooArrayElems;
    TEST_ASSERT(testRaftJsonArray.getArrayElems("consts/oxis/coo", cooArrayElems), "arrayElemsFound");
    TEST_ASSERT(cooArrayElems.size() == 4, "arrayElemsSize");
    if (cooArrayElems.size() == 4)
    {
        TEST_ASSERT(cooArrayElems[0] == "pig", "arrayElems0");
        TEST_ASSERT(cooArrayElems[2] == "dog", "arrayElems2");
    }

    // Array iterator over numbers
    RaftJson testArrayJson1(R"({"testArray":[1, 2, 3, 4, 5, 6, 7, 8, 9, 10]})");
    int idx = 1;
    for (auto elem : testArrayJson1.getArray("testArray"))
    {
        TEST_ASSERT(elem.toInt() == idx, "arrayIterNumbers");
        idx++;
    }
    TEST_ASSERT(idx == 11, "arrayIterNumbersCount");

    // Array iterator over strings, escapes and nested objects
    RaftJson testArrayJson2(R"({"testArray":["a", "bb", "ccc", "dd\\dd\ndd\n\n", "eeeee", "ffffff", "", "bananas", "{}", {"a": 1, "b": 2, "c": 3}]})");
    const char* expected[] = { "a", "bb", "ccc", "dd\\dd\ndd\n\n", "eeeee", "ffffff", "", "bananas", "{}",
                               "{\"a\": 1, \"b\": 2, \"c\": 3}" };
    idx = 0;
    for (auto elem : testArrayJson2.getArray("testArray"))
    {
        if (idx < 10)
            TEST_ASSERT(elem.toString() == expected[idx], ("arrayIterStrings" + String(idx)).c_str());
        idx++;
    }
    TEST_ASSERT(idx == 10, "arrayIterStringsCount");

    // Array size and indexed access
    TEST_ASSERT(testArrayJson1.getArray("testArray").size() == 10, "arraySize1");
    TEST_ASSERT(testArrayJson2.getArray("testArray").size() == 10, "arraySize2");
    TEST_ASSERT(testArrayJson1.getArray("testArray")[4].toInt() == 5, "arrayIndex1");
    TEST_ASSERT(testArrayJson2.getArray("testArray")[4].toString() == "eeeee", "arrayIndex2");
}

static void testObjects()
{
    printf("Running testObjects...\n");
    RaftJson testObjectJson(R"({"testObject":{"a": 1, "b": "hello", "c": {"minky":"monk","dinky":"donk"}, "d": 1234, "e": [1,2,3,4,5,6], "f": 6, "g": 7, "h": 8, "i": 9}})");
    struct Expected { const char* key; const char* value; };
    const Expected expected[] = {
        {"a", "1"}, {"b", "hello"}, {"c", "{\"minky\":\"monk\",\"dinky\":\"donk\"}"}, {"d", "1234"},
        {"e", "[1,2,3,4,5,6]"}, {"f", "6"}, {"g", "7"}, {"h", "8"}, {"i", "9"} };
    int idx = 0;
    for (auto elem : testObjectJson.getObject("testObject"))
    {
        if (idx < 9)
        {
            String name = "objectIter" + String(idx);
            TEST_ASSERT(elem.first == expected[idx].key, (name + "Key").c_str());
            TEST_ASSERT(elem.second.toString() == expected[idx].value, (name + "Value").c_str());
        }
        idx++;
    }
    TEST_ASSERT(idx == 9, "objectIterCount");
}

static void testAppendFields()
{
    printf("Running testAppendFields...\n");

    // String + raw append with escaping and comma insertion
    String json = "{";
    RaftJson::appendStringField(json, "type", "abc");
    RaftJson::appendStringField(json, "name", "q\"w\\e\nr");
    RaftJson::appendRawField(json, "poll", "{\"c\":1}");
    json += "}";
    TEST_ASSERT(json == "{\"type\":\"abc\",\"name\":\"q\\\"w\\\\e\\nr\",\"poll\":{\"c\":1}}", "appendFieldsEscapingAndComma");

    // Null values are ignored and do not change JSON
    String jsonNull = "{";
    RaftJson::appendStringField(jsonNull, "x", nullptr);
    RaftJson::appendRawField(jsonNull, "y", nullptr);
    jsonNull += "}";
    TEST_ASSERT(jsonNull == "{}", "appendFieldsNullNoOp");

    // A null name is also ignored
    String jsonNameNull = "{";
    RaftJson::appendStringField(jsonNameNull, nullptr, "abc");
    RaftJson::appendRawField(jsonNameNull, nullptr, "{\"a\":1}");
    jsonNameNull += "}";
    TEST_ASSERT(jsonNameNull == "{}", "appendFieldsNameNullNoOp");
}

// An escaped quote inside a nested object must not end the string when the object's bounds are located -
// otherwise every key after the object disappears (a posted setting like this hid a whole SysMod section)
static void testEscapedQuoteInObject()
{
    printf("Running testEscapedQuoteInObject...\n");
    RaftJson json("{\"Other\":{\"x\":1,\"s\":\"a\\\"b\"},\"RaftROS\":{\"routerHost\":\"1.2.3.4\"}}");
    std::vector<String> keys;
    json.getKeys("", keys);
    TEST_ASSERT(keys.size() == 2 && keys[1] == "RaftROS", "escapedQuoteKeysAfterObject");
    TEST_ASSERT(json.getString("RaftROS/routerHost", "") == "1.2.3.4", "escapedQuoteLaterValue");
    TEST_ASSERT(json.getString("Other/s", "") == "a\"b", "escapedQuoteValue");
    TEST_ASSERT(json.getString("Other", "") == "{\"x\":1,\"s\":\"a\\\"b\"}", "escapedQuoteObjectText");

    // An escaped backslash just before the closing quote ends the string normally
    RaftJson json2("{\"A\":{\"p\":\"c:\\\\\"},\"B\":2}");
    TEST_ASSERT(json2.getLong("B", -1) == 2, "escapedBackslashBeforeQuote");

    // The same inside an array
    RaftJson json3("{\"A\":[\"q\\\"]\",1],\"B\":3}");
    TEST_ASSERT(json3.getLong("B", -1) == 3, "escapedQuoteInArray");
}

int main()
{
    testDataDocuments();
    testGetString();
    testMissingKeyDefault();
    testPrimitives();
    testExtractNameValues();
    testArrays();
    testObjects();
    testAppendFields();
    testEscapedQuoteInObject();
    return testSummary("test_raft_json");
}
