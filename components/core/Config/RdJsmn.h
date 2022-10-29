/*
 * MIT License
 *
 * Copyright (c) 2010 Serge Zaitsev
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// JSON parser - based on original https://github.com/zserge/jsmn
//
// Rob Dobson 2017-2022
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stddef.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef JSMN_STATIC
#define JSMN_API static
#else
#define JSMN_API extern
#endif

#define JSMN_PARENT_LINKS 1
#define JSMN_STRICT 1

  /**
   * JSON type identifier. Basic types are:
   * 	o Object
   * 	o Array
   * 	o String
   * 	o Other primitive: number, boolean (true/false) or null
   */
  typedef enum
  {
    RD_JSMN_UNDEFINED = 0,
    RD_JSMN_OBJECT = 1 << 0,
    RD_JSMN_ARRAY = 1 << 1,
    RD_JSMN_STRING = 1 << 2,
    RD_JSMN_PRIMITIVE = 1 << 3
  } rd_jsmntype_t;

  typedef enum
  {
    /* Not enough tokens were provided */
    RD_JSMN_ERROR_NOMEM = -1,
    /* Invalid character inside JSON string */
    RD_JSMN_ERROR_INVAL = -2,
    /* The string is not a full JSON packet, more bytes expected */
    RD_JSMN_ERROR_PART = -3,
    /* Everything was fine */
    RD_JSMN_SUCCESS = 0
  } rd_jsmnerr_t;

  /**
   * JSON token description.
   * type		type (object, array, string etc.)
   * start	start position in JSON data string
   * end		end position in JSON data string
   */
  typedef struct rd_jsmntok_t
  {
    rd_jsmntype_t type;
    int start;
    int end;
    int size;
#ifdef JSMN_PARENT_LINKS
    int parent;
#endif
  } rd_jsmntok_t;

  /**
   * JSON parser. Contains an array of token blocks available. Also stores
   * the string being parsed now and current position in that string
   */
  typedef struct
  {
    unsigned int pos;     /* offset in the JSON string */
    unsigned int toknext; /* next token to allocate */
    int toksuper;         /* superior token node, e.g. parent object or array */
  } rd_jsmn_parser;

  /**
   * Create JSON parser over an array of tokens
   */
  JSMN_API void rd_jsmn_init(rd_jsmn_parser *parser);

  /**
   * Run JSON parser. It parses a JSON data string into and array of tokens, each
   * describing
   * a single JSON object.
   * Returns count of parsed objects.
   */
  JSMN_API int rd_jsmn_parse(rd_jsmn_parser *parser, const char *js, const size_t len,
                          rd_jsmntok_t *tokens, const unsigned int num_tokens);

  static const uint32_t MAX_LOG_LONG_STR_LEN = 4096;
  void rd_jsmn_logLongStr(const char *headerMsg, const char *toLog, bool infoLevel);

#ifdef __cplusplus
}
#endif