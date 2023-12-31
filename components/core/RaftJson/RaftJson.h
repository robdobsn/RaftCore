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
    RaftJson(const char* pJsonStr, bool makeCopy = true, bool cacheParseResults = true, int16_t maxTokens = RDJSON_MAX_TOKENS);
    virtual ~RaftJson();    

    // Get values from JSON key/value pairs
    String getString(const char* dataPath, const char* defaultValue)
    {
        return getString(dataPath, defaultValue, nullptr, &_docAndCache);
    }
    double getDouble(const char* dataPath, double defaultValue)
    {
        return getDouble(dataPath, defaultValue, nullptr, &_docAndCache);
    }
    long getLong(const char* dataPath, long defaultValue)
    {
        return getLong(dataPath, defaultValue, nullptr, &_docAndCache);
    }
    bool getBool(const char* dataPath, bool defaultValue)
    {
        return getBool(dataPath, defaultValue, nullptr, &_docAndCache);
    }

    // Get array elements
    bool getArrayElems(const char *dataPath, std::vector<String>& strList)
    {
        return getArrayElems(dataPath, strList, nullptr, &_docAndCache);
    }

    // Get keys
    bool getKeys(const char *dataPath, std::vector<String>& keysVector)
    {
        return getKeys(dataPath, keysVector, nullptr, &_docAndCache);
    }

    // Static methods

    static String getString(const char* pDataPath, const char* defaultValue, const char* pSourceStr, 
            const JSONDocAndCache* pDocAndCache = nullptr)
    {
        
    }
    static double getDouble(const char* pDataPath, double defaultValue, const char* pSourceStr, 
            const JSONDocAndCache* pDocAndCache = nullptr);
    static long getLong(const char* pDataPath, long defaultValue, const char* pSourceStr,
            const JSONDocAndCache* pDocAndCache = nullptr);
    static bool getBool(const char* pDataPath, bool defaultValue, const char* pSourceStr,
            const JSONDocAndCache* pDocAndCache = nullptr);

    // Get array elements
    static bool getArrayElems(const char *pDataPath, std::vector<String>& strList, const char* pSourceStr, 
            const JSONDocAndCache* pDocAndCache = nullptr);

    // Get keys (static)
    static bool getKeys(const char *pDataPath, std::vector<String>& keysVector, const char* pSourceStr, 
            const JSONDocAndCache* pDocAndCache = nullptr)
    {
        // Find the element in the JSON using the pDataPath
        int startPos = 0, strLen = 0;
        jsmntype_t elemType = JSMN_UNDEFINED;
        int elemSize = 0;
        if (!getElement(pDataPath, startPos, strLen, elemType, elemSize, 
                    &keysVector,
                    nullptr,
                    pSourceStr,
                    pDocAndCache))
            return false;
        return elemType == JSMN_OBJECT;        
    }

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
    static String getJSONFromNVPairs(std::vector<NameValuePair>& nameValuePairs, bool includeOuterBraces);

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
    static constexpr int RDJSON_MAX_TOKENS = 10000;
    static jsmntok_t* parseJson(const char* jsonStr, int& numTokens,
                        int maxTokens = RDJSON_MAX_TOKENS);

    // Find key in JSON
    static int findKeyInJson(const char* jsonOriginal, jsmntok_t tokens[],
                        unsigned int numTokens, const char* dataPath,
                        int& endTokenIdx,
                        jsmntype_t keyType = JSMN_UNDEFINED);

    // Check for boolean    
    static bool isBoolean(const char* pBuf, uint32_t bufLen, int &retValue);

    /**
     * getElement : Get location of element in JSON string
     * 
     * @param  {char*} pDataPath                        : path to element to return info about
     * @param  {int&} startPos                          : [out] start position 
     * @param  {int&} strLen                            : [out] length
     * @param  {jsmntype_t&} elemType                   : [out] element type
     * @param  {int&} elemSize                          : [out] element size
     * @param  {std::vector<String>*} pKeysVector       : [out] pointer to vector to receive keys (maybe nullptr)
     * @param  {std::vector<String>*} pArrayElems       : [out] pointer to vector to receive array elements (maybe nullptr)
     * @param  {char*} pSourceStr                       : json string to search for element (maybe nullptr if pDocAndCache is provided)
     * @param  {const JSONDocAndCache*} pDocAndCache    : [in] pointer to JSON document and cache (maybe nullptr if pSourceStr is provided)
     * @return {bool}                 : true if element found
     * 
     * NOTE: If pSourceStr is provided then pDocAndCache must be nullptr and vice versa. If pSourceStr is provided then
     *      the parse result is not cached. If pDocAndCache is provided then the parse result maybe cached based on the 
     *     cacheParseResults flag in the JSONDocAndCache object (if the parse result is not already cached).
     */
    static bool getElement(const char *pDataPath,
                        int &startPos, int &strLen,
                        jsmntype_t &elemType, int &elemSize,
                        std::vector<String>* pKeysVector,
                        std::vector<String>* pArrayElems,
                        const char *pSourceStr
                        const JSONDocAndCache* pDocAndCache = nullptr);

    static const char* getElemTypeStr(jsmntype_t type)
    {
        switch (type)
        {
        case JSMN_PRIMITIVE:
            return "PRIMITIVE";
        case JSMN_STRING:
            return "STRING";
        case JSMN_OBJECT:
            return "OBJECT";
        case JSMN_ARRAY:
            return "ARRAY";
        case JSMN_UNDEFINED:
            return "UNDEFINED";
        }
        return "UNKNOWN";
    }



    // Storage for document and cache of parse result
    class JSONDocAndCache
    {
    public:

        // Destructor
        ~JSONDocAndCache()
        {
            // Release cached parse result
            releaseCachedParseResult();
        }

        // Set JSON document
        void setJsonDoc(const char* pJsonStr, bool makeCopy)
        {
            // Release cached parse result
            releaseCachedParseResult();

            // Set JSON document
            if (makeCopy)
            {
                jsonDoc = std::vector<char, SpiramAwareAllocator<char>>(pJsonStr, pJsonStr + strlen(pJsonStr));
                jsonDoc.push_back(0);
            }
            else
            {
                pJsonDocStatic = pJsonStr;
                jsonDocStaticLen = strlen(pJsonStr);
            }
        }

        // Release cached parse result
        void releaseCachedParseResult()
        {
            if (pCachedParseResult)
            {
                delete[] pCachedParseResult;
                pCachedParseResult = nullptr;
                cachedParseNumTokens = 0;
            }
        }

        // Set parse parameters
        void setParseParams(bool cacheParseResults, uint16_t maxTokens)
        {
            this->cacheParseResults = cacheParseResults;
            this->maxTokens = maxTokens;
        }

        // Access to JSON document
        const char* getJsonDoc() const
        {
            if (pJsonDocStatic)
                return pJsonDocStatic;
            return jsonDoc.data();
        }
        uint32_t getJsonDocLen() const
        {
            if (pJsonDocStatic)
                return strlen(pJsonDocStatic);
            return jsonDoc.size();
        }

        // JSON document
        std::vector<char, SpiramAwareAllocator<char>> jsonDoc;

        // Override of String data to allow for data stored in flash (or other static location)
        // If this value is non-null then it is used instead of jsonDoc
        const char* pJsonDocStatic = nullptr;
        uint32_t jsonDocStaticLen = 0;

        // Caching of parse results
        bool cacheParseResults = true;

        // Cached parse results
        mutable jsmntok_t* pCachedParseResult = nullptr;
        mutable uint32_t cachedParseNumTokens = 0;

        // Max number of tokens to allow when parsing
        uint16_t maxTokens = RDJSON_MAX_TOKENS;
    };

private:
    // JSON document and cache
    JSONDocAndCache _docAndCache;
                        
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
    static int findElemEnd(const char* jsonOriginal, jsmntok_t tokens[],
                        unsigned int numTokens, int startTokenIdx);
    static int findArrayElem(const char *jsonOriginal, jsmntok_t tokens[],
                        unsigned int numTokens, int startTokenIdx, 
                        int arrayElemIdx);

#ifdef RDJSON_RECREATE_JSON
    int recreateJson(const char* js, jsmntok_t* t,
                            size_t count, int indent, String& outStr);
    bool doPrint(const char* jsonStr);
#endif // RDJSON_RECREATE_JSON
};
