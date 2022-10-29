/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Unit tests of JSON parser
// Original https://github.com/zserge/jsmn
//
// Rob Dobson 2017-2022
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <RdJsmn.h>
#include <Logger.h>
#include "unity.h"

static const char* MODULE_PREFIX = "ExprUnitTest";

static const int ERROR_PARSE_RESULT_ERROR = 10;
static const int ERROR_TOKEN_TYPE_ERROR = 11;
static const int ERROR_TOKEN_START_ERROR = 12;
static const int ERROR_TOKEN_END_ERROR = 13;
static const int ERROR_TOKEN_SIZE_ERROR = 14;
static const int ERROR_TOKEN_VALUE_ERROR = 15;

#ifndef RD_JSMN_SUCCESS
#define RD_JSMN_SUCCESS 0
#endif

static int vtokeq(const char *s, rd_jsmntok_t *t, unsigned long numtok,
                  va_list ap) {
  if (numtok > 0) {
    unsigned long i;
    int start, end, size;
    rd_jsmntype_t type;
    char *value;

    size = -1;
    value = NULL;
    for (i = 0; i < numtok; i++) {
      type = (rd_jsmntype_t)va_arg(ap, int);
      if (type == RD_JSMN_STRING) {
        value = va_arg(ap, char *);
        size = va_arg(ap, int);
        start = end = -1;
      } else if (type == RD_JSMN_PRIMITIVE) {
        value = va_arg(ap, char *);
        start = end = size = -1;
      } else {
        start = va_arg(ap, int);
        end = va_arg(ap, int);
        size = va_arg(ap, int);
        value = NULL;
      }
      if (t[i].type != type) {
        LOG_I(MODULE_PREFIX, "token %lu type is %d, not %d", i, t[i].type, type);
        return ERROR_TOKEN_TYPE_ERROR;
      }
      if (start != -1 && end != -1) {
        if (t[i].start != start) {
          LOG_I(MODULE_PREFIX, "token %lu start is %d, not %d", i, t[i].start, start);
          return ERROR_TOKEN_START_ERROR;
        }
        if (t[i].end != end) {
          LOG_I(MODULE_PREFIX, "token %lu end is %d, not %d", i, t[i].end, end);
          return ERROR_TOKEN_END_ERROR;
        }
      }
      if (size != -1 && t[i].size != size) {
        LOG_I(MODULE_PREFIX, "token %lu size is %d, not %d", i, t[i].size, size);
        return ERROR_TOKEN_SIZE_ERROR;
      }

      if (s != NULL && value != NULL) {
        const char *p = s + t[i].start;
        if (strlen(value) != (unsigned long)(t[i].end - t[i].start) ||
            strncmp(p, value, t[i].end - t[i].start) != 0) {
          LOG_I(MODULE_PREFIX, "token %lu value is %.*s, not %s\n", i, t[i].end - t[i].start,
                 s + t[i].start, value);
          return ERROR_TOKEN_VALUE_ERROR;
        }
      }
    }
  }
  return RD_JSMN_SUCCESS;
}

static int tokeq(const char *s, rd_jsmntok_t *tokens, unsigned long numtok, ...) {
  int ok;
  va_list args;
  va_start(args, numtok);
  ok = vtokeq(s, tokens, numtok, args);
  va_end(args);
  return ok;
}

static int parse(const char *s, int status, unsigned long numtok, ...) {
  rd_jsmntok_t *t = (rd_jsmntok_t*)malloc(numtok * sizeof(rd_jsmntok_t));

  rd_jsmn_parser p;
  rd_jsmn_init(&p);
  int r = rd_jsmn_parse(&p, s, strlen(s), t, numtok);
  if (r != status) {
    LOG_I(MODULE_PREFIX, "status is %d, not %d\n", r, status);
    return ERROR_PARSE_RESULT_ERROR;
  }

  int rslt = RD_JSMN_SUCCESS;
  if (status >= 0) {
    va_list args;
    va_start(args, numtok);
    rslt = vtokeq(s, t, numtok, args);
    va_end(args);
  }
  free(t);
  return rslt;
}

TEST_CASE("test_empty", "[jsmn]")
{
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{}", 1, 1, RD_JSMN_OBJECT, 0, 2, 0), "empty object");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("[]", 1, 1, RD_JSMN_ARRAY, 0, 2, 0), "empty array");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("[{},{}]", 3, 3, RD_JSMN_ARRAY, 0, 7, 2, RD_JSMN_OBJECT, 1, 3, 0,
              RD_JSMN_OBJECT, 4, 6, 0), "empty array of empty objects");
}

TEST_CASE("test_object", "[jsmn]")
{
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"a\":0}", 3, 3, RD_JSMN_OBJECT, 0, 7, 1, RD_JSMN_STRING, "a", 1,
              RD_JSMN_PRIMITIVE, "0"), "member primitive");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"a\":[]}", 3, 3, RD_JSMN_OBJECT, 0, 8, 1, RD_JSMN_STRING, "a", 1,
              RD_JSMN_ARRAY, 5, 7, 0), "member array");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"a\":{},\"b\":{}}", 5, 5, RD_JSMN_OBJECT, -1, -1, 2, RD_JSMN_STRING,
              "a", 1, RD_JSMN_OBJECT, -1, -1, 0, RD_JSMN_STRING, "b", 1, RD_JSMN_OBJECT,
              -1, -1, 0), "member object");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\n \"Day\": 26,\n \"Month\": 9,\n \"Year\": 12\n }", 7, 7,
              RD_JSMN_OBJECT, -1, -1, 3, RD_JSMN_STRING, "Day", 1, RD_JSMN_PRIMITIVE,
              "26", RD_JSMN_STRING, "Month", 1, RD_JSMN_PRIMITIVE, "9", RD_JSMN_STRING,
              "Year", 1, RD_JSMN_PRIMITIVE, "12"), "member primitives");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"a\": 0, \"b\": \"c\"}", 5, 5, RD_JSMN_OBJECT, -1, -1, 2,
              RD_JSMN_STRING, "a", 1, RD_JSMN_PRIMITIVE, "0", RD_JSMN_STRING, "b", 1,
              RD_JSMN_STRING, "c", 0), "member mixed");
}

TEST_CASE("test_strict", "[jsmn]")
{
#ifdef JSMN_STRICT
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"a\"\n0}", RD_JSMN_ERROR_INVAL, 3), "strict1");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"a\", 0}", RD_JSMN_ERROR_INVAL, 3), "strict2");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"a\": {2}}", RD_JSMN_ERROR_INVAL, 3), "strict3");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"a\": {2: 3}}", RD_JSMN_ERROR_INVAL, 3), "strict4");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"a\": {\"a\": 2 3}}", RD_JSMN_ERROR_INVAL, 5), "strict5");
/* FIXME */
/*TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"a\"}", RD_JSMN_ERROR_INVAL, 2));*/
/*TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"a\": 1, \"b\"}", RD_JSMN_ERROR_INVAL, 4));*/
/*TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"a\",\"b\":1}", RD_JSMN_ERROR_INVAL, 4));*/
/*TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"a\":1,}", RD_JSMN_ERROR_INVAL, 4));*/
/*TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"a\":\"b\":\"c\"}", RD_JSMN_ERROR_INVAL, 4));*/
/*TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{,}", RD_JSMN_ERROR_INVAL, 4));*/
#endif
}

TEST_CASE("test_array", "[jsmn]")
{
  /* FIXME */
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("[10}", RD_JSMN_ERROR_INVAL, 3), "incorrect brackets");
  // TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("[1,,3]", RD_JSMN_ERROR_INVAL, 3), "missing value");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("[10]", 2, 2, RD_JSMN_ARRAY, -1, -1, 1, RD_JSMN_PRIMITIVE, "10"), "array of primitive");
  // TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"a\": 1]", RD_JSMN_ERROR_INVAL, 3), "unmatched brackets");
  /* FIXME */
  /*TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("[\"a\": 1]", RD_JSMN_ERROR_INVAL, 3));*/
}

TEST_CASE("test_primitives", "[jsmn]")
{
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"boolVar\" : true }", 3, 3, RD_JSMN_OBJECT, -1, -1, 1,
              RD_JSMN_STRING, "boolVar", 1, RD_JSMN_PRIMITIVE, "true"), "boolVar true");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"boolVar\" : false }", 3, 3, RD_JSMN_OBJECT, -1, -1, 1,
              RD_JSMN_STRING, "boolVar", 1, RD_JSMN_PRIMITIVE, "false"), "boolVar false");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"nullVar\" : null }", 3, 3, RD_JSMN_OBJECT, -1, -1, 1,
              RD_JSMN_STRING, "nullVar", 1, RD_JSMN_PRIMITIVE, "null"), "nullVar");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"intVar\" : 12}", 3, 3, RD_JSMN_OBJECT, -1, -1, 1, RD_JSMN_STRING,
              "intVar", 1, RD_JSMN_PRIMITIVE, "12"), "intVar 12");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"floatVar\" : 12.345}", 3, 3, RD_JSMN_OBJECT, -1, -1, 1,
              RD_JSMN_STRING, "floatVar", 1, RD_JSMN_PRIMITIVE, "12.345"), "floatVar 12.345");
}

TEST_CASE("test_strings", "[jsmn]")
{
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"strVar\" : \"hello world\"}", 3, 3, RD_JSMN_OBJECT, -1, -1, 1,
              RD_JSMN_STRING, "strVar", 1, RD_JSMN_STRING, "hello world", 0), "strVar hello world");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"strVar\" : \"escapes: \\/\\r\\n\\t\\b\\f\\\"\\\\\"}", 3, 3,
              RD_JSMN_OBJECT, -1, -1, 1, RD_JSMN_STRING, "strVar", 1, RD_JSMN_STRING,
              "escapes: \\/\\r\\n\\t\\b\\f\\\"\\\\", 0), "strVar escapes");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"strVar\": \"\"}", 3, 3, RD_JSMN_OBJECT, -1, -1, 1, RD_JSMN_STRING,
              "strVar", 1, RD_JSMN_STRING, "", 0), "strVar empty");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"a\":\"\\uAbcD\"}", 3, 3, RD_JSMN_OBJECT, -1, -1, 1, RD_JSMN_STRING,
              "a", 1, RD_JSMN_STRING, "\\uAbcD", 0), "strVar backslashU");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"a\":\"str\\u0000\"}", 3, 3, RD_JSMN_OBJECT, -1, -1, 1,
              RD_JSMN_STRING, "a", 1, RD_JSMN_STRING, "str\\u0000", 0), "strVar nullterminated");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"a\":\"\\uFFFFstr\"}", 3, 3, RD_JSMN_OBJECT, -1, -1, 1,
              RD_JSMN_STRING, "a", 1, RD_JSMN_STRING, "\\uFFFFstr", 0), "strVar maxUshort");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"a\":[\"\\u0280\"]}", 4, 4, RD_JSMN_OBJECT, -1, -1, 1,
              RD_JSMN_STRING, "a", 1, RD_JSMN_ARRAY, -1, -1, 1, RD_JSMN_STRING,
              "\\u0280", 0), "strVar backslashU280");

  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"a\":\"str\\uFFGFstr\"}", RD_JSMN_ERROR_INVAL, 3), "strVar muckedup1");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{\"a\":\"str\\u@FfF\"}", RD_JSMN_ERROR_INVAL, 3), "strVar muckedup2");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse("{{\"a\":[\"\\u028\"]}", RD_JSMN_ERROR_INVAL, 4), "strVar muckedup3");
}

TEST_CASE("test_partial_string", "[jsmn]")
{
  int r;
  unsigned long i;
  rd_jsmn_parser p;
  rd_jsmntok_t tok[5];
  const char *js = "{\"x\": \"va\\\\ue\", \"y\": \"value y\"}";

  rd_jsmn_init(&p);
  for (i = 1; i <= strlen(js); i++) {
    r = rd_jsmn_parse(&p, js, i, tok, sizeof(tok) / sizeof(tok[0]));
    if (i == strlen(js)) {
      TEST_ASSERT_MESSAGE(r == 5, "strlen fail 5");
      TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == tokeq(js, tok, 5, RD_JSMN_OBJECT, -1, -1, 2, RD_JSMN_STRING, "x", 1,
                  RD_JSMN_STRING, "va\\\\ue", 0, RD_JSMN_STRING, "y", 1, RD_JSMN_STRING,
                  "value y", 0), "weirdstring");
    } else {
      TEST_ASSERT_MESSAGE(r == RD_JSMN_ERROR_PART,"strlen fail other");
    }
  }
}

TEST_CASE("test_partial_array", "[jsmn]")
{
#ifdef JSMN_STRICT
  int r;
  unsigned long i;
  rd_jsmn_parser p;
  rd_jsmntok_t tok[10];
  const char *js = "[ 1, true, [123, \"hello\"]]";

  rd_jsmn_init(&p);
  for (i = 1; i <= strlen(js); i++) {
    r = rd_jsmn_parse(&p, js, i, tok, sizeof(tok) / sizeof(tok[0]));
    if (i == strlen(js)) {
      TEST_ASSERT_MESSAGE(r == 6, "strlen 6");
      TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == tokeq(js, tok, 6, RD_JSMN_ARRAY, -1, -1, 3, RD_JSMN_PRIMITIVE, "1",
                  RD_JSMN_PRIMITIVE, "true", RD_JSMN_ARRAY, -1, -1, 2, RD_JSMN_PRIMITIVE,
                  "123", RD_JSMN_STRING, "hello", 0), "not hello");
    } else {
      TEST_ASSERT_MESSAGE(r == RD_JSMN_ERROR_PART, "strlen other 6");
    }
  }
#endif
}

TEST_CASE("test_array_nomem", "[jsmn]")
{
  int i;
  int r;
  rd_jsmn_parser p;
  rd_jsmntok_t toksmall[10], toklarge[10];
  const char *js;

  js = "  [ 1, true, [123, \"hello\"]]";

  for (i = 0; i < 6; i++) {
    rd_jsmn_init(&p);
    memset(toksmall, 0, sizeof(toksmall));
    memset(toklarge, 0, sizeof(toklarge));
    r = rd_jsmn_parse(&p, js, strlen(js), toksmall, i);
    TEST_ASSERT_MESSAGE(r == RD_JSMN_ERROR_NOMEM, "nomem");

    memcpy(toklarge, toksmall, sizeof(toksmall));

    r = rd_jsmn_parse(&p, js, strlen(js), toklarge, 10);
    TEST_ASSERT_MESSAGE(r >= 0, "parse result > 0");
    TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == tokeq(js, toklarge, 4, RD_JSMN_ARRAY, -1, -1, 3, RD_JSMN_PRIMITIVE, "1",
                RD_JSMN_PRIMITIVE, "true", RD_JSMN_ARRAY, -1, -1, 2, RD_JSMN_PRIMITIVE,
                "123", RD_JSMN_STRING, "hello", 0), "gotok");
  }
}

TEST_CASE("test_unquoted_keys", "[jsmn]")
{
#ifndef JSMN_STRICT
  int r;
  rd_jsmn_parser p;
  rd_jsmntok_t tok[10];
  const char *js;

  rd_jsmn_init(&p);
  js = "key1: \"value\"\nkey2 : 123";

  r = rd_jsmn_parse(&p, js, strlen(js), tok, 10);
  TEST_ASSERT_MESSAGE(r >= 0, "parse unquoted fail");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == tokeq(js, tok, 4, RD_JSMN_PRIMITIVE, "key1", RD_JSMN_STRING, "value", 0,
              RD_JSMN_PRIMITIVE, "key2", RD_JSMN_PRIMITIVE, "123"), "parse unquoted fail2");
#endif
}

TEST_CASE("test_issue_22", "[jsmn]")
{
  int r;
  rd_jsmn_parser p;
  rd_jsmntok_t tokens[128];
  const char *js;

  js =
      "{ \"height\":10, \"layers\":[ { \"data\":[6,6], \"height\":10, "
      "\"name\":\"Calque de Tile 1\", \"opacity\":1, \"type\":\"tilelayer\", "
      "\"visible\":true, \"width\":10, \"x\":0, \"y\":0 }], "
      "\"orientation\":\"orthogonal\", \"properties\": { }, \"tileheight\":32, "
      "\"tilesets\":[ { \"firstgid\":1, \"image\":\"..\\/images\\/tiles.png\", "
      "\"imageheight\":64, \"imagewidth\":160, \"margin\":0, "
      "\"name\":\"Tiles\", "
      "\"properties\":{}, \"spacing\":0, \"tileheight\":32, \"tilewidth\":32 "
      "}], "
      "\"tilewidth\":32, \"version\":1, \"width\":10 }";
  rd_jsmn_init(&p);
  r = rd_jsmn_parse(&p, js, strlen(js), tokens, 128);
  TEST_ASSERT_MESSAGE(r >= 0, "issue22");
}

TEST_CASE("test_issue_27", "[jsmn]")
{
  const char *js =
      "{ \"name\" : \"Jack\", \"age\" : 27 } { \"name\" : \"Anna\", ";
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse(js, RD_JSMN_ERROR_PART, 8), "issue27");
}

TEST_CASE("test_input_length", "[jsmn]")
{
  const char *js;
  int r;
  rd_jsmn_parser p;
  rd_jsmntok_t tokens[10];

  js = "{\"a\": 0}garbage";

  rd_jsmn_init(&p);
  r = rd_jsmn_parse(&p, js, 8, tokens, 10);
  TEST_ASSERT_MESSAGE(r == 3, "garbage1");
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == tokeq(js, tokens, 3, RD_JSMN_OBJECT, -1, -1, 1, RD_JSMN_STRING, "a", 1,
              RD_JSMN_PRIMITIVE, "0"), "garbage2");
}

TEST_CASE("test_count", "[jsmn]")
{
  rd_jsmn_parser p;
  const char *js;

  js = "{}";
  rd_jsmn_init(&p);
  TEST_ASSERT_MESSAGE(rd_jsmn_parse(&p, js, strlen(js), NULL, 0) == 1, "count1");

  js = "[]";
  rd_jsmn_init(&p);
  TEST_ASSERT_MESSAGE(rd_jsmn_parse(&p, js, strlen(js), NULL, 0) == 1, "count2");

  js = "[[]]";
  rd_jsmn_init(&p);
  TEST_ASSERT_MESSAGE(rd_jsmn_parse(&p, js, strlen(js), NULL, 0) == 2, "count3");

  js = "[[], []]";
  rd_jsmn_init(&p);
  TEST_ASSERT_MESSAGE(rd_jsmn_parse(&p, js, strlen(js), NULL, 0) == 3, "count4");

  js = "[[], []]";
  rd_jsmn_init(&p);
  TEST_ASSERT_MESSAGE(rd_jsmn_parse(&p, js, strlen(js), NULL, 0) == 3, "count5");

  js = "[[], [[]], [[], []]]";
  rd_jsmn_init(&p);
  TEST_ASSERT_MESSAGE(rd_jsmn_parse(&p, js, strlen(js), NULL, 0) == 7, "count6");

  js = "[\"a\", [[], []]]";
  rd_jsmn_init(&p);
  TEST_ASSERT_MESSAGE(rd_jsmn_parse(&p, js, strlen(js), NULL, 0) == 5, "count7");

  js = "[[], \"[], [[]]\", [[]]]";
  rd_jsmn_init(&p);
  TEST_ASSERT_MESSAGE(rd_jsmn_parse(&p, js, strlen(js), NULL, 0) == 5, "count8");

  js = "[1, 2, 3]";
  rd_jsmn_init(&p);
  TEST_ASSERT_MESSAGE(rd_jsmn_parse(&p, js, strlen(js), NULL, 0) == 4, "count9");

  js = "[1, 2, [3, \"a\"], null]";
  rd_jsmn_init(&p);
  TEST_ASSERT_MESSAGE(rd_jsmn_parse(&p, js, strlen(js), NULL, 0) == 7, "count10");
}

TEST_CASE("test_nonstrict", "[jsmn]")
{
#ifndef JSMN_STRICT
  const char *js;
  js = "a: 0garbage";
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse(js, 2, 2, RD_JSMN_PRIMITIVE, "a", RD_JSMN_PRIMITIVE, "0garbage"), "nonstrict garbage");

  js = "Day : 26\nMonth : Sep\n\nYear: 12";
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse(js, 6, 6, RD_JSMN_PRIMITIVE, "Day", RD_JSMN_PRIMITIVE, "26",
              RD_JSMN_PRIMITIVE, "Month", RD_JSMN_PRIMITIVE, "Sep", RD_JSMN_PRIMITIVE,
              "Year", RD_JSMN_PRIMITIVE, "12"), "nonstrict sep");

  /* nested {s don't cause a parse error. */
  js = "\"key {1\": 1234";
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse(js, 2, 2, RD_JSMN_STRING, "key {1", 1, RD_JSMN_PRIMITIVE, "1234"), "nonstrict keyerr");

#endif
}

TEST_CASE("test_unmatched_brackets", "[jsmn]")
{
  const char *js;
  js = "{\"key 1\": 1234";
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse(js, RD_JSMN_ERROR_PART, 3), "unmatched 1");
  js = "{\"key {1\": 1234}";
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse(js, 3, 3, RD_JSMN_OBJECT, 0, 16, 1, RD_JSMN_STRING, "key {1", 1,
              RD_JSMN_PRIMITIVE, "1234"), "unmatched 2");
  js = "{\"key 1\":{\"key 2\": 1234}";
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse(js, RD_JSMN_ERROR_PART, 5), "unmatched 3");

  // The following tests require the latest version of jsmn and this is not included in the esp-idf currently
  js = "\"key 1\": 1234}";
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse(js, RD_JSMN_ERROR_INVAL, 2), "unmatched 4");
  js = "\"key 1\"}: 1234";
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse(js, RD_JSMN_ERROR_INVAL, 3), "unmatched 5");
  js = "{\"key 1\": 1234}}";
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse(js, RD_JSMN_ERROR_INVAL, 3), "unmatched 6");
}

TEST_CASE("test_object2", "[jsmn]")
{
  const char *js;

  js = "{\"key\": 1}";
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse(js, 3, 3, RD_JSMN_OBJECT, 0, 10, 1, RD_JSMN_STRING, "key", 1,
              RD_JSMN_PRIMITIVE, "1"), "object1");
#ifdef JSMN_STRICT
  js = "{true: 1}";
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse(js, RD_JSMN_ERROR_INVAL, 3), "object2");
  js = "{1: 1}";
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse(js, RD_JSMN_ERROR_INVAL, 3), "object3");
  js = "{{\"key\": 1}: 2}";
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse(js, RD_JSMN_ERROR_INVAL, 5), "object4");
  js = "{[1,2]: 2}";
  TEST_ASSERT_MESSAGE(RD_JSMN_SUCCESS == parse(js, RD_JSMN_ERROR_INVAL, 5), "object5");
#endif
}

// int main(void) {
//   test(test_empty, "test for a empty JSON objects/arrays");
//   test(test_object, "test for a JSON objects");
//   test(test_array, "test for a JSON arrays");
//   test(test_primitive, "test primitive JSON data types");
//   test(test_string, "test string JSON data types");

//   test(test_partial_string, "test partial JSON string parsing");
//   test(test_partial_array, "test partial array reading");
//   test(test_array_nomem, "test array reading with a smaller number of tokens");
//   test(test_unquoted_keys, "test unquoted keys (like in JavaScript)");
//   test(test_input_length, "test strings that are not null-terminated");
//   test(test_issue_22, "test issue #22");
//   test(test_issue_27, "test issue #27");
//   test(test_count, "test tokens count estimation");
//   test(test_nonstrict, "test for non-strict mode");
//   test(test_unmatched_brackets, "test for unmatched brackets");
//   test(test_object_key, "test for key type");
//   printf("\nPASSED: %d\nFAILED: %d\n", test_passed, test_failed);
//   return (test_failed > 0);
// }