/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Unit tests of RaftJson number parsing (bases, 64 bit values), quoted booleans, escaping in the JSON
// builders and name-value extraction with unusual delimiters
//
// Rob Dobson 2017-2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "unity.h"
#include "unity_test_runner.h"
#include "RaftArduino.h"
#include "RaftJson.h"
#include "RaftJsonPrefixed.h"
#include "Logger.h"
#include "RaftUtils.h"

TEST_CASE("test_raftjson_number_base", "[raftjson_parsing]")
{
    // Leading zeros are decimal (not octal) and 0x prefixed values are hex
    RaftJson json(R"({"o":"010","t":"08","n":"-012","h":"0x10","H":"0X1f","nh":"-0x10","d":10,"f":"010.5"})");
    TEST_ASSERT_EQUAL_MESSAGE(10, json.getLong("o", -1), "leadingZeroDecimal");
    TEST_ASSERT_EQUAL_MESSAGE(8, json.getLong("t", -1), "leadingZeroNotOctal");
    TEST_ASSERT_EQUAL_MESSAGE(-12, json.getLong("n", -1), "negativeLeadingZero");
    TEST_ASSERT_EQUAL_MESSAGE(16, json.getLong("h", -1), "hexLower");
    TEST_ASSERT_EQUAL_MESSAGE(31, json.getInt("H", -1), "hexUpperPrefix");
    TEST_ASSERT_EQUAL_MESSAGE(-16, json.getLong("nh", -1), "hexNegative");
    TEST_ASSERT_EQUAL_MESSAGE(10, json.getLong("d", -1), "plainDecimal");
    TEST_ASSERT_TRUE_MESSAGE(json.getDouble("f", -1) == 10.5, "doubleLeadingZero");
    TEST_ASSERT_TRUE_MESSAGE(json.getBool("o", false), "boolFromLeadingZero");
}

TEST_CASE("test_raftjson_quoted_bool", "[raftjson_parsing]")
{
    // Quoted true and false are treated as booleans (as quoted numbers are treated as numbers)
    RaftJson json(R"({"t":"true","f":"false","ut":true,"uf":false,"tx":"trueish","T":"TRUE","params":{"active":"true"}})");
    TEST_ASSERT_TRUE_MESSAGE(json.getBool("t", false), "quotedTrue");
    TEST_ASSERT_FALSE_MESSAGE(json.getBool("f", true), "quotedFalse");
    TEST_ASSERT_EQUAL_MESSAGE(1, json.getLong("t", -1), "quotedTrueLong");
    TEST_ASSERT_EQUAL_MESSAGE(0, json.getInt("f", -1), "quotedFalseInt");
    TEST_ASSERT_TRUE_MESSAGE(json.getDouble("t", -1) == 1.0, "quotedTrueDouble");
    TEST_ASSERT_TRUE_MESSAGE(json.getBool("ut", false), "unquotedTrue");
    TEST_ASSERT_FALSE_MESSAGE(json.getBool("uf", true), "unquotedFalse");

    // Only the exact (lower case) literals
    TEST_ASSERT_FALSE_MESSAGE(json.getBool("tx", false), "quotedTrueWithSuffix");
    TEST_ASSERT_FALSE_MESSAGE(json.getBool("T", false), "quotedUpperCase");

    // As from a REST request with ?active=true
    TEST_ASSERT_TRUE_MESSAGE(json.getBool("params/active", false), "restParamTrue");

    // The type of a quoted literal is still string
    int arrayLen = 0;
    TEST_ASSERT_EQUAL_MESSAGE(RaftJsonIF::RAFT_JSON_STRING, json.getType("t", arrayLen), "quotedTrueType");
}

TEST_CASE("test_raftjson_int64", "[raftjson_parsing]")
{
    // Values beyond 32 bits (e.g. masks)
    RaftJson json(R"({"m":"0xfffff000","big":3000000000,"neg":-5,"s":"12","b":true,"q":"true",)"
                  R"("all":"0xffffffffffffffff","o":"010","n":null,"huge":-9000000000000000000})");
    TEST_ASSERT_TRUE_MESSAGE(json.getInt64("m", -1) == 4294963200LL, "int64HexMask");
    TEST_ASSERT_TRUE_MESSAGE((uint32_t)json.getInt64("m", -1) == 0xfffff000u, "int64MaskAsUInt32");
    TEST_ASSERT_TRUE_MESSAGE(json.getInt64("big", -1) == 3000000000LL, "int64Decimal");
    TEST_ASSERT_TRUE_MESSAGE(json.getInt64("neg", 0) == -5, "int64Negative");
    TEST_ASSERT_TRUE_MESSAGE(json.getInt64("huge", 0) == -9000000000000000000LL, "int64LargeNegative");
    TEST_ASSERT_TRUE_MESSAGE(json.getInt64("s", -1) == 12, "int64QuotedNumber");
    TEST_ASSERT_TRUE_MESSAGE(json.getInt64("b", -1) == 1, "int64Bool");
    TEST_ASSERT_TRUE_MESSAGE(json.getInt64("q", -1) == 1, "int64QuotedBool");
    TEST_ASSERT_TRUE_MESSAGE((uint64_t)json.getInt64("all", 0) == 0xffffffffffffffffULL, "int64AllBitsHex");
    TEST_ASSERT_TRUE_MESSAGE(json.getInt64("o", -1) == 10, "int64LeadingZeroDecimal");
    TEST_ASSERT_TRUE_MESSAGE(json.getInt64("n", -7) == -7, "int64NullDefault");
    TEST_ASSERT_TRUE_MESSAGE(json.getInt64("missing", 1234567890123LL) == 1234567890123LL, "int64MissingDefault");

    // Through the interface and a prefixed document
    const RaftJsonIF& jsonIF = json;
    TEST_ASSERT_TRUE_MESSAGE(jsonIF.getInt64("big", -1) == 3000000000LL, "int64ViaInterface");
    RaftJson nested(R"({"pfx":{"m":"0xfffff000"}})");
    RaftJsonPrefixed prefixed(nested, "pfx");
    TEST_ASSERT_TRUE_MESSAGE(prefixed.getInt64("m", -1) == 4294963200LL, "int64ViaPrefixed");

    // In a chained document
    RaftJson chained(R"({"c":"0x100000000"})");
    RaftJson withChain(R"({"x":1})", true, nullptr, &chained);
    TEST_ASSERT_TRUE_MESSAGE(withChain.getInt64("c", -1) == 0x100000000LL, "int64Chained");
}

TEST_CASE("test_raftjson_builder_escapes", "[raftjson_parsing]")
{
    // Field names are escaped
    String json = "{";
    RaftJson::appendStringField(json, "a\"b", "v");
    RaftJson::appendRawField(json, "c\\d", "1");
    json += "}";
    TEST_ASSERT_EQUAL_STRING_MESSAGE(R"({"a\"b":"v","c\\d":1})", json.c_str(), "appendFieldNamesEscaped");
    RaftJson parsed(json);
    std::vector<String> keys;
    TEST_ASSERT_TRUE_MESSAGE(parsed.getKeys("", keys), "appendFieldNamesValidJson");
    TEST_ASSERT_EQUAL_MESSAGE(2, keys.size(), "appendFieldNamesKeyCount");

    // Name-value pairs (e.g. from REST query parameters) give valid JSON whatever they contain
    std::vector<RaftJson::NameValuePair> pairs;
    pairs.push_back(RaftJson::NameValuePair("q", "say \"hi\" \\ end"));
    pairs.push_back(RaftJson::NameValuePair("n\"m", "plain"));
    pairs.push_back(RaftJson::NameValuePair("arr", "[1,2]"));
    pairs.push_back(RaftJson::NameValuePair("obj", R"({"x":3})"));
    String nvJson = RaftJson::getJSONFromNVPairs(pairs, true);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(R"({"q":"say \"hi\" \\ end","n\"m":"plain","arr":[1,2],"obj":{"x":3}})", nvJson.c_str(), "nvPairsEscaped");
    RaftJson nvParsed(nvJson);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("say \"hi\" \\ end", nvParsed.getString("q", "").c_str(), "nvPairsValueRoundTrip");
    TEST_ASSERT_EQUAL_MESSAGE(2, nvParsed.getArray("arr").size(), "nvPairsRawArray");
    TEST_ASSERT_EQUAL_MESSAGE(3, nvParsed.getLong("obj/x", -1), "nvPairsRawObject");
    String nvJsonNoBraces = RaftJson::getJSONFromNVPairs(pairs, false);
    TEST_ASSERT_TRUE_MESSAGE(nvJsonNoBraces.startsWith("\"q\":"), "nvPairsNoBraces");
}

TEST_CASE("test_raftjson_extract_nv_delimiters", "[raftjson_parsing]")
{
    // An empty or null pair delimiter means the whole string is one pair (rather than never advancing)
    std::vector<RaftJson::NameValuePair> pairs;
    RaftJson::extractNameValues("a=1&b=2", "=", "", nullptr, pairs);
    TEST_ASSERT_EQUAL_MESSAGE(1, pairs.size(), "emptyPairDelimOnePair");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("a", pairs[0].name.c_str(), "emptyPairDelimName");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("1&b=2", pairs[0].value.c_str(), "emptyPairDelimValue");
    pairs.clear();
    RaftJson::extractNameValues("a=1&b=2", "=", nullptr, nullptr, pairs);
    TEST_ASSERT_EQUAL_MESSAGE(1, pairs.size(), "nullPairDelimOnePair");

    // An empty alternate delimiter is ignored
    pairs.clear();
    RaftJson::extractNameValues("a=1&b=2", "=", "&", "", pairs);
    TEST_ASSERT_EQUAL_MESSAGE(2, pairs.size(), "emptyAltDelimIgnored");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("2", pairs[1].value.c_str(), "emptyAltDelimValue");

    // An empty or null name-value separator gives names without values
    pairs.clear();
    RaftJson::extractNameValues("a=1&b", "", "&", nullptr, pairs);
    TEST_ASSERT_EQUAL_MESSAGE(2, pairs.size(), "emptySepPairs");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("a=1", pairs[0].name.c_str(), "emptySepName");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", pairs[0].value.c_str(), "emptySepValue");
    pairs.clear();
    RaftJson::extractNameValues("a=1", nullptr, "&", nullptr, pairs);
    TEST_ASSERT_EQUAL_MESSAGE(1, pairs.size(), "nullSepPairs");

    // Usual delimiters are unchanged
    pairs.clear();
    RaftJson::extractNameValues("x=1&y=2;z=3", "=", "&", ";", pairs);
    TEST_ASSERT_EQUAL_MESSAGE(3, pairs.size(), "usualDelimPairs");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("z", pairs[2].name.c_str(), "usualDelimName");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("3", pairs[2].value.c_str(), "usualDelimValue");
}
