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
#include <SpiramAwareAllocator.h>

// Define this to enable reformatting of JSON
//#define RDJSON_RECREATE_JSON 1

class RaftJson
{
public:
    // Constructor and destructor
    RaftJson(const char* pJsonStr, bool cache = true, int maxTokens = RDJSON_MAX_TOKENS);
    virtual ~RaftJson();    

    // Get values from JSON key/value pairs
    String getString(const char* dataPath, const char* defaultValue);
    double getDouble(const char* dataPath, double defaultValue);
    long getLong(const char* dataPath, long defaultValue);
    bool getBool(const char* dataPath, bool defaultValue);

    // Get array elements
    bool getArrayElems(const char *dataPath, std::vector<String>& strList);

    // Get keys
    bool getKeys(const char *dataPath, std::vector<String>& keysVector);

    // Name value pair handling methods
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

    // Static methods

    /**
     * getElement : Get location of element in JSON string
     * 
     * @param  {char*} dataPath       : path to element to return info about
     * @param  {int&} startPos        : [out] start position 
     * @param  {int&} strLen          : [out] length
     * @param  {jsmntype_t&} elemType : [out] element type
     * @param  {int&} elemSize        : [out] element size
     * @param  {char*} pSourceStr     : json string to search for element
     * @param  {void**} pCachedParseResult : [in/out] pointer to cached parse result
     * @param  {uint32_t*} pCachedParseNumTokens : [in/out] pointer to cached parse result
     * @return {bool}                 : true if element found
     * 
     * NOTE: If pCachedParseResult and pCachedParseNumTokens are provided then they must be a pointer to an already
     *      allocated void pointer and pointer to uint32_t respectively. In this case the parse result will be cached
     *      and the caller must call releaseCachedParseResult() with the same pointer as used when creating the cache
     *      to release the cached memory when all uses of this parse result are complete.
     */
    static bool getElement(const char* dataPath,
                        int& startPos, int& strLen,
                        jsmntype_t& elemType, int& elemSize,
                        const char* pSourceStr,
                        void** pCachedParseResult = nullptr,
                        uint32_t* pCachedParseNumTokens = nullptr);

    static const char* getElemTypeStr(jsmntype_t type);

    // Escape and unescape strings
    static void escapeString(String& strToEsc);
    static void unescapeString(String& strToUnEsc);

    // Validate JSON document
    static bool validateJson(const char* pSourceStr, int& numTokens);

    // Extract name value pairs from a string
    static void extractNameValues(const String& inStr, 
        const char* pNameValueSep, const char* pPairDelim, const char* pPairDelimAlt, 
        std::vector<RaftJson::NameValuePair>& nameValuePairs);

    // Extract name value pairs from a string
    const int RDJSON_MAX_TOKENS = 10000;
    jsmntok_t* parseJson(const char* jsonStr, int& numTokens,
                        int maxTokens = RDJSON_MAX_TOKENS);

    // Find key in JSON
    int findKeyInJson(const char* jsonOriginal, jsmntok_t tokens[],
                        unsigned int numTokens, const char* dataPath,
                        int& endTokenIdx,
                        jsmntype_t keyType = JSMN_UNDEFINED);

    // Check for boolean    
    static bool isBoolean(const char* pBuf, uint32_t bufLen, int &retValue);

    // Release cached parse result
    static void releaseCachedParseResult(void** pParseResult);

private:

    // Storage for document and cache of parse result
    class JSONDocAndCache
    {
    public:
        // JSON document
        std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> jsonDoc;

        // Override of String data to allow for data stored in flash (or other static location)
        // If this value is non-null then it is used instead of jsonDoc
        const char* pJsonDocStatic = nullptr;

        // Enable caching
        bool enableCaching = true;

        // Cached parse results
        mutable void* pCachedParseResult = nullptr;
        mutable uint32_t cachedParseNumTokens = 0;

        // Pointer to string that cache was based upon
        mutable const char* pCachedParseStr = nullptr;

        // Max number of tokens to allow when parsing
        uint16_t maxTokens = 10000;

        // Helper
        const char* getJsonDoc() const
        {
            if (pJsonDocStatic)
                return pJsonDocStatic;
            return jsonDoc.c_str();
        }
        uint32_t getJsonDocLen() const
        {
            if (pJsonDocStatic)
                return strlen(pJsonDocStatic);
            return jsonDoc.length();
        }
    };

public:

                        
    // // Get a string from the JSON
    // String getString(const char* dataPath,
    //                     const char* defaultValue, bool& isValid,
    //                     jsmntype_t& elemType, int& elemSize,
    //                     const char* pSourceStr);

    // // Alternate form of getString with fewer parameters
    // String getString(const char* dataPath, const char* defaultValue,
    //                     const char* pSourceStr, bool& isValid);

    // // Alternate form of getString with fewer parameters
    // String getString(const char* dataPath, const char* defaultValue,
    //                     const char* pSourceStr);

    // double getDouble(const char* dataPath,
    //                     double defaultValue, bool& isValid,
    //                     const char* pSourceStr);

    // double getDouble(const char* dataPath, double defaultValue,
    //                     const char* pSourceStr);

    // long getLong(const char* dataPath,
    //                     long defaultValue, bool& isValid,
    //                     const char* pSourceStr);

    // long getLong(const char* dataPath, long defaultValue, const char* pSourceStr);

    // bool getBool(const char* dataPath,
    //                     bool defaultValue, bool& isValid,
    //                     const char* pSourceStr);

    // bool getBool(const char* dataPath, bool defaultValue, const char* pSourceStr);

    // jsmntype_t getType(int& arrayLen, const char* pSourceStr);

    // const int MAX_KEYS_TO_RETURN = 100;
    // bool getKeys(const char *dataPath, std::vector<String>& keysVector, const char *pSourceStr);
    
    // bool getArrayElems(const char *dataPath, std::vector<String>& arrayElems, const char *pSourceStr);

    // // Get HTML query string from JSON
    // String getHTMLQueryFromJSON(const String& jsonStr);

    // size_t safeStringLen(const char* pSrc,
    //                             bool skipJSONWhitespace = false, size_t maxx = LONG_MAX);

    // void safeStringCopy(char* pDest, const char* pSrc,
    //                            size_t maxx, bool skipJSONWhitespace = false);

    // void debugDumpParseResult(const char* pSourceStr, jsmntok_t* pTokens, int numTokens);


    // char* safeStringDup(const char* pSrc, size_t maxx,
    //                     bool skipJSONWhitespace = false);
    // int findElemEnd(const char* jsonOriginal, jsmntok_t tokens[],
    //                     unsigned int numTokens, int startTokenIdx);
    // int findArrayElem(const char *jsonOriginal, jsmntok_t tokens[],
    //                     unsigned int numTokens, int startTokenIdx, 
    //                     int arrayElemIdx);

#ifdef RDJSON_RECREATE_JSON
    int recreateJson(const char* js, jsmntok_t* t,
                            size_t count, int indent, String& outStr);
    bool doPrint(const char* jsonStr);
#endif // RDJSON_RECREATE_JSON
};
