/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Unit tests of RaftJson chained documents and document bounds
//
// Rob Dobson 2017-2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory>
#include <vector>
#include "unity.h"
#include "unity_test_runner.h"
#include "RaftArduino.h"
#include "RaftJson.h"
#include "RaftJsonPrefixed.h"
#include "Logger.h"
#include "RaftUtils.h"

// Iteration cap so that a regression shows up as a failure rather than a hang
static const int MAX_TEST_ITERATIONS = 20;

// Check the array and object access methods on a document where "arr" and "obj" are only in a chained document
static void checkChainedContainers(const RaftJson& json, const char* pMsgPrefix)
{
    char msg[100];
    #define CHAINED_MSG(x) (snprintf(msg, sizeof(msg), "%s %s", pMsgPrefix, x), msg)

    // Array access
    TEST_ASSERT_EQUAL_MESSAGE(3, json.getArray("arr").size(), CHAINED_MSG("arraySize"));
    TEST_ASSERT_EQUAL_MESSAGE(30, json.getArray("arr")[2].toLong(), CHAINED_MSG("arrayIndex"));
    std::vector<long> vals;
    for (RaftJson elem : json.getArray("arr"))
    {
        vals.push_back(elem.toLong());
        if (vals.size() > MAX_TEST_ITERATIONS)
            break;
    }
    TEST_ASSERT_EQUAL_MESSAGE(3, vals.size(), CHAINED_MSG("arrayIterCount"));
    TEST_ASSERT_EQUAL_MESSAGE(20, vals[1], CHAINED_MSG("arrayIterValue"));
    std::vector<String> elems;
    TEST_ASSERT_TRUE_MESSAGE(json.getArrayElems("arr", elems), CHAINED_MSG("getArrayElems"));
    TEST_ASSERT_EQUAL_MESSAGE(3, elems.size(), CHAINED_MSG("getArrayElemsCount"));
    int arrayLen = 0;
    TEST_ASSERT_EQUAL_MESSAGE(RaftJsonIF::RAFT_JSON_ARRAY, json.getType("arr", arrayLen), CHAINED_MSG("getTypeArray"));
    TEST_ASSERT_EQUAL_MESSAGE(3, arrayLen, CHAINED_MSG("getTypeArrayLen"));

    // Object access
    std::vector<String> keys;
    for (auto keyValue : json.getObject("obj"))
    {
        keys.push_back(keyValue.first);
        if (keys.size() > MAX_TEST_ITERATIONS)
            break;
    }
    TEST_ASSERT_EQUAL_MESSAGE(2, keys.size(), CHAINED_MSG("objectIterCount"));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("b", keys[1].c_str(), CHAINED_MSG("objectIterKey"));
    keys.clear();
    TEST_ASSERT_TRUE_MESSAGE(json.getKeys("obj", keys), CHAINED_MSG("getKeys"));
    TEST_ASSERT_EQUAL_MESSAGE(2, keys.size(), CHAINED_MSG("getKeysCount"));
    TEST_ASSERT_EQUAL_STRING_MESSAGE(R"({"a":1,"b":2})", json.getString("obj", "").c_str(), CHAINED_MSG("getStringObject"));
    #undef CHAINED_MSG
}

TEST_CASE("test_raftjson_chained_containers", "[raftjson_chained]")
{
    // Both documents in one buffer so that the chained document's address relative to the main one is known
    static const char CHAINED_DOC[] = R"({"arr":[10,20,30],"obj":{"a":1,"b":2}})";
    static const char MAIN_DOC[] = R"({"x":1})";
    char buf[sizeof(MAIN_DOC) + sizeof(CHAINED_DOC)];

    // Chained document at a higher address than the main document
    memcpy(buf, MAIN_DOC, sizeof(MAIN_DOC));
    memcpy(buf + sizeof(MAIN_DOC), CHAINED_DOC, sizeof(CHAINED_DOC));
    {
        RaftJson chained(buf + sizeof(MAIN_DOC), false);
        RaftJson json(buf, false, nullptr, &chained);
        checkChainedContainers(json, "chainedHigher");
    }

    // Chained document at a lower address than the main document
    memcpy(buf, CHAINED_DOC, sizeof(CHAINED_DOC));
    memcpy(buf + sizeof(CHAINED_DOC), MAIN_DOC, sizeof(MAIN_DOC));
    {
        RaftJson chained(buf, false);
        RaftJson json(buf + sizeof(CHAINED_DOC), false, nullptr, &chained);
        checkChainedContainers(json, "chainedLower");
    }
}

TEST_CASE("test_raftjson_chained_three_levels", "[raftjson_chained]")
{
    // Base document is last in the buffer (beyond the end of the middle document)
    static const char MAIN_DOC[] = R"({"m":1})";
    static const char MID_DOC[] = R"({"mid":2})";
    static const char BASE_DOC[] = R"({"deep":{"arr":[4,5],"o":{"k":"v"}},"s":"str"})";
    char buf[sizeof(MAIN_DOC) + sizeof(MID_DOC) + sizeof(BASE_DOC)];
    memcpy(buf, MAIN_DOC, sizeof(MAIN_DOC));
    memcpy(buf + sizeof(MAIN_DOC), MID_DOC, sizeof(MID_DOC));
    memcpy(buf + sizeof(MAIN_DOC) + sizeof(MID_DOC), BASE_DOC, sizeof(BASE_DOC));
    RaftJson base(buf + sizeof(MAIN_DOC) + sizeof(MID_DOC), false);
    RaftJson mid(buf + sizeof(MAIN_DOC), false, nullptr, &base);
    RaftJson json(buf, false, nullptr, &mid);

    // Values from each level
    TEST_ASSERT_EQUAL_MESSAGE(1, json.getLong("m", -1), "mainValue");
    TEST_ASSERT_EQUAL_MESSAGE(2, json.getLong("mid", -1), "midValue");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("str", json.getString("s", "").c_str(), "baseValue");

    // Containers in the base document
    std::vector<String> elems;
    TEST_ASSERT_TRUE_MESSAGE(json.getArrayElems("deep/arr", elems), "baseGetArrayElems");
    TEST_ASSERT_EQUAL_MESSAGE(2, elems.size(), "baseGetArrayElemsCount");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("5", elems[1].c_str(), "baseGetArrayElemsValue");
    std::vector<String> keys;
    TEST_ASSERT_TRUE_MESSAGE(json.getKeys("deep", keys), "baseGetKeys");
    TEST_ASSERT_EQUAL_MESSAGE(2, keys.size(), "baseGetKeysCount");
    int arrayLen = 0;
    TEST_ASSERT_EQUAL_MESSAGE(RaftJsonIF::RAFT_JSON_ARRAY, json.getType("deep/arr", arrayLen), "baseGetType");
    TEST_ASSERT_EQUAL_MESSAGE(2, arrayLen, "baseGetTypeArrayLen");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(R"({"k":"v"})", json.getString("deep/o", "").c_str(), "baseGetStringObject");
    TEST_ASSERT_EQUAL_MESSAGE(2, json.getArray("deep/arr").size(), "baseArraySize");
    TEST_ASSERT_EQUAL_MESSAGE(5, json.getArray("deep/arr")[1].toLong(), "baseArrayIndex");
}

TEST_CASE("test_raftjson_chained_prefixed", "[raftjson_chained]")
{
    // A RaftJsonPrefixed used as the chained document
    RaftJson base(R"({"pfx":{"k":9,"arr":[1,2],"o":{"a":1}},"k":99})");
    RaftJsonPrefixed prefixed(base, "pfx");
    RaftJson json(R"({"own":1})", true, nullptr, &prefixed);
    TEST_ASSERT_EQUAL_MESSAGE(1, json.getLong("own", -1), "ownValue");
    TEST_ASSERT_EQUAL_MESSAGE(9, json.getLong("k", -1), "prefixedValue");
    TEST_ASSERT_TRUE_MESSAGE(json.contains("arr"), "prefixedContains");
    TEST_ASSERT_EQUAL_MESSAGE(2, json.getArray("arr").size(), "prefixedArraySize");
    std::vector<String> keys;
    TEST_ASSERT_TRUE_MESSAGE(json.getKeys("o", keys), "prefixedGetKeys");
    TEST_ASSERT_EQUAL_MESSAGE(1, keys.size(), "prefixedGetKeysCount");
}

TEST_CASE("test_raftjson_bounded_view", "[raftjson_chained]")
{
    // Nothing beyond the end of a document must be used
    static const char TWO_DOCS[] = R"({"a":1}{"b":2})";
    RaftJson view(TWO_DOCS, false, TWO_DOCS + 7);
    TEST_ASSERT_EQUAL_MESSAGE(1, view.getLong("a", -1), "viewValue");
    TEST_ASSERT_FALSE_MESSAGE(view.contains("b"), "viewNotContainsBeyondEnd");
    TEST_ASSERT_EQUAL_MESSAGE(-1, view.getLong("b", -1), "viewValueBeyondEnd");
    std::vector<String> keys;
    TEST_ASSERT_TRUE_MESSAGE(view.getKeys("", keys), "viewGetKeys");
    TEST_ASSERT_EQUAL_MESSAGE(1, keys.size(), "viewGetKeysCount");

    // Numbers and literals end at the end of the document
    static const char DIGITS[] = "1234";
    RaftJson numView(DIGITS, false, DIGITS + 2);
    TEST_ASSERT_EQUAL_MESSAGE(12, numView.getLong("", -1), "numberViewLong");
    static const char FLOAT_STR[] = "1.5e3";
    RaftJson floatView(FLOAT_STR, false, FLOAT_STR + 3);
    TEST_ASSERT_TRUE_MESSAGE(floatView.getDouble("", -1) == 1.5, "numberViewDouble");
    static const char TRUE_STR[] = "true";
    RaftJson litView(TRUE_STR, false, TRUE_STR + 3);
    int arrayLen = 0;
    TEST_ASSERT_EQUAL_MESSAGE(RaftJsonIF::RAFT_JSON_UNDEFINED, litView.getType("", arrayLen), "literalViewType");

    // A document which isn't null-terminated (as for a region of flash)
    static const char OBJ_STR[] = R"({"a":[1,"x"],"b":{"c":3}})";
    size_t objLen = strlen(OBJ_STR);
    std::unique_ptr<char[]> pExact(new char[objLen]);
    memcpy(pExact.get(), OBJ_STR, objLen);
    RaftJson exactView(pExact.get(), false, pExact.get() + objLen);
    TEST_ASSERT_EQUAL_MESSAGE(3, exactView.getLong("b/c", -1), "unterminatedViewValue");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("x", exactView.getString("a[1]", "").c_str(), "unterminatedViewString");
    TEST_ASSERT_EQUAL_MESSAGE(-1, exactView.getLong("z", -1), "unterminatedViewMissing");
    TEST_ASSERT_EQUAL_MESSAGE(2, exactView.getArray("a").size(), "unterminatedViewArraySize");
    RaftJson exactCopy(pExact.get(), true, pExact.get() + objLen);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(OBJ_STR, exactCopy.getJsonDoc(), "unterminatedCopy");
}

TEST_CASE("test_raftjson_truncated", "[raftjson_chained]")
{
    // An object or array cut off by the end of the document is not a valid value
    RaftJson truncObj(R"({"o":{"k":1)");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("DEF", truncObj.getString("o", "DEF").c_str(), "truncatedObjectString");
    RaftJson truncArr(R"({"a":[1,2)");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("DEF", truncArr.getString("a", "DEF").c_str(), "truncatedArrayString");
    std::vector<String> elems;
    TEST_ASSERT_FALSE_MESSAGE(truncArr.getArrayElems("a", elems), "truncatedArrayElems");

    // Container closed only beyond the end of the document
    static const char FULL[] = R"({"a":[1,2],"b":1})";
    RaftJson cutView(FULL, false, FULL + 9);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("DEF", cutView.getString("a", "DEF").c_str(), "cutArrayString");
}

TEST_CASE("test_raftjson_null_source", "[raftjson_chained]")
{
    // Null sources give an empty document rather than crashing
    RaftJson fromNull((const char*)nullptr);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("{}", fromNull.getJsonDoc(), "constructFromNull");
    TEST_ASSERT_EQUAL_MESSAGE(-1, fromNull.getLong("a", -1), "constructFromNullValue");
    RaftJson assigned(R"({"a":1})");
    assigned = (const char*)nullptr;
    TEST_ASSERT_EQUAL_STRING_MESSAGE("{}", assigned.getJsonDoc(), "assignNull");
    RaftJson setDoc(R"({"a":1})");
    TEST_ASSERT_FALSE_MESSAGE(setDoc.setJsonDoc(nullptr), "setJsonDocNull");
    TEST_ASSERT_EQUAL_MESSAGE(1, setDoc.getLong("a", -1), "setJsonDocNullUnchanged");
}
