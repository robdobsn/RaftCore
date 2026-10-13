/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Unit tests of RaftJson conversions, integer arrays, key matching and taking ownership of a buffer
//
// Rob Dobson 2017-2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory>
#include <utility>
#include <vector>
#include "unity.h"
#include "unity_test_runner.h"
#include "RaftArduino.h"
#include "RaftJson.h"
#include "Logger.h"
#include "RaftUtils.h"

TEST_CASE("test_raftjson_to_conversions", "[raftjson_conversions]")
{
    // Whole-document conversions (as used on elements from iterators)
    TEST_ASSERT_EQUAL_MESSAGE(42, RaftJson("42").toInt(), "toIntNumber");
    TEST_ASSERT_EQUAL_MESSAGE(-17, RaftJson(" -17 ").toLong(), "toLongWhitespace");
    TEST_ASSERT_EQUAL_MESSAGE(0, RaftJson("0x10").toInt(), "toIntDecimalOnly");
    TEST_ASSERT_EQUAL_MESSAGE(0, RaftJson(R"("7")").toInt(), "toIntQuoted");
    TEST_ASSERT_EQUAL_MESSAGE(0, RaftJson(R"({"a":1})").toInt(), "toIntObject");
    TEST_ASSERT_TRUE_MESSAGE(RaftJson("1.5e3").toDouble() == 1500.0, "toDoubleExponent");
    TEST_ASSERT_TRUE_MESSAGE(RaftJson("-0.25").toDouble() == -0.25, "toDoubleNegative");
    TEST_ASSERT_TRUE_MESSAGE(RaftJson("true").toBool(), "toBoolTrue");
    TEST_ASSERT_FALSE_MESSAGE(RaftJson("false").toBool(), "toBoolFalse");
    TEST_ASSERT_TRUE_MESSAGE(RaftJson("2").toBool(), "toBoolNonZero");
    TEST_ASSERT_FALSE_MESSAGE(RaftJson("0").toBool(), "toBoolZero");

    // Views end at their end even where the following text would continue the number
    static const char DIGITS[] = "123,456";
    RaftJson view(DIGITS, false, DIGITS + 3);
    TEST_ASSERT_EQUAL_MESSAGE(123, view.toInt(), "toIntView");
    static const char JOINED[] = "1234";
    RaftJson joinedView(JOINED, false, JOINED + 2);
    TEST_ASSERT_EQUAL_MESSAGE(12, joinedView.toLong(), "toLongViewNoDelimiter");
    TEST_ASSERT_TRUE_MESSAGE(RaftJson(JOINED, false, JOINED + 3).toDouble() == 123.0, "toDoubleViewNoDelimiter");

    // A buffer which isn't null-terminated
    static const char NUM_STR[] = "987";
    std::unique_ptr<char[]> pExact(new char[3]);
    memcpy(pExact.get(), NUM_STR, 3);
    RaftJson exactView(pExact.get(), false, pExact.get() + 3);
    TEST_ASSERT_EQUAL_MESSAGE(987, exactView.toInt(), "toIntUnterminated");
    TEST_ASSERT_TRUE_MESSAGE(exactView.toBool(), "toBoolUnterminated");

    // Elements from an iterator
    RaftJson arr(R"({"a":[5, -6.5, true, "x"]})");
    std::vector<long> vals;
    for (RaftJson elem : arr.getArray("a"))
        vals.push_back(elem.toLong());
    TEST_ASSERT_EQUAL_MESSAGE(4, vals.size(), "iterCount");
    TEST_ASSERT_EQUAL_MESSAGE(5, vals[0], "iterToLong0");
    TEST_ASSERT_EQUAL_MESSAGE(-6, vals[1], "iterToLong1");
    TEST_ASSERT_TRUE_MESSAGE(arr.getArray("a")[1].toDouble() == -6.5, "iterToDouble");
    TEST_ASSERT_TRUE_MESSAGE(arr.getArray("a")[2].toBool(), "iterToBool");
}

TEST_CASE("test_raftjson_array_ints", "[raftjson_conversions]")
{
    // Elements are converted as integers (strings without their quotes, other values as 0)
    RaftJson json(R"({"a":["12", 3, true, 1.9, "0x10", -4, "1", null],"n":5,"e":[]})");
    std::vector<int> ints;
    TEST_ASSERT_TRUE_MESSAGE(json.getArrayInts("a", ints), "getArrayInts");
    static const int expected[] = {12, 3, 0, 1, 0, -4, 1, 0};
    TEST_ASSERT_EQUAL_MESSAGE(sizeof(expected) / sizeof(expected[0]), ints.size(), "getArrayIntsCount");
    for (size_t i = 0; i < ints.size(); i++)
        TEST_ASSERT_EQUAL_MESSAGE(expected[i], ints[i], "getArrayIntsValue");

    // Results are appended to the list
    TEST_ASSERT_TRUE_MESSAGE(json.getArrayInts("e", ints), "getArrayIntsEmpty");
    TEST_ASSERT_EQUAL_MESSAGE(sizeof(expected) / sizeof(expected[0]), ints.size(), "getArrayIntsEmptyAppendsNothing");
    std::vector<int> more = {99};
    TEST_ASSERT_TRUE_MESSAGE(json.getArrayInts("a", more), "getArrayIntsAppend");
    TEST_ASSERT_EQUAL_MESSAGE(99, more[0], "getArrayIntsAppendKeeps");
    TEST_ASSERT_EQUAL_MESSAGE(1 + sizeof(expected) / sizeof(expected[0]), more.size(), "getArrayIntsAppendCount");

    // Not found, not an array, unterminated
    std::vector<int> none;
    TEST_ASSERT_FALSE_MESSAGE(json.getArrayInts("missing", none), "getArrayIntsMissing");
    TEST_ASSERT_FALSE_MESSAGE(json.getArrayInts("n", none), "getArrayIntsNotArray");
    RaftJson trunc(R"({"a":[1,2)");
    TEST_ASSERT_FALSE_MESSAGE(trunc.getArrayInts("a", none), "getArrayIntsUnterminated");

    // Array in a chained document
    RaftJson chained(R"({"c":[7,8,9]})");
    RaftJson main(R"({"x":1})", true, nullptr, &chained);
    std::vector<int> chainedInts;
    TEST_ASSERT_TRUE_MESSAGE(main.getArrayInts("c", chainedInts), "getArrayIntsChained");
    TEST_ASSERT_EQUAL_MESSAGE(3, chainedInts.size(), "getArrayIntsChainedCount");
    TEST_ASSERT_EQUAL_MESSAGE(9, chainedInts[2], "getArrayIntsChainedValue");
}

TEST_CASE("test_raftjson_key_match", "[raftjson_conversions]")
{
    // A document key must match the whole of a path part - and only that part
    RaftJson slashKey(R"({"a/b":{"b":7},"a":{"b":1}})");
    TEST_ASSERT_EQUAL_MESSAGE(1, slashKey.getLong("a/b", -1), "keyContainingSlash");
    RaftJson bracketKey(R"({"a[":{"0":9},"a":[5]})");
    TEST_ASSERT_EQUAL_MESSAGE(5, bracketKey.getLong("a[0]", -1), "keyContainingBracket");
    RaftJson prefixKeys(R"({"ab":1,"a":2,"abc":3})");
    TEST_ASSERT_EQUAL_MESSAGE(2, prefixKeys.getLong("a", -1), "keyPrefixOfOther");
    TEST_ASSERT_EQUAL_MESSAGE(1, prefixKeys.getLong("ab", -1), "keyMiddle");
    TEST_ASSERT_EQUAL_MESSAGE(3, prefixKeys.getLong("abc", -1), "keyLongest");
    TEST_ASSERT_EQUAL_MESSAGE(-1, prefixKeys.getLong("abcd", -1), "keyLongerThanAny");
    TEST_ASSERT_EQUAL_MESSAGE(-1, prefixKeys.getLong("b", -1), "keyMissing");
}

// Exposes the protected method that takes ownership of a buffer
class RaftJsonOwnBufferTest : public RaftJson
{
public:
    void takeBuffer(RaftJson::JsonStrVec&& jsonStr)
    {
        setSourceVec(std::move(jsonStr));
    }
};

TEST_CASE("test_raftjson_take_buffer", "[raftjson_conversions]")
{
    // A null-terminated buffer is used without copying
    static const char DOC[] = R"({"k":"v","n":3})";
    RaftJson::JsonStrVec buf(DOC, DOC + sizeof(DOC));
    const char* pBufData = buf.data();
    RaftJsonOwnBufferTest json;
    json.takeBuffer(std::move(buf));
    TEST_ASSERT_TRUE_MESSAGE(json.getJsonDoc() == pBufData, "bufferNotCopied");
    TEST_ASSERT_EQUAL_MESSAGE(strlen(DOC), json.getJsonDocEnd() - json.getJsonDoc(), "bufferDocLen");
    TEST_ASSERT_EQUAL_MESSAGE(3, json.getLong("n", -1), "bufferValue");

    // A copy of the object has its own buffer
    RaftJson copied(json);
    TEST_ASSERT_TRUE_MESSAGE(copied.getJsonDoc() != pBufData, "copyOwnBuffer");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("v", copied.getString("k", "").c_str(), "copyValue");

    // A buffer without a terminator is terminated
    static const char UNTERM[] = R"({"u":1})";
    RaftJson::JsonStrVec unterm(UNTERM, UNTERM + strlen(UNTERM));
    json.takeBuffer(std::move(unterm));
    TEST_ASSERT_EQUAL_STRING_MESSAGE(UNTERM, json.getJsonDoc(), "unterminatedBuffer");
    TEST_ASSERT_EQUAL_MESSAGE(1, json.getLong("u", -1), "unterminatedBufferValue");

    // An empty buffer gives an empty document
    json.takeBuffer(RaftJson::JsonStrVec());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("{}", json.getJsonDoc(), "emptyBuffer");
}
