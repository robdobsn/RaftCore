/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftJson_jsmn - JSON parser and field extractor
// Many of the methods here support a pDataPath parameter. This uses a syntax like a much simplified XPath:
// [0] returns the 0th element of an array
// / is a separator of nodes
//
// Rob Dobson 2017-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <vector>
#include <string>
#include <stdlib.h>
#include <limits.h>
#include "RaftArduino.h"
#include "RaftJsmn.h"
#include "RaftJsonIF.h"
#include "SpiramAwareAllocator.h"

// Define this to enable reformatting of JSON
//#define RDJSON_RECREATE_JSON 1

class RaftJson_jsmn : public RaftJsonIF
{
public:
    // Constructor from string with all options
    // Note that when makeCopy is false the string pointer must remain valid for the lifetime of this object
    // This option is provided to avoid copying strings in flash memory - please don't use it in other cases
    RaftJson_jsmn(const char* pJsonStr, bool makeCopy = true, bool cacheParseResults = true, int16_t maxTokens = RAFT_JSON_MAX_TOKENS);

    // Constructor from arduino String with all options
    RaftJson_jsmn(const String& jsonStr, bool cacheParseResults = true, int16_t maxTokens = RAFT_JSON_MAX_TOKENS);

    // Destructor
    virtual ~RaftJson_jsmn();

    // Assignment from string types
    RaftJson_jsmn& operator=(const char* pJsonStr);
    RaftJson_jsmn& operator=(const String& jsonStr);
    RaftJson_jsmn& operator=(const std::string& jsonStr);

    // Get values from JSON key/value pairs
    virtual String getString(const char* pDataPath, const char* defaultValue) const override
    {
        return getString(nullptr, pDataPath, defaultValue, nullptr, &_docAndCache);
    }
    virtual double getDouble(const char* pDataPath, double defaultValue) const override
    {
        return getDouble(nullptr, pDataPath, defaultValue, nullptr, &_docAndCache);
    }
    virtual long getLong(const char* pDataPath, long defaultValue) const override
    {
        return getLong(nullptr, pDataPath, defaultValue, nullptr, &_docAndCache);
    }
    virtual bool getBool(const char* pDataPath, bool defaultValue) const override
    {
        return getBool(nullptr, pDataPath, defaultValue, nullptr, &_docAndCache);
    }

    // Get array elements
    virtual bool getArrayElems(const char *pDataPath, std::vector<String>& strList) const override
    {
        return getArrayElems(nullptr, pDataPath, strList, nullptr, &_docAndCache);
    }

    // Get keys
    virtual bool getKeys(const char *pDataPath, std::vector<String>& keysVector) const override
    {
        return getKeys(nullptr, pDataPath, keysVector, nullptr, &_docAndCache);
    }

    // Check if key exists
    virtual bool contains(const char* pDataPath) const override
    {
        int arrayLen = 0;
        jsmntype_t elemType = getType(nullptr, pDataPath, arrayLen, nullptr, &_docAndCache);
        return elemType != JSMN_UNDEFINED;
    }

    // Get type
    virtual RaftJsonType getType(const char* pDataPath, int &arrayLen) const override
    {
        jsmntype_t jsmntype = getType(nullptr, pDataPath, arrayLen, nullptr, &_docAndCache);
        switch (jsmntype)
        {
        case JSMN_PRIMITIVE:
            return RaftJsonType::RAFT_JSON_NUMBER;
        case JSMN_STRING:
            return RaftJsonType::RAFT_JSON_STRING;
        case JSMN_OBJECT:
            return RaftJsonType::RAFT_JSON_OBJECT;
        case JSMN_ARRAY:
            return RaftJsonType::RAFT_JSON_ARRAY;
        case JSMN_UNDEFINED:
            return RaftJsonType::RAFT_JSON_UNDEFINED;
        }
        return RaftJsonType::RAFT_JSON_UNDEFINED;
    }

    // Access to JSON document
    virtual const char* getJsonDoc() const
    {
        return _docAndCache.getJsonDoc();
    }
    virtual uint32_t getJsonDocLen() const
    {
        return _docAndCache.getJsonDocLen();
    }
    virtual const char* c_str() const
    {
        return _docAndCache.getJsonDoc();
    }

    // Static methods

    class JSONDocAndCache;
    static String getString(const char* pJsonDoc, 
            const char* pDataPath, const char* defaultValue,
            const char* pPathPrefix = nullptr,
            const JSONDocAndCache* pDocAndCache = nullptr);
    static double getDouble(const char* pJsonDoc,
            const char* pDataPath, double defaultValue,
            const char* pPathPrefix = nullptr,
            const JSONDocAndCache* pDocAndCache = nullptr);
    static long getLong(const char* pJsonDoc,
            const char* pDataPath, long defaultValue,
            const char* pPathPrefix = nullptr,
            const JSONDocAndCache* pDocAndCache = nullptr);
    static bool getBool(const char* pJsonDoc,
            const char* pDataPath, bool defaultValue,
            const char* pPathPrefix = nullptr,
            const JSONDocAndCache* pDocAndCache = nullptr);

    // Get array elements
    static bool getArrayElems(const char* pJsonDoc,
            const char *pDataPath, std::vector<String>& strList,
            const char* pPathPrefix = nullptr,
            const JSONDocAndCache* pDocAndCache = nullptr);

    // Get keys (static)
    static bool getKeys(const char* pJsonDoc,
            const char *pDataPath, std::vector<String>& keysVector,
            const char* pPathPrefix = nullptr,
            const JSONDocAndCache* pDocAndCache = nullptr);

    // Get type of element (also returns array length if array)
    static jsmntype_t getType(const char* pJsonDoc,
            const char* pDataPath,
            int &arrayLen, 
            const char* pPathPrefix = nullptr, 
            const JSONDocAndCache* pDocAndCache = nullptr);

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
        std::vector<RaftJson_jsmn::NameValuePair>& nameValuePairs);

    // Extract name value pairs from a string
    static constexpr int RAFT_JSON_MAX_TOKENS = 10000;
    static jsmntok_t* parseJson(const char* jsonStr, int& numTokens,
                        int maxTokens = RAFT_JSON_MAX_TOKENS);

    // Check for boolean    
    static bool isBoolean(const char* pBuf, uint32_t bufLen, int &retValue);

    // Get HTML query string from JSON
    static String getHTMLQueryFromJSON(const String& jsonStr);

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
                        const char* pPathPrefix,
                        int &startPos, int &strLen,
                        jsmntype_t &elemType, int &elemSize,
                        std::vector<String>* pKeysVector,
                        std::vector<String>* pArrayElems,
                        const char *pSourceStr,
                        const JSONDocAndCache* pDocAndCache);

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

        // Constructor
        JSONDocAndCache()
        {
        }

        // Destructor
        ~JSONDocAndCache()
        {
            // Release cached parse result
            releaseCachedParseResult();
        }

        // Copy constructor and assignment operator
        JSONDocAndCache(const JSONDocAndCache& other)
        {
            // Copy JSON document
            if (other.pJsonDocStatic)
            {
                pJsonDocStatic = other.pJsonDocStatic;
                jsonDocStaticLen = other.jsonDocStaticLen;
            }
            else
            {
                jsonDoc = other.jsonDoc;
            }

            // Copy parse parameters
            cacheParseResults = other.cacheParseResults;
            maxTokens = other.maxTokens;

            // NOTE: the cache results are not copied and remain nullptr
        }

        // Get JSON document
        const char* getJsonDoc() const
        {
            if (pJsonDocStatic)
                return pJsonDocStatic;
            return jsonDoc.data();
        }

        // Get JSON document length
        uint32_t getJsonDocLen() const
        {
            if (pJsonDocStatic)
                return strlen(pJsonDocStatic);
            return jsonDoc.size();
        }


        JSONDocAndCache& operator=(const JSONDocAndCache& other)
        {
            // Copy JSON document
            if (other.pJsonDocStatic)
            {
                pJsonDocStatic = other.pJsonDocStatic;
                jsonDocStaticLen = other.jsonDocStaticLen;
            }
            else
            {
                jsonDoc = other.jsonDoc;
            }

            // Copy parse parameters
            cacheParseResults = other.cacheParseResults;
            maxTokens = other.maxTokens;

            // NOTE: the cache results are not copied and remain nullptr

            return *this;
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
        uint16_t maxTokens = RAFT_JSON_MAX_TOKENS;
    };

    // // Get a string from the JSON
    // String getString(const char* pDataPath,
    //                     const char* defaultValue, bool& isValid,
    //                     jsmntype_t& elemType, int& elemSize,
    //                     const char* pSourceStr);

    // // Alternate form of getString with fewer parameters
    // String getString(const char* pDataPath, const char* defaultValue,
    //                     const char* pSourceStr, bool& isValid);

    // // Alternate form of getString with fewer parameters
    // String getString(const char* pDataPath, const char* defaultValue,
    //                     const char* pSourceStr);

    // double getDouble(const char* pDataPath,
    //                     double defaultValue, bool& isValid,
    //                     const char* pSourceStr);

    // double getDouble(const char* pDataPath, double defaultValue,
    //                     const char* pSourceStr);

    // long getLong(const char* pDataPath,
    //                     long defaultValue, bool& isValid,
    //                     const char* pSourceStr);

    // long getLong(const char* pDataPath, long defaultValue, const char* pSourceStr);

    // bool getBool(const char* pDataPath,
    //                     bool defaultValue, bool& isValid,
    //                     const char* pSourceStr);

    // bool getBool(const char* pDataPath, bool defaultValue, const char* pSourceStr);

    // jsmntype_t getType(int& arrayLen, const char* pSourceStr);

    // const int MAX_KEYS_TO_RETURN = 100;
    // bool getKeys(const char *pDataPath, std::vector<String>& keysVector, const char *pSourceStr);
    
    // bool getArrayElems(const char *pDataPath, std::vector<String>& arrayElems, const char *pSourceStr);

    // size_t safeStringLen(const char* pSrc,
    //                             bool skipJSONWhitespace = false, size_t maxx = LONG_MAX);

    // void safeStringCopy(char* pDest, const char* pSrc,
    //                            size_t maxx, bool skipJSONWhitespace = false);

    // Debug
    static void debugDumpParseResult(const char* pSourceStr, jsmntok_t* pTokens, int numTokens,
                const char* debugLinePrefix);


    // char* safeStringDup(const char* pSrc, size_t maxx,
    //                     bool skipJSONWhitespace = false);
    // Find key in JSON
    static int findKeyInJson(const char* pJsonDoc, 
                        const char* pDataPath, 
                        const char* pPathPrefix,
                        jsmntok_t tokens[],
                        unsigned int numTokens, 
                        int& endTokenIdx,
                        jsmntype_t keyType = JSMN_UNDEFINED);
    static bool extractPathParts(const char* pDataPath, const char* pPathPrefix, 
            std::vector<String>& pathParts, 
            std::vector<int>& arrayIndices);
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


private:
    // JSON document and cache
    JSONDocAndCache _docAndCache;

};
