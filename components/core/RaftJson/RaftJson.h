/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftJson - JSON parser and field extractor
// 
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
#include "RaftJsonIF.h"

// Accommodate usage standalone (outside RaftCore)
#if __has_include("RaftArduino.h")
#include "Logger.h"
// #define DEBUG_JSON_BY_SPECIFIC_PATH "SystemName"
// #define DEBUG_JSON_BY_SPECIFIC_PATH_PART ""
// #define DEBUG_JSON_LOCATE_ELEMENT_BOUNDS
// #define DEBUG_EXTRACT_NAME_VALUES
// #define DEBUG_CHAINED_RAFT_JSON
#ifdef ESP_PLATFORM
#include "SpiramAwareAllocator.h"
#define USE_RAFT_SPIRAM_AWARE_ALLOCATOR
#endif
#endif

// Treat strings as numbers in JSON documents
// Change this value to false if you want to treat strings as strings in ALL JSON documents
#define RAFT_JSON_TREAT_STRINGS_AS_NUMBERS true

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @class RaftJson
/// @brief JSON on-demand parser and field extractor
class RaftJson : public RaftJsonIF
{
public:
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Construct an empty RaftJson object
    RaftJson()
    {
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Construct a new RaftJson object
    /// @param pJsonStr start of JSON document
    /// @param pJsonEnd end of JSON document
    /// @param makeCopy when false the string pointer must remain valid for the lifetime of this object
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @note The makeCopy option is provided to avoid copying strings in flash memory - please don't use it in other cases
    RaftJson(const char* pJsonStr, bool makeCopy = true, const char* pJsonEnd = nullptr, RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Store the source string
        setSourceStr(pJsonStr, makeCopy, pJsonEnd ? pJsonEnd : pJsonStr + strlen(pJsonStr));
        _pChainedRaftJson = pChainedRaftJson;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Construct a new RaftJson object from Arduino String
    /// @param pJsonStr
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @note makes a copy
    RaftJson(const String& jsonStr, RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Store the source string
        setSourceStr(jsonStr.c_str(), true, jsonStr.c_str() + jsonStr.length());
        _pChainedRaftJson = pChainedRaftJson;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Construct a new RaftJson object from std::string
    /// @param pJsonStr
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @note makes a copy
    RaftJson(const std::string& jsonStr, RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Store the source string
        setSourceStr(jsonStr.c_str(), true, jsonStr.c_str() + jsonStr.length());
        _pChainedRaftJson = pChainedRaftJson;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Destructor
    virtual ~RaftJson()
    {
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Assignment from string types
    /// @param pJsonStr
    /// @note makes a copy
    RaftJson& operator=(const char* pJsonStr)
    {
        // Store the source string
        setSourceStr(pJsonStr, true, pJsonStr + strlen(pJsonStr));
        return *this;
    }
    RaftJson& operator=(const String& jsonStr)
    {
        // Store the source string
        setSourceStr(jsonStr.c_str(), true, jsonStr.c_str() + jsonStr.length());
        return *this;
    }
    RaftJson& operator=(const std::string& jsonStr)
    {
        // Store the source string
        setSourceStr(jsonStr.c_str(), true, jsonStr.c_str() + jsonStr.length());
        return *this;
    }
    virtual bool setJsonDoc(const char* pJsonDoc) override
    {
        // Store the source string
        setSourceStr(pJsonDoc, true, pJsonDoc + strlen(pJsonDoc));
        return true;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get string value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual String getString(const char* pDataPath, const char* defaultValue) const override
    {
        return getStringIm(_pSourceStr, _pSourceEnd, pDataPath, defaultValue, _pChainedRaftJson);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get double value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual double getDouble(const char* pDataPath, double defaultValue) const override
    {
        return getDoubleIm(_pSourceStr, _pSourceEnd, pDataPath, defaultValue, _pChainedRaftJson);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get int value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual int getInt(const char* pDataPath, int defaultValue) const override
    {
        return getLongIm(_pSourceStr, _pSourceEnd, pDataPath, defaultValue, _pChainedRaftJson);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get long value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual long getLong(const char* pDataPath, long defaultValue) const override
    {
        return getLongIm(_pSourceStr, _pSourceEnd, pDataPath, defaultValue, _pChainedRaftJson);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get boolean value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual bool getBool(const char* pDataPath, bool defaultValue) const override
    {
        return getBoolIm(_pSourceStr, _pSourceEnd, pDataPath, defaultValue, _pChainedRaftJson);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get array elements using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param strList a vector which is filled with the array elements
    /// @return true if the array was found
    virtual bool getArrayElems(const char *pDataPath, std::vector<String>& strList) const override
    {
        return getArrayElemsIm(_pSourceStr, _pSourceEnd, pDataPath, strList, _pChainedRaftJson);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get array integers using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param strList a vector which is filled with the array integers
    /// @return true if the array was found
    /// @note This is a convenience function for the common case of an array of integers and assumes that
    ///       the array elements are all integers - any non-integer elements will be converted to 0
    virtual bool getArrayInts(const char *pDataPath, std::vector<int>& intList) const override
    {
        std::vector<String> strList;
        if (!getArrayElemsIm(_pSourceStr, _pSourceEnd, pDataPath, strList, _pChainedRaftJson))
            return false;
        for (String& str : strList)
            intList.push_back(str.toInt());
        return true;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get keys of an object using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param keysVector a vector which is filled with the keys
    /// @return true if the object was found
    virtual bool getKeys(const char *pDataPath, std::vector<String>& keysVector) const override
    {
        return getKeysIm(_pSourceStr, _pSourceEnd, pDataPath, keysVector, _pChainedRaftJson);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Check if the member JSON document contains the key specified by the path
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @return true if the key was found
    virtual bool contains(const char* pDataPath) const override
    {
        int arrayLen = 0;
        RaftJsonType elemType = getTypeIm(_pSourceStr, _pSourceEnd, pDataPath, arrayLen, _pChainedRaftJson);
        return elemType != RAFT_JSON_UNDEFINED;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get type of element from a JSON document at the specified path
    /// @param pDataPath the path of the required object in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param arrayLen the length of the array if the element is an array
    /// @return the type of the element
    virtual RaftJsonType getType(const char* pDataPath, int &arrayLen) const override
    {
        return getTypeIm(_pSourceStr, _pSourceEnd, pDataPath, arrayLen, _pChainedRaftJson);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Static methods
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief gets a string from a JSON document (immediate - i.e. static function, doc passed in)
    /// @param pJsonDoc the JSON document (string)
    /// @param pJsonEnd the end of the JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @return the value of the variable or the default value if not found
    static String getStringIm(const char* pJsonDoc, const char* pJsonEnd,
            const char* pDataPath, const char* defaultValue,
            const RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Locate the element
        const char* pJsonDocPos = pJsonDoc;
        bool foundInChainedDoc = false;
        pJsonDocPos = RaftJson::locateElemByPath(pJsonDocPos, pJsonEnd, pDataPath, pChainedRaftJson, foundInChainedDoc);
        // Check if we found the element
        if (!pJsonDocPos)
            return defaultValue;
        const char* pElemStart = nullptr;
        const char* pElemEnd = nullptr;
        const char* pDocEnd = (foundInChainedDoc && pChainedRaftJson) ? pChainedRaftJson->getJsonDocEnd() : pJsonEnd;
        if (!RaftJson::locateElementBounds(pJsonDocPos, pDocEnd, pElemStart, pElemEnd))
            return defaultValue;
        // Get string without quotes
        return getStringWithoutQuotes(pElemStart, pElemEnd, true);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief gets a double from a JSON document (immediate - i.e. static function, doc passed in)
    /// @param pJsonDoc the JSON document (string)
    /// @param pJsonEnd the end of the JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @return the value of the variable or the default value if not found
    static double getDoubleIm(const char* pJsonDoc, const char* pJsonEnd,
            const char* pDataPath, double defaultValue,
            const RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Locate the element
        const char* pJsonDocPos = pJsonDoc;
        bool foundInChainedDoc = false;
        pJsonDocPos = RaftJson::locateElemByPath(pJsonDocPos, pJsonEnd, pDataPath, pChainedRaftJson, foundInChainedDoc);
        // Check if we found the element
        if (!pJsonDocPos)
            return defaultValue;
        // Check if it's a boolean or null
        int retValue = 0;
        if (RaftJson::isBooleanIm(pJsonDocPos, retValue))
            return retValue;
        if (RaftJson::isNullIm(pJsonDocPos))
            return defaultValue;
        // Check for a string value - if so skip quotes
        if ((*pJsonDocPos == '"') && RAFT_JSON_TREAT_STRINGS_AS_NUMBERS)
            pJsonDocPos++;
        // Convert to double
        return strtod(pJsonDocPos, NULL);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief gets a long from a JSON document (immediate - i.e. static function, doc passed in)
    /// @param pJsonDoc the JSON document (string)
    /// @param pJsonEnd the end of the JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @return the value of the variable or the default value if not found
    static long getLongIm(const char* pJsonDoc, const char* pJsonEnd,
            const char* pDataPath, long defaultValue,
            const RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Locate the element
        const char* pJsonDocPos = pJsonDoc;
        bool foundInChainedDoc = false;        
        pJsonDocPos = RaftJson::locateElemByPath(pJsonDocPos, pJsonEnd, pDataPath, pChainedRaftJson, foundInChainedDoc);
        // Check if we found the element
        if (!pJsonDocPos)
            return defaultValue;
        // Check if it's a boolean or null
        int retValue = 0;
        if (RaftJson::isBooleanIm(pJsonDocPos, retValue))
            return retValue;
        if (RaftJson::isNullIm(pJsonDocPos))
            return defaultValue;
        // Check for a string value - if so skip quotes
        if ((*pJsonDocPos == '"') && RAFT_JSON_TREAT_STRINGS_AS_NUMBERS)
            pJsonDocPos++;
        // Convert to long
        return strtol(pJsonDocPos, NULL, 0);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief gets a boolean from a JSON document (immediate - i.e. static function, doc passed in)
    /// @param pJsonDoc the JSON document (string)
    /// @param pJsonEnd the end of the JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @return the value of the variable or the default value if not found
    static bool getBoolIm(const char* pJsonDoc, const char* pJsonEnd,
            const char* pDataPath, bool defaultValue,
            const RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Use long method to get value
        return RaftJson::getLongIm(pJsonDoc, pJsonEnd, pDataPath, defaultValue, pChainedRaftJson) != 0;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief gets the elements of an array from the JSON (immediate - i.e. static function, doc passed in)
    /// @param pJsonDoc the JSON document (string)
    /// @param pJsonEnd the end of the JSON document
    /// @param pDataPath the path of the required array in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param strList a vector which is filled with the array elements
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @return true if the array was found
    static bool getArrayElemsIm(const char* pJsonDoc, const char* pJsonEnd,
            const char *pDataPath, std::vector<String>& strList,
            const RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Locate the element
        const char* pJsonDocPos = pJsonDoc;
        bool foundInChainedDoc = false;
        pJsonDocPos = RaftJson::locateElemByPath(pJsonDocPos, pJsonEnd, pDataPath, pChainedRaftJson, foundInChainedDoc);
        // Check if we found the element
        if (!pJsonDocPos)
            return false;
        // Check if it's an array
        if (*pJsonDocPos != '[')
            return false;
        // Skip over array start
        pJsonDocPos++;
        // Iterate over array elements
        const char* pDocEnd = (foundInChainedDoc && pChainedRaftJson) ? pChainedRaftJson->getJsonDocEnd() : pJsonEnd;
        while (*pJsonDocPos && (pJsonDocPos < pDocEnd))
        {
            // Skip whitespace
            while (*pJsonDocPos && (pJsonDocPos < pDocEnd) && (*pJsonDocPos <= ' '))
                pJsonDocPos++;
            // Check for end of array
            if (*pJsonDocPos == ']')
                return true;
            // Locate element
            const char* pElemStart = nullptr;
            const char* pElemEnd = nullptr;
            pJsonDocPos = RaftJson::locateElementBounds(pJsonDocPos, pDocEnd, pElemStart, pElemEnd);
            if (!pJsonDocPos)
                return false;
            // Add to list
            strList.push_back(getStringWithoutQuotes(pElemStart, pElemEnd, true));
        }
        return true;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief gets the keys of an object from the JSON (immediate - i.e. static function, doc passed in)
    /// @param pJsonDoc the JSON document (string)
    /// @param pJsonEnd the end of the JSON document
    /// @param pDataPath the path of the required object in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param keysVector a vector which is filled with the keys
    /// @param pChainRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @return true if the object was found
    static bool getKeysIm(const char* pJsonDoc, const char* pJsonEnd,
            const char *pDataPath, std::vector<String>& keysVector,
            const RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Locate the element
        const char* pJsonDocPos = pJsonDoc;
        bool foundInChainedDoc = false;
        pJsonDocPos = RaftJson::locateElemByPath(pJsonDocPos, pJsonEnd, pDataPath, pChainedRaftJson, foundInChainedDoc);
        // Check if we found the element
        if (!pJsonDocPos)
            return false;
        // Check if it's an object
        if (*pJsonDocPos != '{')
            return false;
        // Skip over object start
        pJsonDocPos++;
        // Iterate over object elements
        const char* pDocEnd = (foundInChainedDoc && pChainedRaftJson) ? pChainedRaftJson->getJsonDocEnd() : pJsonEnd;
        while (*pJsonDocPos && (pJsonDocPos < pDocEnd))
        {
            // Skip whitespace
            while (*pJsonDocPos && (pJsonDocPos < pDocEnd) && (*pJsonDocPos <= ' '))
                pJsonDocPos++;
            // Check for end of object
            if (*pJsonDocPos == '}')
                return true;
            // Locate key
            const char* pKeyStart = nullptr;
            const char* pKeyEnd = nullptr;
            pJsonDocPos = locateStringElement(pJsonDocPos, pDocEnd, pKeyStart, pKeyEnd, false);
            if (!pJsonDocPos)
                return false;
            // Add to list
            keysVector.push_back(String(pKeyStart, pKeyEnd - pKeyStart));
            // Skip to end of element
            const char* pElemEnd = nullptr;
            const char* pElemStart = nullptr;
            pJsonDocPos = RaftJson::locateElementBounds(pJsonDocPos, pDocEnd, pElemStart, pElemEnd);
            if (!pJsonDocPos)
                return false;
        }
        return true;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Check if element at the current position in a JSON document is a boolean value (static)
    /// @param pJsonDocPos the current position in the JSON document
    /// @param retValue the value of the boolean
    /// @return true if the element is a boolean
    static bool isBooleanIm(const char* pJsonDocPos, int &retValue)
    {
        if (!pJsonDocPos)
            return false;
        if (strncmp(pJsonDocPos, "true", 4) == 0)
        {
            retValue = 1;
            return true;
        }
        if (strncmp(pJsonDocPos, "false", 5) == 0)
        {
            retValue = 0;
            return true;
        }
        return false;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Check if element at the current position in a JSON document is a null value (static)
    /// @param pJsonDocPos the current position in the JSON document
    /// @return true if the element is a null
    static bool isNullIm(const char* pJsonDocPos)
    {
        if (!pJsonDocPos)
            return false;
        if (strncmp(pJsonDocPos, "null", 4) == 0)
        {
            return true;
        }
        return false;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get type of element from a JSON document at the specified path
    /// @param pJsonDoc the JSON document (string)
    /// @param pJsonEnd the end of the JSON document
    /// @param pDataPath the path of the required object in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param arrayLen the length of the array if the element is an array
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @return the type of the element
    static RaftJsonType getTypeIm(const char* pJsonDoc, const char* pJsonEnd,
            const char* pDataPath, int &arrayLen,
            const RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Locate the element
        const char* pJsonDocPos = pJsonDoc;
        bool foundInChainedDoc = false;
        pJsonDocPos = RaftJson::locateElemByPath(pJsonDocPos, pJsonEnd, pDataPath, pChainedRaftJson, foundInChainedDoc);
        // Check if we found the element
        if (!pJsonDocPos)
            return RAFT_JSON_UNDEFINED;
        // Check if it's an object
        if (*pJsonDocPos == '{')
            return RAFT_JSON_OBJECT;
        // Check if it's an array
        const char* pDocEnd = (foundInChainedDoc && pChainedRaftJson) ? pChainedRaftJson->getJsonDocEnd() : pJsonEnd;
        if (*pJsonDocPos == '[')
        {
            // Skip over array start
            pJsonDocPos++;
            // Iterate over array elements
            arrayLen = 0;
            while (*pJsonDocPos && (pJsonDocPos < pDocEnd))
            {
                // Skip whitespace
                while (*pJsonDocPos && (pJsonDocPos < pDocEnd) && (*pJsonDocPos <= ' '))
                    pJsonDocPos++;
                // Check for end of array
                if (*pJsonDocPos == ']')
                    return RAFT_JSON_ARRAY;
                // Locate element
                const char* pElemStart = nullptr;
                const char* pElemEnd = nullptr;
                pJsonDocPos = RaftJson::locateElementBounds(pJsonDocPos, pDocEnd, pElemStart, pElemEnd);
                if (!pJsonDocPos)
                    return RAFT_JSON_UNDEFINED;
                // Count elements
                arrayLen++;
            }
            return RAFT_JSON_ARRAY;
        }
        // Check if it's a string
        if (*pJsonDocPos == '"')
            return RAFT_JSON_STRING;
        // Check if it's a boolean
        int retValue = 0;
        if (RaftJson::isBooleanIm(pJsonDocPos, retValue))
            return RAFT_JSON_BOOLEAN;
        // Check for null
        if (RaftJson::isNullIm(pJsonDocPos))
            return RAFT_JSON_NULL;
        // Check if it's a number (including negative numbers)
        if (((*pJsonDocPos >= '0') && (*pJsonDocPos <= '9')) || (*pJsonDocPos == '-'))
            return RAFT_JSON_NUMBER;
        // Must be undefined
        return RAFT_JSON_UNDEFINED;
    }

    ////////////////////////////////////////////////////////////////////////
    // Name value pair handling methods
    ////////////////////////////////////////////////////////////////////////

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
    static String getJSONFromNVPairs(std::vector<NameValuePair>& nameValuePairs, bool includeOuterBraces)
    {
        // Calculate length for efficiency
        uint32_t reserveLen = 0;
        for (NameValuePair& pair : nameValuePairs)
            reserveLen += 6 + pair.name.length() + pair.value.length();

        // Generate JSON
        String jsonStr;
        jsonStr.reserve(reserveLen);
        for (NameValuePair& pair : nameValuePairs)
        {
            if (jsonStr.length() > 0)
                jsonStr += ',';
            if (pair.value.startsWith("[") || pair.value.startsWith("{"))
                jsonStr += "\"" + pair.name + "\":" + pair.value;
            else
                jsonStr += "\"" + pair.name + "\":\"" + pair.value + "\"";
        }
        if (includeOuterBraces)
            return "{" + jsonStr + "}";
        return jsonStr;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Escape a string
    /// @param strToEsc : string in which to replace characters which are invalid in JSON
    static void escapeString(String& strToEsc)
    {
        // Replace characters which are invalid in JSON
        strToEsc.replace("\\", "\\\\");
        strToEsc.replace("\"", "\\\"");
        strToEsc.replace("\n", "\\n");
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Unescape a string
    /// @param strToUnEsc : string in which to restore characters which are invalid in JSON
    static void unescapeString(String& strToUnEsc)
    {
        // Replace characters which are invalid in JSON
        strToUnEsc.replace("\\\"", "\"");
        strToUnEsc.replace("\\\\", "\\");
        strToUnEsc.replace("\\n", "\n");
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Convert JSON object to HTML query string syntax
    /// @param jsonStr : JSON string
    /// @return String : HTML query string
    static String getHTMLQueryFromJSON(const String& jsonStr)
    {
        // Get keys of object
        std::vector<String> keyStrs;
        RaftJson::getKeysIm(jsonStr.c_str(), jsonStr.c_str()+jsonStr.length(), "", keyStrs);
        if (keyStrs.size() == 0)
            return "";

        // Fill object
        String outStr;
        for (String& keyStr : keyStrs)
        {
            String valStr = RaftJson::getStringIm(jsonStr.c_str(), jsonStr.c_str()+jsonStr.length(), keyStr.c_str(), "");
            if (valStr.length() == 0)
                continue;
            if (outStr.length() != 0)
                outStr += "&";
            outStr += keyStr + "=" + valStr;
        }
        return outStr;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Extract name-value pairs from string
    /// @param inStr : input string
    /// @param pNameValueSep : separator between name and value, e.g. "=" for HTTP
    /// @param pPairDelim : separator between pairs, e.g. "&" for HTTP
    /// @param pPairDelimAlt : alternate separator between pairs (pass 0 if not needed), e.g. ";" for HTTP
    /// @param nameValuePairs : vector of name-value pairs
    /// @return void
    static void extractNameValues(const String& inStr, 
                                const char* pNameValueSep, 
                                const char* pPairDelim, 
                                const char* pPairDelimAlt, 
                                std::vector<NameValuePair>& nameValuePairs) {
        // Calculate lengths of delimiters for optimization
        size_t nameValueSepLen = strlen(pNameValueSep);
        size_t pairDelimLen = strlen(pPairDelim);
        size_t pairDelimAltLen = pPairDelimAlt ? strlen(pPairDelimAlt) : 0;

        uint32_t startPos = 0;
        uint32_t endPos;

        while (startPos < inStr.length()) {
            // Find the next pair delimiter
            int nextPairDelimPos = inStr.indexOf(pPairDelim, startPos);
            int nextPairDelimAltPos = pPairDelimAlt ? inStr.indexOf(pPairDelimAlt, startPos) : -1;

            if (nextPairDelimPos == -1) nextPairDelimPos = inStr.length();
            if (nextPairDelimAltPos == -1) nextPairDelimAltPos = inStr.length();

            endPos = nextPairDelimPos < nextPairDelimAltPos ? nextPairDelimPos : nextPairDelimAltPos;

            // Extract the pair substring
            String pair = inStr.substring(startPos, endPos);

            // Find the separator between name and value
            int sepPos = pair.indexOf(pNameValueSep);
            String name, value;
            if (sepPos != -1) {
                name = pair.substring(0, sepPos);
                value = pair.substring(sepPos + nameValueSepLen);
            } else {
                name = pair;
            }
            name.trim();

            // Add to the vector
            nameValuePairs.emplace_back(name, value);

            // Move to the next part of the string
            startPos = endPos + (endPos == (uint32_t)nextPairDelimPos ? pairDelimLen : pairDelimAltLen);
        }
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get string representation of element type
    /// @param type : element type
    /// @return const char* : string representation of element type
    static const char* getElemTypeStr(RaftJsonType type)
    {
        switch (type)
        {
        case RAFT_JSON_STRING: return "STR";
        case RAFT_JSON_OBJECT: return "OBJ";
        case RAFT_JSON_ARRAY: return "ARRY";
        case RAFT_JSON_BOOLEAN: return "BOOL";
        case RAFT_JSON_NUMBER: return "NUM";
        case RAFT_JSON_NULL: return "NULL";
        case RAFT_JSON_UNDEFINED: return "UNDEF";
        }
        return "UNKN";
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get Element start by path
    /// @return const char* : Start of element
    const char* getElemStart(const char* pDataPath)
    {
        bool foundInChainedDoc = false;
        return locateElemByPath(_pSourceStr, _pSourceEnd, pDataPath, _pChainedRaftJson, foundInChainedDoc);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get JSON doc contents
    /// @return const char* : JSON doc contents
    const char* getJsonDoc() const override
    {
        return _pSourceStr;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get JSON doc end
    /// @return const char* : JSON doc end
    const char* getJsonDocEnd() const override
    {
        return _pSourceEnd;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get as a string
    /// @return String : JSON doc contents
    virtual String toString() const
    {
        // Extract string
        return getStringWithoutQuotes(_pSourceStr, _pSourceEnd, true);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Coerce to a double
    /// @return double : JSON doc contents
    virtual double toDouble() const
    {
        return String(_pSourceStr, _pSourceEnd - _pSourceStr).toDouble();
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get as a long
    /// @return long : JSON doc contents
    virtual int toInt() const
    {
        return String(_pSourceStr, _pSourceEnd - _pSourceStr).toInt();
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get as a long
    /// @return long : JSON doc contents
    virtual long toLong() const
    {
        return String(_pSourceStr, _pSourceEnd - _pSourceStr).toInt();
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get as a boolean
    /// @return bool : JSON doc contents
    virtual bool toBool() const
    {
        int retVal = 0;
        if (isBooleanIm(_pSourceStr, retVal))
            return retVal != 0;
        return String(_pSourceStr, _pSourceEnd - _pSourceStr).toInt() != 0;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get chained RaftJson object
    /// @return RaftJsonIF* : chained RaftJson object (may be null if no chaining)
    const RaftJsonIF* getChainedRaftJson() const override
    {
        return _pChainedRaftJson;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set chained RaftJson object
    /// @param pChainedRaftJson chained RaftJson object (may be null if chaining is to be disabled)
    virtual void setChainedRaftJson(const RaftJsonIF* pChainedRaftJson) override
    {
        _pChainedRaftJson = pChainedRaftJson;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set source string - the main JSON doc string used by this object
    /// @param pSourceStr source string
    /// @param makeCopy when false the string pointer must remain valid for the lifetime of this object
    /// @param pSourceEnd end of source string (location of null character or arbitrary location within the valid string bounds)
    void setSourceStr(const char* pSourceStr, bool makeCopy, const char* pSourceEnd)
    {
        // Check valid
        if (!pSourceStr || !pSourceEnd)
            return;

        // Make copy if required
        if (makeCopy)
        {
#ifdef USE_RAFT_SPIRAM_AWARE_ALLOCATOR
            _jsonStr = std::vector<char, SpiramAwareAllocator<char>>(pSourceStr, pSourceEnd + 1);
            _jsonStr[pSourceEnd - pSourceStr] = 0;
#else
            _jsonStr = std::vector<char>(pSourceStr, pSourceEnd + 1);
            _jsonStr[pSourceEnd - pSourceStr] = 0;
#endif
            // Reference the copy
            _pSourceStr = _jsonStr.data();
            _pSourceEnd = _pSourceStr + (_jsonStr.size() - 1);
        }
        else
        {
            // Reference the source string
            _pSourceStr = pSourceStr;
            _pSourceEnd = pSourceEnd;
        }
    }

    // Forward definition of iterator
    class ArrayIterator;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @class ArrayWrapper
    /// @brief wrapper for iterator for arrays in JSON
    class ArrayWrapper {
    public:
        ArrayWrapper(const RaftJsonIF* pJsonDoc, const char* pPath)
            : _pJsonDoc(pJsonDoc), _path(pPath)
        {
        }

        ArrayIterator begin() const {
            const char* pStart = _pJsonDoc->locateElementByPath(_path.c_str());
            return ArrayIterator(pStart, _pJsonDoc->getJsonDocEnd());
        }

        ArrayIterator end() const {
            return ArrayIterator(nullptr, nullptr);
        }

        RaftJson operator[](size_t index) const {
            const char* pStart = _pJsonDoc->locateElementByPath(_path.c_str());
            const char* pCurrent = pStart;
            const char* pDocEnd = _pJsonDoc->getJsonDocEnd();
            if (pCurrent && *pCurrent == '[') {
                pCurrent++;  // Move past the '['
                skipWhitespace(pCurrent, pDocEnd);
            }

            size_t currentIndex = 0;
            while (pCurrent && pCurrent < pDocEnd && *pCurrent != ']') {
                if (currentIndex == index) {
                    // Find the bounds of the current element
                    const char* pElemEnd = nullptr;
                    locateElementBounds(pCurrent, pDocEnd, pCurrent, pElemEnd);
                    return RaftJson(pCurrent, false, pElemEnd);
                }

                // Move to next element
                const char* pElemEnd = nullptr;
                pCurrent = locateElementBounds(pCurrent, pDocEnd, pCurrent, pElemEnd);
                skipWhitespace(pCurrent, pDocEnd);
                if (*pCurrent == ',') {
                    pCurrent++;
                    skipWhitespace(pCurrent, pDocEnd);
                }
                currentIndex++;
            }
            return RaftJson();
        }

        size_t size() const {
            const char* pStart = _pJsonDoc->locateElementByPath(_path.c_str());
            const char* pCurrent = pStart;
            const char* pDocEnd = _pJsonDoc->getJsonDocEnd();
            if (pCurrent && *pCurrent == '[') {
                pCurrent++;  // Move past the '['
                skipWhitespace(pCurrent, pDocEnd);
            } else {
                return 0; // Not a valid array
            }

            size_t count = 0;
            while (pCurrent && pCurrent < pDocEnd && *pCurrent != ']') {
                // Find the end of the current element
                const char* pElemEnd = nullptr;
                pCurrent = locateElementBounds(pCurrent, pDocEnd, pCurrent, pElemEnd);
                if (!pCurrent || pCurrent >= pDocEnd) {
                    break;
                }
                count++;
                skipWhitespace(pCurrent, pDocEnd);
                if (*pCurrent == ',') {
                    pCurrent++;
                    skipWhitespace(pCurrent, pDocEnd);
                }
            }
            return count;
        }        
        
    private:
        const RaftJsonIF* _pJsonDoc;
        const String _path;
    };

    // Method to get the array wrapper
    ArrayWrapper getArray(const char* pPath) const {
        return ArrayWrapper(this, pPath);
    }

    class ArrayIterator {
    public:
        // Construct the iterator with a pointer to the JSON document and the start of the array
        ArrayIterator(const char* pStart, const char* pEnd)
            : _pStart(pStart), _pCurrent(pStart), _pDocEnd(pEnd), _atEnd(pStart == nullptr || pStart == pEnd) 
        {
            if (_pCurrent && *_pCurrent == '[') 
            {
                // Move past the opening bracket of the array
                _pCurrent++;
                skipWhitespace(_pCurrent, _pDocEnd);
                if (*_pCurrent == ']') 
                {
                    _atEnd = true; // Handle empty array
                }
            }
        }

        // Move to the next element in the array
        ArrayIterator& operator++() 
        {
            if (!_atEnd && _pCurrent) 
            {
                const char* pElemEnd = nullptr;
                // Find the bounds of the current element
                _pCurrent = locateElementBounds(_pCurrent, _pDocEnd, _pCurrent, pElemEnd);
                if (!_pCurrent)
                {
                    _atEnd = true;
                    return *this;
                }
                skipWhitespace(_pCurrent, _pDocEnd);
                if (*_pCurrent == ']') 
                {
                    _atEnd = true;
                    _pCurrent = nullptr;
                }
                else if (*_pCurrent == ',') 
                {
                    _pCurrent++;
                    skipWhitespace(_pCurrent, _pDocEnd);
                }
            }
            return *this;
        }

        // Access the element the iterator points to
        RaftJson operator*() const 
        {
            const char* pElemEnd = nullptr;
            const char* pElemStart = _pCurrent;
            locateElementBounds(_pCurrent, _pDocEnd, pElemStart, pElemEnd);
            return RaftJson(pElemStart, false, pElemEnd);
        }

        // Compare two iterators
        bool operator!=(const ArrayIterator& other) const 
        {
            return _pCurrent != other._pCurrent || _atEnd != other._atEnd;
        }

    private:
        const char* _pStart = nullptr;
        const char* _pCurrent = nullptr;
        const char* _pDocEnd = nullptr;
        bool _atEnd = false;
    };

    // Forward definition of iterator
    class ObjectIterator;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @class ObjectWrapper
    /// @brief wrapper for iterator for objects in JSON
    class ObjectWrapper {
    public:
        ObjectWrapper(const RaftJsonIF* pJsonDoc, const char* pPath)
            : _pJsonDoc(pJsonDoc), _path(pPath)
        {
        }

        ObjectIterator begin() const {
            const char* pStart = _pJsonDoc->locateElementByPath(_path.c_str());
            return ObjectIterator(pStart, _pJsonDoc->getJsonDocEnd());
        }

        ObjectIterator end() const {
            return ObjectIterator(nullptr, nullptr);
        }

    private:
        const RaftJsonIF* _pJsonDoc;
        const String _path;
    };

    // Method to get the object wrapper
    ObjectWrapper getObject(const char* pPath) const {
        return ObjectWrapper(this, pPath);
    }

    class ObjectIterator {
    public:
        ObjectIterator(const char* pStart, const char* pEnd)
            : _pCurrent(pStart), _pDocEnd(pEnd), _atEnd(pStart == nullptr || pStart == pEnd)
        {
            if (_pCurrent && *_pCurrent == '{') {
                _pCurrent++;
                skipWhitespace(_pCurrent, _pDocEnd);
                if (*_pCurrent == '}') {
                    _atEnd = true; // Handle empty object
                }
            }
        }

        ObjectIterator& operator++() {
            if (!_atEnd && _pCurrent) 
            {
                const char* pElemEnd = nullptr;
                // Find the bounds of the key
                _pCurrent = locateElementBounds(_pCurrent, _pDocEnd, _pCurrent, pElemEnd);
                if (!_pCurrent)
                {
                    _atEnd = true;
                    return *this;
                }
                skipWhitespace(_pCurrent, _pDocEnd);
                if (*_pCurrent == ':')
                {
                    _pCurrent++;
                }
                // Find the bounds of the value
                _pCurrent = locateElementBounds(_pCurrent, _pDocEnd, _pCurrent, pElemEnd);
                if (!_pCurrent)
                {
                    _atEnd = true;
                    return *this;
                }
                skipWhitespace(_pCurrent, _pDocEnd);
                if (*_pCurrent == '}') 
                {
                    _atEnd = true;
                    _pCurrent = nullptr;
                }
                else if (*_pCurrent == ',') 
                {
                    _pCurrent++;
                    skipWhitespace(_pCurrent, _pDocEnd);
                }
            }
            return *this;
        }

        std::pair<String, RaftJson> operator*() const {
            // Locate key
            const char* pKeyStart = nullptr;
            const char* pKeyEnd = nullptr;
            const char* pJsonDocPos = locateStringElement(_pCurrent, _pDocEnd, pKeyStart, pKeyEnd, false);
            String key;
            if (pJsonDocPos)
                key = String(pKeyStart, pKeyEnd - pKeyStart);

            // Find value
            skipWhitespace(pJsonDocPos, _pDocEnd);
            if (*pJsonDocPos == ':')
                pJsonDocPos++;
            skipWhitespace(pJsonDocPos, _pDocEnd);
            const char* pValueEnd = nullptr;
            locateElementBounds(pJsonDocPos, _pDocEnd, pJsonDocPos, pValueEnd);
            return {key, RaftJson(pJsonDocPos, false, pValueEnd)};
        }

        bool operator!=(const ObjectIterator& other) const {
            return _pCurrent != other._pCurrent || _atEnd != other._atEnd;
        }

    private:
        const char* _pCurrent;
        const char* _pDocEnd;
        bool _atEnd;
    };    

protected:
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Locate an element in a JSON document using a path
    /// @param pPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @return the position of the element or nullptr if not found
    virtual const char* locateElementByPath(const char* pPath) const override
    {
        bool foundInChainedDoc = false;
        return locateElemByPath(_pSourceStr, _pSourceEnd, pPath, _pChainedRaftJson, foundInChainedDoc);
    }

private:
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Skip whitespace characters
    /// @param pCur the current position in the JSON document
    /// @param pEnd the end of the JSON document
    static void skipWhitespace(const char*& pCur, const char* pEnd) {
        while ((pCur < pEnd) && (*pCur == ' ' || *pCur == '\n' || *pCur == '\r' || *pCur == '\t')) {
            pCur++;
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Locate a string element from the current position in a JSON document
    /// @param pJsonDocPos the current position in the JSON document
    /// @param pJsonEnd the end of the JSON document
    /// @param pElemStart [out] the start of the element
    /// @param pElemEnd [out] the end of the element
    /// @param includeQuotes if true include the quotes in the returned element
    /// @return a position in the document after the end of the element or nullptr if not found
    static const char* locateStringElement(const char* pJsonDocPos, const char* pJsonEnd,
            const char*& pElemStart, const char*& pElemEnd, bool includeQuotes = false)
    {
        // Skip quote if at start of string
        if (!includeQuotes && (*pJsonDocPos == '"') && (pJsonDocPos < pJsonEnd))
            pJsonDocPos++;

        // Find end of string
        pElemStart = pJsonDocPos;
        bool isEscaped = false;
        while (*pJsonDocPos && (pJsonDocPos < pJsonEnd) && (isEscaped || (*pJsonDocPos != '"')))
        {
            isEscaped = (*pJsonDocPos == '\\');
            pJsonDocPos++;
        }

        // Return string start and end
        if (*pJsonDocPos == '"')
        {
            pElemEnd = includeQuotes ? pJsonDocPos + 1 : pJsonDocPos;
            return pJsonDocPos + 1;
        }
        return nullptr;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Locate the bounds of an element in a JSON document
    /// @param pJsonDocPos the current position in the JSON document
    /// @param pJsonEnd the end of the JSON document
    /// @param pElemStart [out] the start of the element
    /// @param pElemEnd [out] the end of the element
    /// @return a position in the document after the end of the element or nullptr if not found
    static const char* locateElementBounds(const char* pJsonDocPos, const char* pJsonEnd, const char*& pElemStart, const char*& pElemEnd)
    {
        // Skip whitespace, commas and colons
        while (*pJsonDocPos && (pJsonDocPos < pJsonEnd) && ((*pJsonDocPos <= ' ') || (*pJsonDocPos == ',') || (*pJsonDocPos == ':')))
            pJsonDocPos++;
        if (!*pJsonDocPos || (pJsonDocPos >= pJsonEnd))
            return nullptr;
        pElemStart = pJsonDocPos;
            
        // Check for kind of element
        if ((*pJsonDocPos == '{') || (*pJsonDocPos == '['))
        {
#ifdef DEBUG_JSON_LOCATE_ELEMENT_BOUNDS
            LOG_I("RaftJson", "locateElementBounds found object/array pJsonDocPos %p jsonDoc ", 
                        pJsonDocPos, pJsonDocPos ? pJsonDocPos : "null");
#endif
            // Find end of object
            char braceChar = *pJsonDocPos;
            int numBraces = 1;
            pJsonDocPos++;
            // Skip to end of object
            bool insideString = false;
            while (*pJsonDocPos && (pJsonDocPos < pJsonEnd) && (numBraces > 0))
            {
                if (*pJsonDocPos == '"')
                {
                    insideString = !insideString;
#ifdef DEBUG_JSON_LOCATE_ELEMENT_BOUNDS
                    LOG_I("RaftJson", "locateElementBounds idx %d %s", 
                            pJsonDocPos-pElemStart,
                            insideString ? "INSIDE_STR" : "OUTSIDE_STR");
#endif
                }
                if (!insideString)
                {
                    if (*pJsonDocPos == braceChar)
                    {
                        numBraces++;
#ifdef DEBUG_JSON_LOCATE_ELEMENT_BOUNDS
                    LOG_I("RaftJson", "locateElementBounds idx %d OPEN_BRACE %d", 
                            pJsonDocPos-pElemStart, numBraces);
#endif
                    }
                    else if (*pJsonDocPos == ((braceChar == '{') ? '}' : ']'))
                    {
                        numBraces--;
#ifdef DEBUG_JSON_LOCATE_ELEMENT_BOUNDS
                    LOG_I("RaftJson", "locateElementBounds idx %d CLOSE_BRACE %d", 
                            pJsonDocPos-pElemStart, numBraces);
#endif
                    }
                }
                pJsonDocPos++;
#ifdef DEBUG_JSON_LOCATE_ELEMENT_BOUNDS
                LOG_I("RaftJson", "locateElementBounds LOOPING idx %d ch <<<%c>>>", 
                        pJsonDocPos-pElemStart, *pJsonDocPos ? *pJsonDocPos : '~');
#endif            
            }
            if ((numBraces > 0) && (!*pJsonDocPos))
            {
#ifdef DEBUG_JSON_LOCATE_ELEMENT_BOUNDS
                LOG_I("RaftJson", "locateElementBounds obj unexpectedly reached end of document pJsonDocPos %p", pJsonDocPos);
#endif
                return nullptr;
            }
            pElemEnd = pJsonDocPos;
            // Skip whitespace and commas
            while (*pJsonDocPos && (pJsonDocPos < pJsonEnd) && ((*pJsonDocPos <= ' ') || (*pJsonDocPos == ',')))
                pJsonDocPos++;
            return pJsonDocPos;
        }
        else if (*pJsonDocPos == '"')
        {
#ifdef DEBUG_JSON_LOCATE_ELEMENT_BOUNDS
            LOG_I("RaftJson", "locateElementBounds found string pJsonDocPos %p", pJsonDocPos);
#endif
            // Find end of string
            pJsonDocPos++;
            bool isEscaped = false;
            while (*pJsonDocPos && (pJsonDocPos < pJsonEnd) && (isEscaped || (*pJsonDocPos != '"')))
            {
                isEscaped = (*pJsonDocPos == '\\');
                pJsonDocPos++;
            }
            if (!*pJsonDocPos || (pJsonDocPos >= pJsonEnd))
            {
#ifdef DEBUG_JSON_LOCATE_ELEMENT_BOUNDS
                LOG_I("RaftJson", "locateElementBounds str unexpectedly reached end of document pJsonDocPos %p", pJsonDocPos);
#endif
                return nullptr;
            }
            // Check if we're on the closing quotes
            pElemEnd = pJsonDocPos;
            if (*pJsonDocPos == '"')
                pElemEnd = pJsonDocPos + 1;
            pJsonDocPos++;
            // Skip whitespace and commas
            while (*pJsonDocPos && (pJsonDocPos < pJsonEnd) && ((*pJsonDocPos <= ' ') || (*pJsonDocPos == ',')))
                pJsonDocPos++;
            return pJsonDocPos;
        }
        else
        {
#ifdef DEBUG_JSON_LOCATE_ELEMENT_BOUNDS
            LOG_I("RaftJson", "locateElementBounds found number pJsonDocPos %p", pJsonDocPos);
#endif        
            // Find end of element
            while (*pJsonDocPos && (pJsonDocPos < pJsonEnd) && (*pJsonDocPos > ' ') && (*pJsonDocPos != ',') && (*pJsonDocPos != '}') && (*pJsonDocPos != ']'))
                pJsonDocPos++;
            pElemEnd = pJsonDocPos;
            // Skip whitespace and commas
            while (*pJsonDocPos && (pJsonDocPos < pJsonEnd) && ((*pJsonDocPos <= ' ') || (*pJsonDocPos == ',')))
                pJsonDocPos++;
            if (!*pJsonDocPos)
            {
#ifdef DEBUG_JSON_LOCATE_ELEMENT_BOUNDS
                LOG_I("RaftJson", "locateElementBounds num unexpectedly reached end of document pJsonDocPos %p", pJsonDocPos);
#endif
            }
            return pJsonDocPos;
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Locate an element in a JSON document using a single key (part of a path)
    /// @param pJsonDocPos the current position in the JSON document
    /// @param pJsonEnd the end of the JSON document
    /// @param pReqdKey [in/out] the key of the required variable (note this is modified by the function)
    /// @return the position of the element or nullptr if not found
    /// @note The key can be empty in which case the entire object is returned
    /// @note The key can be an array index (e.g. "[0]") in which case the value at that index is returned if the element is an array
    /// @note The key can be a string in which case the value for that key is returned if the element is an object
    static const char* locateElementValueWithKey(const char* pJsonDocPos, const char* pJsonEnd, const char*& pReqdKey)
    {
        // Check valid
        if (!pJsonDocPos || !pJsonEnd)
            return nullptr;

        // If key is empty return the entire element
        if (!pReqdKey || !*pReqdKey || (*pReqdKey == '/'))
        {
            // Skip any whitespace
            while (*pJsonDocPos && (pJsonDocPos < pJsonEnd) && (*pJsonDocPos <= ' '))
                pJsonDocPos++;

#ifdef DEBUG_JSON_BY_SPECIFIC_PATH_PART
        if (strcmp(pReqdKey, DEBUG_JSON_BY_SPECIFIC_PATH_PART) == 0)
        {
            LOG_I(RAFT_JSON_PREFIX, "locateElementValueWithKey key <<<%s>>> jsonDoc %s", pReqdKey, pJsonDocPos);
        }
#endif

            // Move key position to next part of path
            if (pReqdKey && (*pReqdKey == '/'))
                pReqdKey++;
            return pJsonDocPos;
        }

        // Skip any whitespace
        while (*pJsonDocPos && (pJsonDocPos < pJsonEnd) && (*pJsonDocPos <= ' '))
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
        while (*pJsonDocPos && (pJsonDocPos < pJsonEnd))
        {
            // Check for object - in which case what comes first is the key
            if (isObject)
            {
                // Skip to start of key
                while (*pJsonDocPos && (pJsonDocPos < pJsonEnd) && (*pJsonDocPos != '"'))
                    pJsonDocPos++;
                if (!*pJsonDocPos || (pJsonDocPos >= pJsonEnd))
                    return nullptr;
                // Extract key string
                pJsonDocPos = locateStringElement(pJsonDocPos, pJsonEnd, pKeyStart, pKeyEnd, false);
                if (!pJsonDocPos)
                    return nullptr;
                // Skip over any whitespace and colons
                while (*pJsonDocPos && (pJsonDocPos < pJsonEnd) && ((*pJsonDocPos <= ' ') || (*pJsonDocPos == ':')))
                    pJsonDocPos++;
                if (!*pJsonDocPos || (pJsonDocPos >= pJsonEnd))
                    return nullptr;
                // Check for longer of key and path part
                uint32_t keyLen = (pKeyEnd - pKeyStart) > (pReqdKeyEnd - pReqdKeyStart) ? (pKeyEnd - pKeyStart) : (pReqdKeyEnd - pReqdKeyStart);
                if (strncmp(pKeyStart, pReqdKeyStart, keyLen) == 0)
                    return pJsonDocPos;
                // Skip whitespace
                while (*pJsonDocPos && (pJsonDocPos < pJsonEnd) && (*pJsonDocPos <= ' '))
                    pJsonDocPos++;
            }
            else
            {
                if (arrayIdx == elemCount)
                    return pJsonDocPos;
                elemCount++;
                // Skip whitespace
                while (*pJsonDocPos && (pJsonDocPos < pJsonEnd) &&(*pJsonDocPos <= ' '))
                    pJsonDocPos++;
            }

            // Skip to end of element
            const char* pElemEnd = nullptr;
            const char* pElemStart = nullptr;
            pJsonDocPos = RaftJson::locateElementBounds(pJsonDocPos, pJsonEnd, pElemStart, pElemEnd);
            if (!pJsonDocPos)
                return nullptr;

            // Check if we've reached the end of the object or array
            if ((*pJsonDocPos == '}') || (*pJsonDocPos == ']'))
                return nullptr;
        }
        return nullptr;
    }    

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Locate an element in a JSON document using a path
    /// @param pJsonDocPos the current position in the JSON document
    /// @param pJsonEnd the end of the JSON document
    /// @param pPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @param foundInChainedDoc [out] set to true if the element was found in the chained document
    /// @return the position of the element or nullptr if not found
    static const char* locateElemByPath(const char* pJsonDocPos, const char* pJsonEnd, 
                const char* pPath,
                const RaftJsonIF* pChainedRaftJson,
                bool& foundInChainedDoc)
    {
        // Check valid
        foundInChainedDoc = false;
        if (!pPath || !pJsonDocPos || !pJsonEnd)
            return nullptr;

        // Iterate over path
        const char* pPathPos = pPath;
#ifdef DEBUG_JSON_BY_SPECIFIC_PATH
        if (strcmp(pPath, DEBUG_JSON_BY_SPECIFIC_PATH) == 0)
        {
            if (!strstr(DEBUG_JSON_BY_SPECIFIC_PATH, "/"))
            {
                const char* pTestStr = DEBUG_JSON_BY_SPECIFIC_PATH;
                const char* pMatch = strstr(pJsonDocPos, pTestStr);
                LOG_I(RAFT_JSON_PREFIX, "locateElemByPath path <<<%s>>> jsonDoc %p chainedPtr %p pMatch %p", pPath, pJsonDocPos, pChainedRaftJson, pMatch);
            }
            else
            {
                LOG_I(RAFT_JSON_PREFIX, "locateElemByPath path <<<%s>>> trying jsonDoc %p chainedPtr %p", pPath, pJsonDocPos, pChainedRaftJson);
            }
        }
#endif

#ifdef DEBUG_CHAINED_RAFT_JSON
        const char* pOriginalDoc = pJsonDocPos;
#endif
        while(true)
        {
            // Locate element (note that pPath is modified by each call)
            pJsonDocPos = locateElementValueWithKey(pJsonDocPos, pJsonEnd, pPathPos);
            if (!pJsonDocPos)
            {
#ifdef DEBUG_CHAINED_RAFT_JSON
                LOG_I(RAFT_JSON_PREFIX, "locateElemByPath path %s not found, chainedPtr %p originalDoc %s", 
                            pPath, pChainedRaftJson, pOriginalDoc ? pOriginalDoc : "null");
#endif
                // Handle chained documents searching with the original path
                const char* pFoundLocation = pChainedRaftJson ? pChainedRaftJson->locateElementByPath(pPath) : nullptr;
                if (pFoundLocation)
                {
                    foundInChainedDoc = true;
                    return pFoundLocation;
                }
                return nullptr;
            }

            // Check if we're at the end of the path
            if (!*pPathPos)
            {
#ifdef DEBUG_JSON_BY_SPECIFIC_PATH
                if (strcmp(pPath, DEBUG_JSON_BY_SPECIFIC_PATH) == 0)
                {
                    char elemText[32];
                    strlcpy(elemText, pJsonDocPos, 31);
                    LOG_I(RAFT_JSON_PREFIX, "locateElemByPath path <<<%s>>> returning jsonDoc %p str %s", pPath, pJsonDocPos, elemText);
                }
#endif
                return pJsonDocPos;
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get string without quotes from element bounds
    /// @param pElemStart JSON string element start position (generally points to a quote mark)
    /// @param pElemEnd JSON string element end position (generally points to the char after a quote mark)
    /// @param unescape if true unescape if it is a string
    /// @return string without quotes
    /// @note This function is used to extract a string from the bounds of a JSON element which will generally
    ///       include quote marks.
    static String getStringWithoutQuotes(const char* pElemStart, const char* pElemEnd, bool unescape = true)
    {
        // Check valid
        if (!pElemStart || !pElemEnd)
            return String();
        // Check if string start bounds is a quote
        bool isString = (pElemEnd > pElemStart) && (*pElemStart == '"');
        if (isString)
            pElemStart++;
        // Check if string end bounds is a quote
        if ((pElemEnd > pElemStart) && (*(pElemEnd-1) == '"'))
            pElemEnd--;
        // Return string without quotes
        String retStr = String(pElemStart, pElemEnd - pElemStart);
        if (unescape && isString)
            unescapeString(retStr);
        return retStr;

    }

private:
    // JSON document string
#ifdef USE_RAFT_SPIRAM_AWARE_ALLOCATOR
    std::vector<char, SpiramAwareAllocator<char>> _jsonStr;
#else
    std::vector<char> _jsonStr;
#endif

    // Empty JSON document
    static constexpr const char* EMPTY_JSON_DOCUMENT = "{}";

    // Copy of pointer to JSON string
    // Note this pointer should NEVER be deleted by this object
    // It may be a pointer to a string in flash memory
    // Or it may be a pointer to the string in the _jsonStr vector
    // Or it may be a pointer to a static string which contains the empty JSON document {}
    const char* _pSourceStr = EMPTY_JSON_DOCUMENT;

    // Pointer to end of JSON document (location of null character or other location within the string)
    const char* _pSourceEnd = _pSourceStr + strlen(_pSourceStr);

    // Pointer to an alternate RaftJsonIF implementation
    // This allows chaining so that if a value is not found in this object
    // it can be searched for in another object
    // This pointer should not be deleted by this object
    const RaftJsonIF* _pChainedRaftJson = nullptr;

    // Debug
    static constexpr const char *RAFT_JSON_PREFIX = "RaftJson";
};
