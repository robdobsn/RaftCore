/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// JSON field extraction
// Many of the methods here support a dataPath parameter. This uses a syntax like a much simplified XPath:
// [0] returns the 0th element of an array
// / is a separator of nodes
//
// Rob Dobson 2017-2022
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <stdlib.h>
#include <limits.h>
#include <RaftArduino.h>
#include "RaftJsmn.h"
#include <vector>

// Define this to enable reformatting of JSON
//#define RDJSON_RECREATE_JSON 1

namespace RaftJson {
    // Get location of element in JSON string
    bool getElement(const char* dataPath,
                        int& startPos, int& strLen,
                        jsmntype_t& elemType, int& elemSize,
                        const char* pSourceStr,
                        void** pCachedParseResult = nullptr,
                        uint32_t* pCachedParseNumTokens = nullptr);
    // Get a string from the JSON
    String getString(const char* dataPath,
                        const char* defaultValue, bool& isValid,
                        jsmntype_t& elemType, int& elemSize,
                        const char* pSourceStr);

    // Alternate form of getString with fewer parameters
    String getString(const char* dataPath, const char* defaultValue,
                        const char* pSourceStr, bool& isValid);

    // Alternate form of getString with fewer parameters
    String getString(const char* dataPath, const char* defaultValue,
                        const char* pSourceStr);

    double getDouble(const char* dataPath,
                        double defaultValue, bool& isValid,
                        const char* pSourceStr);

    double getDouble(const char* dataPath, double defaultValue,
                        const char* pSourceStr);

    long getLong(const char* dataPath,
                        long defaultValue, bool& isValid,
                        const char* pSourceStr);

    long getLong(const char* dataPath, long defaultValue, const char* pSourceStr);

    bool getBool(const char* dataPath,
                        bool defaultValue, bool& isValid,
                        const char* pSourceStr);

    bool getBool(const char* dataPath, bool defaultValue, const char* pSourceStr);

    const char* getElemTypeStr(jsmntype_t type);

    jsmntype_t getType(int& arrayLen, const char* pSourceStr);

    const int MAX_KEYS_TO_RETURN = 100;
    bool getKeys(const char *dataPath, std::vector<String>& keysVector, const char *pSourceStr);
    
    bool getArrayElems(const char *dataPath, std::vector<String>& arrayElems, const char *pSourceStr);

    // Name value pair
    struct NameValuePair
    {
    public:
        NameValuePair()
        {
        }
        NameValuePair(const String& name, const String& value)
        {
            this->name = name;
            this->value = value;
        }
        String name;
        String value;
    };

    // Get JSON from NameValue pairs
    String getJSONFromNVPairs(std::vector<NameValuePair>& nameValuePairs, bool includeOuterBraces);

    // Get HTML query string from JSON
    String getHTMLQueryFromJSON(const String& jsonStr);

    size_t safeStringLen(const char* pSrc,
                                bool skipJSONWhitespace = false, size_t maxx = LONG_MAX);

    void safeStringCopy(char* pDest, const char* pSrc,
                               size_t maxx, bool skipJSONWhitespace = false);

    void debugDumpParseResult(const char* pSourceStr, jsmntok_t* pTokens, int numTokens);

    void escapeString(String& strToEsc);

    void unescapeString(String& strToUnEsc);

    int findKeyInJson(const char* jsonOriginal, jsmntok_t tokens[],
                        unsigned int numTokens, const char* dataPath,
                        int& endTokenIdx,
                        jsmntype_t keyType = JSMN_UNDEFINED);

    const int RDJSON_MAX_TOKENS = 10000;
    jsmntok_t* parseJson(const char* jsonStr, int& numTokens,
                        int maxTokens = RDJSON_MAX_TOKENS);
    // Validate JSON
    bool validateJson(const char* pSourceStr, int& numTokens);
    char* safeStringDup(const char* pSrc, size_t maxx,
                        bool skipJSONWhitespace = false);
    int findElemEnd(const char* jsonOriginal, jsmntok_t tokens[],
                        unsigned int numTokens, int startTokenIdx);
    int findArrayElem(const char *jsonOriginal, jsmntok_t tokens[],
                        unsigned int numTokens, int startTokenIdx, 
                        int arrayElemIdx);

     // Extract name value pairs from a string
    void extractNameValues(const String& inStr, 
        const char* pNameValueSep, const char* pPairDelim, const char* pPairDelimAlt, 
        std::vector<RaftJson::NameValuePair>& nameValuePairs);

    // Check for boolean    
    bool isBoolean(const char* pBuf, uint32_t bufLen, int &retValue);

    // Release cached parse result
    void releaseCachedParseResult(void** pParseResult);

#ifdef RDJSON_RECREATE_JSON
    int recreateJson(const char* js, jsmntok_t* t,
                            size_t count, int indent, String& outStr);
    bool doPrint(const char* jsonStr);
#endif // RDJSON_RECREATE_JSON
};
