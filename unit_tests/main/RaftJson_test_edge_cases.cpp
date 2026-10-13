/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Unit tests of RaftJson edge cases - empty containers, whitespace, escapes and iterators
//
// Rob Dobson 2017-2026
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

// Iteration cap so that a regression shows up as a failure rather than a hang
static const int MAX_TEST_ITERATIONS = 20;

static int countArrayIterations(const RaftJson& json, const char* pPath)
{
    int count = 0;
    for (RaftJson elem : json.getArray(pPath))
    {
        (void)elem;
        if (++count > MAX_TEST_ITERATIONS)
            break;
    }
    return count;
}

static int countObjectIterations(const RaftJson& json, const char* pPath)
{
    int count = 0;
    for (auto keyValue : json.getObject(pPath))
    {
        (void)keyValue;
        if (++count > MAX_TEST_ITERATIONS)
            break;
    }
    return count;
}

TEST_CASE("test_raftjson_empty_containers", "[raftjson_edge_cases]")
{
    // An empty object must not let the lookup continue into the parent object
    RaftJson emptyObj(R"({"a":{},"b":1})");
    TEST_ASSERT_EQUAL_MESSAGE(-99, emptyObj.getLong("a/b", -99), "emptyObjNoLeak");
    TEST_ASSERT_FALSE_MESSAGE(emptyObj.contains("a/b"), "emptyObjNotContains");
    TEST_ASSERT_EQUAL_MESSAGE(1, emptyObj.getLong("b", -99), "emptyObjSibling");
    RaftJson emptyObjSpaces(R"({"a":{  },"b":{"c":2}})");
    TEST_ASSERT_EQUAL_MESSAGE(-99, emptyObjSpaces.getLong("a/c", -99), "emptyObjSpacesNoLeak");
    TEST_ASSERT_EQUAL_MESSAGE(2, emptyObjSpaces.getLong("b/c", -99), "emptyObjSpacesSibling");

    // Indexing an empty array must return the default value
    RaftJson emptyArr(R"({"a":[],"b":[ ],"c":[5]})");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("DEF", emptyArr.getString("a[0]", "DEF").c_str(), "emptyArrString");
    TEST_ASSERT_EQUAL_MESSAGE(-99, emptyArr.getLong("a[0]", -99), "emptyArrLong");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("DEF", emptyArr.getString("b[0]", "DEF").c_str(), "emptyArrSpacesString");
    TEST_ASSERT_FALSE_MESSAGE(emptyArr.contains("a[0]"), "emptyArrNotContains");
    TEST_ASSERT_EQUAL_MESSAGE(5, emptyArr.getLong("c[0]", -99), "nonEmptyArr");
    TEST_ASSERT_EQUAL_MESSAGE(-99, emptyArr.getLong("c[1]", -99), "arrOutOfRange");
}

TEST_CASE("test_raftjson_array_whitespace", "[raftjson_edge_cases]")
{
    // Whitespace before the first array element (as in pretty-printed JSON)
    RaftJson json(R"({"a":[ 1, 2 ],"t":[ true ],"o":[
        {"x":3}
    ]})");
    int arrayLen = 0;
    TEST_ASSERT_EQUAL_MESSAGE(RaftJsonIF::RAFT_JSON_NUMBER, json.getType("a[0]", arrayLen), "typeFirstElem");
    TEST_ASSERT_TRUE_MESSAGE(json.contains("a[0]"), "containsFirstElem");
    TEST_ASSERT_TRUE_MESSAGE(json.contains("a[1]"), "containsSecondElem");
    TEST_ASSERT_FALSE_MESSAGE(json.contains("a[2]"), "notContainsThirdElem");
    TEST_ASSERT_EQUAL_MESSAGE(1, json.getLong("a[0]", -99), "longFirstElem");
    TEST_ASSERT_EQUAL_MESSAGE(2, json.getLong("a[1]", -99), "longSecondElem");
    TEST_ASSERT_TRUE_MESSAGE(json.getBool("t[0]", false), "boolFirstElem");
    TEST_ASSERT_EQUAL_MESSAGE(RaftJsonIF::RAFT_JSON_OBJECT, json.getType("o[0]", arrayLen), "typeObjElem");
    TEST_ASSERT_EQUAL_MESSAGE(3, json.getLong("o[0]/x", -99), "objElemValue");
}

TEST_CASE("test_raftjson_escaped_backslash", "[raftjson_edge_cases]")
{
    // A string ending in an escaped backslash must terminate at the following quote
    RaftJson json(R"({"s":"x\\","t":5})");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("x\\", json.getString("s", "DEF").c_str(), "trailingBackslashValue");
    TEST_ASSERT_EQUAL_MESSAGE(5, json.getLong("t", -99), "afterTrailingBackslash");
    std::vector<String> keys;
    TEST_ASSERT_TRUE_MESSAGE(json.getKeys("", keys), "getKeys");
    TEST_ASSERT_EQUAL_MESSAGE(2, keys.size(), "numKeys");

    // As an array element and inside a nested object
    RaftJson arr(R"({"a":["x\\",7],"o":{"s":"y\\"},"u":8})");
    TEST_ASSERT_EQUAL_MESSAGE(7, arr.getLong("a[1]", -99), "arrayElemAfterBackslash");
    TEST_ASSERT_EQUAL_MESSAGE(8, arr.getLong("u", -99), "keyAfterNestedBackslash");
    std::vector<String> elems;
    TEST_ASSERT_TRUE_MESSAGE(arr.getArrayElems("a", elems), "getArrayElems");
    TEST_ASSERT_EQUAL_MESSAGE(2, elems.size(), "numArrayElems");

    // A key ending in an escaped backslash
    RaftJson key(R"({"k\\":1,"t":2})");
    TEST_ASSERT_EQUAL_MESSAGE(2, key.getLong("t", -99), "keyWithTrailingBackslash");

    // An escaped quote followed by more text
    RaftJson quote(R"({"q":"a\"b","t":3})");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("a\"b", quote.getString("q", "DEF").c_str(), "escapedQuote");
    TEST_ASSERT_EQUAL_MESSAGE(3, quote.getLong("t", -99), "afterEscapedQuote");
}

TEST_CASE("test_raftjson_unescape", "[raftjson_edge_cases]")
{
    // Escaped backslash followed by a letter is not a control-character escape
    RaftJson path(R"({"p":"C:\\new\\tmp"})");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("C:\\new\\tmp", path.getString("p", "").c_str(), "escapedBackslashThenLetter");

    // All the single-character escapes
    RaftJson ctrl(R"({"c":"a\tb\r\nc\/d\be\ff\"g\\h"})");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("a\tb\r\nc/d\be\ff\"g\\h", ctrl.getString("c", "").c_str(), "singleCharEscapes");

    // Unicode escapes are decoded to UTF-8 (including a surrogate pair)
    RaftJson uni(R"({"u":"\u00e9\u20AC\ud83d\ude00!"})");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80!", uni.getString("u", "").c_str(), "unicodeEscapes");

    // Unknown or incomplete escapes are left as they are
    RaftJson unknown(R"({"x":"a\qb\u12"})");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("a\\qb\\u12", unknown.getString("x", "").c_str(), "unknownEscapes");

    // Array elements are unescaped too
    RaftJson arr(R"({"a":["x\\ny"]})");
    std::vector<String> elems;
    TEST_ASSERT_TRUE_MESSAGE(arr.getArrayElems("a", elems), "getArrayElems");
    TEST_ASSERT_EQUAL_MESSAGE(1, elems.size(), "numArrayElems");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("x\\ny", elems[0].c_str(), "arrayElemUnescaped");
}

TEST_CASE("test_raftjson_escape_round_trip", "[raftjson_edge_cases]")
{
    // Control characters, quotes and backslashes must be escaped to give valid JSON
    const char* pValue = "a\\b\"c\nd\te\rf\x01g";
    String json = "{";
    RaftJson::appendStringField(json, "v", pValue);
    json += "}";
    TEST_ASSERT_EQUAL_STRING_MESSAGE(R"({"v":"a\\b\"c\nd\te\rf\u0001g"})", json.c_str(), "escapedJson");

    // And read back to the original value
    RaftJson parsed(json);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(pValue, parsed.getString("v", "").c_str(), "roundTrip");

    // Strings without special characters are unchanged
    String plain = "plain text";
    RaftJson::escapeString(plain);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("plain text", plain.c_str(), "plainUnchanged");
}

TEST_CASE("test_raftjson_iterators", "[raftjson_edge_cases]")
{
    RaftJson json(R"({"empty":[],"emptySp":[ ],"emptyObj":{},"emptyObjSp":{ },"num":5,
                     "arr":[1, "two", {"x":3}],"obj":{"a":1, "b":[2]}})");

    // Empty, missing and non-container elements give no iterations
    TEST_ASSERT_EQUAL_MESSAGE(0, countArrayIterations(json, "empty"), "iterEmptyArray");
    TEST_ASSERT_EQUAL_MESSAGE(0, countArrayIterations(json, "emptySp"), "iterEmptyArraySpaces");
    TEST_ASSERT_EQUAL_MESSAGE(0, countArrayIterations(json, "missing"), "iterMissingArray");
    TEST_ASSERT_EQUAL_MESSAGE(0, countArrayIterations(json, "num"), "iterNonArray");
    TEST_ASSERT_EQUAL_MESSAGE(0, countArrayIterations(json, "obj"), "iterObjectAsArray");
    TEST_ASSERT_EQUAL_MESSAGE(0, countObjectIterations(json, "emptyObj"), "iterEmptyObject");
    TEST_ASSERT_EQUAL_MESSAGE(0, countObjectIterations(json, "emptyObjSp"), "iterEmptyObjectSpaces");
    TEST_ASSERT_EQUAL_MESSAGE(0, countObjectIterations(json, "missing"), "iterMissingObject");
    TEST_ASSERT_EQUAL_MESSAGE(0, countObjectIterations(json, "num"), "iterNonObject");
    TEST_ASSERT_EQUAL_MESSAGE(0, countObjectIterations(json, "arr"), "iterArrayAsObject");

    // Array iteration visits each element
    std::vector<String> arrVals;
    for (RaftJson elem : json.getArray("arr"))
    {
        arrVals.push_back(elem.toString());
        if (arrVals.size() > MAX_TEST_ITERATIONS)
            break;
    }
    TEST_ASSERT_EQUAL_MESSAGE(3, arrVals.size(), "iterArrayCount");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("1", arrVals[0].c_str(), "iterArrayElem0");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("two", arrVals[1].c_str(), "iterArrayElem1");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(R"({"x":3})", arrVals[2].c_str(), "iterArrayElem2");

    // Object iteration visits each key/value
    std::vector<String> objKeys;
    std::vector<String> objVals;
    for (auto keyValue : json.getObject("obj"))
    {
        objKeys.push_back(keyValue.first);
        objVals.push_back(keyValue.second.toString());
        if (objKeys.size() > MAX_TEST_ITERATIONS)
            break;
    }
    TEST_ASSERT_EQUAL_MESSAGE(2, objKeys.size(), "iterObjectCount");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("a", objKeys[0].c_str(), "iterObjectKey0");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("1", objVals[0].c_str(), "iterObjectVal0");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("b", objKeys[1].c_str(), "iterObjectKey1");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("[2]", objVals[1].c_str(), "iterObjectVal1");

    // Indexed access and size
    RaftJson::ArrayWrapper arr = json.getArray("arr");
    TEST_ASSERT_EQUAL_MESSAGE(3, arr.size(), "arraySize");
    TEST_ASSERT_EQUAL_MESSAGE(1, arr[0].toLong(), "arrayIndex0");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("two", arr[1].toString().c_str(), "arrayIndex1");
    TEST_ASSERT_EQUAL_MESSAGE(3, arr[2].getLong("x", -99), "arrayIndex2");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("{}", arr[3].toString().c_str(), "arrayIndexOutOfRange");
    TEST_ASSERT_EQUAL_MESSAGE(0, json.getArray("empty").size(), "emptyArraySize");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("{}", json.getArray("empty")[0].toString().c_str(), "emptyArrayIndex");
    TEST_ASSERT_EQUAL_MESSAGE(0, json.getArray("num").size(), "nonArraySize");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("{}", json.getArray("num")[0].toString().c_str(), "nonArrayIndex");
}

TEST_CASE("test_raftjson_malformed", "[raftjson_edge_cases]")
{
    // Malformed arrays must not hang or crash
    RaftJson json(R"({"a":[1}, "b":2})");
    int arrayLen = 0;
    json.getType("a", arrayLen);
    TEST_ASSERT_TRUE_MESSAGE(json.getArray("a").size() <= 1, "malformedSize");
    TEST_ASSERT_TRUE_MESSAGE(countArrayIterations(json, "a") <= 1, "malformedIter");
    json.getArray("a")[3];
    std::vector<String> elems;
    json.getArrayElems("a", elems);

    // Truncated documents
    RaftJson truncArr(R"({"a":[1, 2)");
    TEST_ASSERT_TRUE_MESSAGE(countArrayIterations(truncArr, "a") <= 2, "truncatedArrayIter");
    truncArr.getArray("a")[5];
    RaftJson truncObj(R"({"o":{"k":)");
    TEST_ASSERT_TRUE_MESSAGE(countObjectIterations(truncObj, "o") <= 1, "truncatedObjectIter");
    RaftJson truncKey(R"({"o":{"k)");
    TEST_ASSERT_TRUE_MESSAGE(countObjectIterations(truncKey, "o") <= 1, "truncatedKeyIter");
}
