/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftJson - JSON parser and field extractor
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
#include "RaftJsonIF.h"
#ifdef ESP_PLATFORM
#include "SpiramAwareAllocator.h"
#endif

class RaftJson : public RaftJsonIF
{
public:
    // Element type
    typedef enum
    {
        JSON_ELEM_UNDEFINED = 0,
        JSON_ELEM_OBJECT = 1,
        JSON_ELEM_ARRAY = 2,
        JSON_ELEM_STRING = 3,
        JSON_ELEM_PRIMITIVE = 4
    } JSON_ELEM_TYPE;

    // Constructor from string with all options
    // Note that when makeCopy is false the string pointer must remain valid for the lifetime of this object
    // This option is provided to avoid copying strings in flash memory - please don't use it in other cases
    RaftJson(const char* pJsonStr, bool makeCopy = true)
    {
        if (makeCopy)
        {
#ifdef ESP_PLATFORM
            _jsonStr = std::vector<char, SpiramAwareAllocator<char>>(pJsonStr, pJsonStr + strlen(pJsonStr) + 1);
#else
            _jsonStr = std::vector<char>(pJsonStr, pJsonStr + strlen(pJsonStr) + 1);
#endif
            _pSourceStr = _jsonStr.data();
        }
        else
        {
            _pSourceStr = pJsonStr;
        }
    }

    // Constructor from arduino String (makes a copy)
    RaftJson(const String& jsonStr)
    {
#ifdef ESP_PLATFORM        
        _jsonStr = std::vector<char, SpiramAwareAllocator<char>>(jsonStr.c_str(), jsonStr.c_str() + jsonStr.length() + 1);
#else
        _jsonStr = std::vector<char>(jsonStr.c_str(), jsonStr.c_str() + jsonStr.length() + 1);
#endif
        _pSourceStr = _jsonStr.data();
    }

    // Constructor from std::string (makes a copy)
    RaftJson(const std::string& jsonStr)
    {
#ifdef ESP_PLATFORM
        _jsonStr = std::vector<char, SpiramAwareAllocator<char>>(jsonStr.c_str(), jsonStr.c_str() + jsonStr.length() + 1);
#else
        _jsonStr = std::vector<char>(jsonStr.c_str(), jsonStr.c_str() + jsonStr.length() + 1);
#endif
        _pSourceStr = _jsonStr.data();
    }

    // Destructor
    virtual ~RaftJson()
    {
    }

    // Assignment from string types
    RaftJson& operator=(const char* pJsonStr)
    {
#ifdef ESP_PLATFORM
        _jsonStr = std::vector<char, SpiramAwareAllocator<char>>(pJsonStr, pJsonStr + strlen(pJsonStr) + 1);
#else
        _jsonStr = std::vector<char>(pJsonStr, pJsonStr + strlen(pJsonStr) + 1);
#endif
        _pSourceStr = _jsonStr.data();
        return *this;
    }
    RaftJson& operator=(const String& jsonStr)
    {
#ifdef ESP_PLATFORM
        _jsonStr = std::vector<char, SpiramAwareAllocator<char>>(jsonStr.c_str(), jsonStr.c_str() + jsonStr.length() + 1);
#else
        _jsonStr = std::vector<char>(jsonStr.c_str(), jsonStr.c_str() + jsonStr.length() + 1);
#endif
        _pSourceStr = _jsonStr.data();
        return *this;
    }
    RaftJson& operator=(const std::string& jsonStr)
    {
#ifdef ESP_PLATFORM
        _jsonStr = std::vector<char, SpiramAwareAllocator<char>>(jsonStr.c_str(), jsonStr.c_str() + jsonStr.length() + 1);
#else
        _jsonStr = std::vector<char>(jsonStr.c_str(), jsonStr.c_str() + jsonStr.length() + 1);
#endif
        _pSourceStr = _jsonStr.data();
        return *this;
    }

    // Get values from JSON key/value pairs
    virtual String getString(const char* pDataPath, const char* defaultValue, const char* pPathPrefix = nullptr) const override
    {
        return getStringStatic(_pSourceStr, pDataPath, defaultValue, pPathPrefix);
    }
    virtual double getDouble(const char* pDataPath, double defaultValue, const char* pPathPrefix = nullptr) const override
    {
        return getDoubleStatic(_pSourceStr, pDataPath, defaultValue, pPathPrefix);
    }
    virtual long getLong(const char* pDataPath, long defaultValue, const char* pPathPrefix = nullptr) const override
    {
        return getLongStatic(_pSourceStr, pDataPath, defaultValue, pPathPrefix);
    }
    virtual bool getBool(const char* pDataPath, bool defaultValue, const char* pPathPrefix = nullptr) const override
    {
        return getBoolStatic(_pSourceStr, pDataPath, defaultValue, pPathPrefix);
    }

    // Get array elements
    virtual bool getArrayElems(const char *pDataPath, std::vector<String>& strList, const char* pPathPrefix = nullptr) const override
    {
        return getArrayElemsStatic(_pSourceStr, pDataPath, strList, pPathPrefix);
    }

    // Get keys
    virtual bool getKeys(const char *pDataPath, std::vector<String>& keysVector, const char* pPathPrefix = nullptr) const override
    {
        return getKeysStatic(_pSourceStr, pDataPath, keysVector, pPathPrefix);
    }

    // Check if key exists
    virtual bool contains(const char* pDataPath, const char* pPathPrefix = nullptr) const override
    {
        int arrayLen = 0;
        JSON_ELEM_TYPE elemType = getTypeStatic(_pSourceStr, pDataPath, arrayLen, pPathPrefix);
        return elemType != JSON_ELEM_UNDEFINED;
    }

    // Static methods

    static String getStringStatic(const char* pJsonDoc,
            const char* pDataPath, const char* defaultValue,
            const char* pPathPrefix = nullptr)
    {
        // Locate the element
        const char* pJsonDocPos = pJsonDoc;
        const char* pElemStart = nullptr;
        const char* pElemEnd = nullptr;
        pJsonDocPos = locateElementByPath(pJsonDocPos, pJsonDocPos + strlen(pJsonDoc), pDataPath, pElemStart, pElemEnd);
        // Check if we found the element
        if (!pJsonDocPos)
            return defaultValue;
        // Skip quotes
        if (*pElemStart == '"')
            pElemStart++;
        return String(pElemStart, pElemEnd - pElemStart);
    }
    static double getDoubleStatic(const char* pJsonDoc,
            const char* pDataPath, double defaultValue,
            const char* pPathPrefix = nullptr)
    {
        // Locate the element
        const char* pJsonDocPos = pJsonDoc;
        const char* pElemStart = nullptr;
        const char* pElemEnd = nullptr;
        pJsonDocPos = locateElementByPath(pJsonDocPos, pJsonDocPos + strlen(pJsonDoc), pDataPath, pElemStart, pElemEnd);
        // Check if we found the element
        if (!pJsonDocPos)
            return defaultValue;
        // Check if it's a boolean
        int retValue = 0;
        if (RaftJson::isBoolean(pElemStart, pElemEnd-pElemStart, retValue))
            return retValue;
        // Convert to double
        return strtod(pElemStart, NULL);
    }
    static long getLongStatic(const char* pJsonDoc,
            const char* pDataPath, long defaultValue,
            const char* pPathPrefix = nullptr)
    {
        // Locate the element
        const char* pJsonDocPos = pJsonDoc;
        const char* pElemStart = nullptr;
        const char* pElemEnd = nullptr;
        pJsonDocPos = locateElementByPath(pJsonDocPos, pJsonDocPos + strlen(pJsonDoc), pDataPath, pElemStart, pElemEnd);
        // Check if we found the element
        if (!pJsonDocPos)
            return defaultValue;
        // Check if it's a boolean
        int retValue = 0;
        if (RaftJson::isBoolean(pElemStart, pElemEnd-pElemStart, retValue))
            return retValue;
        // Convert to long
        return strtol(pElemStart, NULL, 0);
    }
    static bool getBoolStatic(const char* pJsonDoc, 
            const char* pDataPath, bool defaultValue,
            const char* pPathPrefix = nullptr)
    {
        // Use long method to get value
        return RaftJson::getLongStatic(pJsonDoc, pDataPath, defaultValue, pPathPrefix) != 0;
    }

    // Get array elements
    static bool getArrayElemsStatic(const char* pJsonDoc,
            const char *pDataPath, std::vector<String>& strList,
            const char* pPathPrefix = nullptr)
    {
        // TODO - implement
        return false;
    }

    // Get keys (static)
    static bool getKeysStatic(const char* pJsonDoc,
            const char *pDataPath, std::vector<String>& keysVector,
            const char* pPathPrefix = nullptr)
    {
        // TODO - implement
        return false;
    }

    // Boolean check
    static bool isBoolean(const char* pBuf, uint32_t bufLen, int &retValue)
    {
        if (bufLen == 4)
        {
            if (strncmp(pBuf, "true", 4) == 0)
            {
                retValue = 1;
                return true;
            }
        }
        else if (bufLen == 5)
        {
            if (strncmp(pBuf, "false", 5) == 0)
            {
                retValue = 0;
                return true;
            }
        }
        return false;
    }

    // Get type of element (also returns array length if array)
    static JSON_ELEM_TYPE getTypeStatic(const char* pJsonDoc,
            const char* pDataPath, int &arrayLen, 
            const char* pPathPrefix = nullptr)
    {
        // TODO - implement
        return JSON_ELEM_UNDEFINED;
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

    // Get HTML query string from JSON
    static String getHTMLQueryFromJSON(const String& jsonStr);

    /**
     * getElement : Get location of element in JSON string
     * 
     * @param  {char*} pSourceStr                       : json string to search for element
     * @param  {char*} pDataPath                        : path to element to return info about
     * @param  {int&} startPos                          : [out] start position 
     * @param  {int&} strLen                            : [out] length
     * @param  {jsmntype_t&} elemType                   : [out] element type
     * @param  {int&} elemSize                          : [out] element size
     * @param  {std::vector<String>*} pKeysVector       : [out] pointer to vector to receive keys (maybe nullptr)
     * @param  {std::vector<String>*} pArrayElems       : [out] pointer to vector to receive array elements (maybe nullptr)
     * @param  {const JSONDocAndCache*} pDocAndCache    : [in] pointer to JSON document and cache (maybe nullptr if pSourceStr is provided)
     * @return {bool}                 : true if element found
     * 
     * NOTE: If pSourceStr is provided then pDocAndCache must be nullptr and vice versa. If pSourceStr is provided then
     *      the parse result is not cached. If pDocAndCache is provided then the parse result maybe cached based on the 
     *     cacheParseResults flag in the JSONDocAndCache object (if the parse result is not already cached).
     */
    // static bool getElement(const char *pSourceStr,
    //                 const char *pDataPath,
    //                 const char* pPathPrefix,
    //                 int &startPos, int &strLen,
    //                     jsmntype_t &elemType, int &elemSize,
    //                     std::vector<String>* pKeysVector,
    //                     std::vector<String>* pArrayElems,
                        
    //                     const JSONDocAndCache* pDocAndCache);

    static const char* getElemTypeStr(JSON_ELEM_TYPE type)
    {
        switch (type)
        {
        case JSON_ELEM_PRIMITIVE: return "PRIM";
        case JSON_ELEM_STRING: return "STR";
        case JSON_ELEM_OBJECT: return "OBJ";
        case JSON_ELEM_ARRAY: return "ARRY";
        case JSON_ELEM_UNDEFINED: return "UNDEF";
        }
        return "UNKN";
    }

    // String getNextPathElem(const char*& pDataPathPos)
    // {
    //     const char* pElemStart = pDataPathPos;
    //     while (*pDataPathPos && (*pDataPathPos != '/'))
    //         pDataPathPos++;
    //     String pathPart = String(pElemStart, pDataPathPos - pElemStart);
    //     if (*pDataPathPos)
    //         pDataPathPos++;
    //     return pathPart;
    // }

    static const char* locateStringElement(const char* pJsonDocPos, const char*& pElemStart, const char*& pElemEnd, bool includeQuotes = false)
    {
        // Skip quote if at start of string
        if (!includeQuotes && (*pJsonDocPos == '"'))
            pJsonDocPos++;

        // Find end of string
        pElemStart = pJsonDocPos;
        while (*pJsonDocPos && (*pJsonDocPos != '"'))
            pJsonDocPos++;

        // Return string start and end
        if (*pJsonDocPos == '"')
        {
            pElemEnd = includeQuotes ? pJsonDocPos + 1 : pJsonDocPos;
            return pJsonDocPos + 1;
        }
        return nullptr;
    }

    static const char* skipOverElement(const char* pJsonDocPos, const char* pMaxDocPos, 
                const char*& pElemStart, const char*& pElemEnd)
    {
        // Skip whitespace, commas and colons
        while (*pJsonDocPos && ((*pJsonDocPos <= ' ') || (*pJsonDocPos == ',') || (*pJsonDocPos == ':'))
                && (pJsonDocPos < pMaxDocPos))
            pJsonDocPos++;
        if (!*pJsonDocPos || (pJsonDocPos >= pMaxDocPos))
            return nullptr;
        pElemStart = pJsonDocPos;
            
        // Check for kind of element
        if ((*pJsonDocPos == '{') || (*pJsonDocPos == '['))
        {
            // Find end of object
            char braceChar = *pJsonDocPos;
            int numBraces = 1;
            pJsonDocPos++;
            // Skip to end of object
            bool insideString = false;
            while (*pJsonDocPos && (numBraces > 0) && (pJsonDocPos < pMaxDocPos))
            {
                if (*pJsonDocPos == '"')
                    insideString = !insideString;
                if (!insideString)
                {
                    if (*pJsonDocPos == braceChar)
                        numBraces++;
                    else if (*pJsonDocPos == ((braceChar == '{') ? '}' : ']'))
                        numBraces--;
                }
                pJsonDocPos++;
            }
            if (!*pJsonDocPos)
                return nullptr;
            pElemEnd = pJsonDocPos;
            return pJsonDocPos;
        }
        else if (*pJsonDocPos == '"')
        {
            // Find end of string
            pJsonDocPos++;
            while (*pJsonDocPos && (*pJsonDocPos != '"') && (pJsonDocPos < pMaxDocPos))
                pJsonDocPos++;
            if (!*pJsonDocPos)
                return nullptr;
            pElemEnd = pJsonDocPos;
            pJsonDocPos++;
            return pJsonDocPos;
        }
        else
        {
            // Find end of element
            while (*pJsonDocPos && (*pJsonDocPos != ',') && (pJsonDocPos < pMaxDocPos))
                pJsonDocPos++;
            if (!*pJsonDocPos)
                return nullptr;
            pElemEnd = pJsonDocPos;
            return pJsonDocPos;
        }
    }

    // const char* locateNthArrayElement(const char* pJsonDocPos, 
    //             const char*& pElemStart, const char*& pElemEnd,
    //             int arrayIdx)
    // {
    //     // Skip over elements until we get to the one we want
    //     for (int i = 0; i < arrayIdx; i++)
    //     {
    //         pJsonDocPos = skipOverElement(pJsonDocPos, pElemStart, pElemEnd);
    //         if (!pJsonDocPos)
    //             return nullptr;
    //         // Skip whitespace
    //         while (*pJsonDocPos && (*pJsonDocPos <= ' '))
    //             pJsonDocPos++;
    //         // Check for end of array
    //         if (*pJsonDocPos == ']')
    //             return nullptr;
    //     }
    //     return pJsonDocPos;
    // }

    // Locate value inside element with key
    // The key can be empty in which case the entire object is returned
    // The key can be an array index (e.g. "[0]") in which case the value at that index is returned if the element is an array
    // The key can be a string in which case the value for that key is returned if the element is an object

    static const char* locateElementValueWithKey(const char* pJsonDocPos,
                const char* pMaxDocPos,
                const char*& pReqdKey,
                const char*& pElemStart, const char*& pElemEnd)
    {
        // If key is empty return the entire element
        if (!pReqdKey || !*pReqdKey || (*pReqdKey == '/'))
        {
            // Skip to end of element
            pJsonDocPos = skipOverElement(pJsonDocPos, pMaxDocPos, pElemStart, pElemEnd);
            if (!pJsonDocPos)
                return nullptr;
            // Skip whitespace
            while (*pJsonDocPos && (*pJsonDocPos <= ' '))
                pJsonDocPos++;
            // Move key position to next part of path
            if (pReqdKey && (*pReqdKey == '/'))
                pReqdKey++;
            return pJsonDocPos;
        }

        // Skip any whitespace
        while (*pJsonDocPos && (*pJsonDocPos <= ' '))
            pJsonDocPos++;
        if ((*pJsonDocPos != '{') && (*pJsonDocPos != '['))
            return nullptr;

        // Check for the type of element - object or array
        const char* pReqdKeyStart = pReqdKey;
        const char* pReqdKeyEnd = nullptr;
        bool isObject = true;
        uint32_t arrayIdx = 0;
        uint32_t elemCount = 0;
        if (*pJsonDocPos == '[')
        {
            isObject = false;
            // Check the key is an array index
            if (*pReqdKey != '[')
                return nullptr;
            pReqdKey++;
            // Extract array index from key
            arrayIdx = atoi(pReqdKey);
            // Move key position to next part of path
            while (*pReqdKey && (*pReqdKey != '/'))
                pReqdKey++;
        }
        else
        {
            // Find the end of this part of the key path
            while (*pReqdKey && (*pReqdKey != '/') && (*pReqdKey != '['))
                pReqdKey++;
            pReqdKeyEnd = pReqdKey;
        }
        // Move past the used part of the key path
        if (*pReqdKey == '/')
            pReqdKey++;

        // Move into the object or array
        pJsonDocPos++;

        // Skip over elements until we get to the one we want
        const char* pKeyStart = pJsonDocPos;
        const char* pKeyEnd = nullptr;
        while (*pJsonDocPos && pJsonDocPos < pMaxDocPos)
        {
            // Check for object - in which case what comes first is the key
            if (isObject)
            {
                // Skip to start of key
                while (*pJsonDocPos && (*pJsonDocPos != '"'))
                    pJsonDocPos++;
                if (!*pJsonDocPos)
                    return nullptr;
                // Extract key string
                pJsonDocPos = locateStringElement(pJsonDocPos, pKeyStart, pKeyEnd, false);
                if (!pJsonDocPos)
                    return nullptr;
                // Skip over any whitespace and colons
                while (*pJsonDocPos && ((*pJsonDocPos <= ' ') || (*pJsonDocPos == ':')))
                    pJsonDocPos++;
                if (!*pJsonDocPos)
                    return nullptr;
            }   
            // Skip to end of element
            pJsonDocPos = skipOverElement(pJsonDocPos, pMaxDocPos, pElemStart, pElemEnd);
            if (!pJsonDocPos)
                return nullptr;
            // Skip whitespace
            while (*pJsonDocPos && (*pJsonDocPos <= ' '))
                pJsonDocPos++;
            // Check if this is the key we are looking for
            if (isObject)
            {
                // Check for longer of key and path part
                uint32_t keyLen = (pKeyEnd - pKeyStart) > (pReqdKeyEnd - pReqdKeyStart) ? (pKeyEnd - pKeyStart) : (pReqdKeyEnd - pReqdKeyStart);
                if (strncmp(pKeyStart, pReqdKeyStart, keyLen) == 0)
                    return pJsonDocPos;
                // Check for end of object
                if (*pJsonDocPos == '}')
                    return nullptr;
            }
            else
            {
                if (arrayIdx == elemCount)
                    return pJsonDocPos;
                elemCount++;
                // Check for end of array
                if (*pJsonDocPos == ']')
                    return nullptr;
            }
        }
        return nullptr;
    }

    // String getCurrentElement(const char*& pJsonDocPos, int arrayIdx, bool& isValid)
    // {
    //     // Skip any whitespace
    //     while (*pJsonDocPos && (*pJsonDocPos <= ' '))
    //         pJsonDocPos++;

    //     // Check for array
    //     if (*pJsonDocPos == '[')
    //     {
    //         // If we're looking for an array element then return it
    //         if (arrayIdx >= 0)
    //             return getNthArrayElement(pJsonDocPos+1, arrayIdx, isValid);
    //         // Otherwise return failure
    //         isValid = false;
    //         return "";
    //     }

    //     // Check for object
    //     if (*pJsonDocPos == '{')
    //     {
    //         // Return entire element
    //         return getObjectElement(pJsonDocPos, isValid);
    //     }

    //     // Check for string
    //     if (*pJsonDocPos == '"')
    //     {
    //         // Return string contents
    //         return getStringElement(pJsonDocPos, isValid, true);
    //     }

    //     // Nothing else is valid
    //     isValid = false;
    //     return "";
    // }

    static const char* locateElementByPath(const char* pJsonDocPos, 
                const char* pMaxDocPos,
                const char* pPath, 
                const char*& pElemStart, const char*& pElemEnd)
    {
        // Iterate over path
        const char* pPathPos = pPath;
        while(true)
        {
            // Locate element
            pJsonDocPos = locateElementValueWithKey(pJsonDocPos, pMaxDocPos, pPathPos, pElemStart, pElemEnd);
            if (!pJsonDocPos)
                return nullptr;

            // Check if we're at the end of the path
            if (!*pPathPos)
                return pJsonDocPos;

            // Set search bounds to the object we've just found
            pJsonDocPos = pElemStart;
            pMaxDocPos = pElemEnd;
        }
    }

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

    // // Find key in JSON
    // static int findKeyInJson(const char* pJsonDoc, 
    //                     const char* pDataPath, 
    //                     const char* pPathPrefix,
    //                     jsmntok_t tokens[],
    //                     unsigned int numTokens, 
    //                     int& endTokenIdx,
    //                     jsmntype_t keyType = JSMN_UNDEFINED);
    // static bool extractPathParts(const char* pDataPath, const char* pPathPrefix, 
    //         std::vector<String>& pathParts, 
    //         std::vector<int>& arrayIndices);
    // static int findElemEnd(const char* jsonOriginal, jsmntok_t tokens[],
    //                     unsigned int numTokens, int startTokenIdx);
    // static int findArrayElem(const char *jsonOriginal, jsmntok_t tokens[],
    //                     unsigned int numTokens, int startTokenIdx, 
    //                     int arrayElemIdx);

private:
    // JSON string
#ifdef ESP_PLATFORM
    std::vector<char, SpiramAwareAllocator<char>> _jsonStr;
#else
    std::vector<char> _jsonStr;
#endif

    // Pointer to JSON string
    const char* _pSourceStr = nullptr;

};
