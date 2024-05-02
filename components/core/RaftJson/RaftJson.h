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
// #define DEBUG_JSON_BY_SPECIFIC_PATH ""
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

// RaftJson class
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
    /// @param pJsonStr 
    /// @param makeCopy when false the string pointer must remain valid for the lifetime of this object
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @note The makeCopy option is provided to avoid copying strings in flash memory - please don't use it in other cases
    RaftJson(const char* pJsonStr, bool makeCopy = true, RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Store the source string
        setSourceStr(pJsonStr, makeCopy, strlen(pJsonStr));
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
        setSourceStr(jsonStr.c_str(), true, jsonStr.length());
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
        setSourceStr(jsonStr.c_str(), true, jsonStr.length());
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
        setSourceStr(pJsonStr, true, strlen(pJsonStr));
        return *this;
    }
    RaftJson& operator=(const String& jsonStr)
    {
        // Store the source string
        setSourceStr(jsonStr.c_str(), true, jsonStr.length());
        return *this;
    }
    RaftJson& operator=(const std::string& jsonStr)
    {
        // Store the source string
        setSourceStr(jsonStr.c_str(), true, jsonStr.length());
        return *this;
    }
    virtual bool setJsonDoc(const char* pJsonDoc) override
    {
        // Store the source string
        setSourceStr(pJsonDoc, true, strlen(pJsonDoc));
        return true;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get string value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual String getString(const char* pDataPath, const char* defaultValue) const override
    {
        return getStringIm(_pSourceStr, pDataPath, defaultValue, _pChainedRaftJson);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get double value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual double getDouble(const char* pDataPath, double defaultValue) const override
    {
        return getDoubleIm(_pSourceStr, pDataPath, defaultValue, _pChainedRaftJson);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get long value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual long getLong(const char* pDataPath, long defaultValue) const override
    {
        return getLongIm(_pSourceStr, pDataPath, defaultValue, _pChainedRaftJson);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get boolean value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual bool getBool(const char* pDataPath, bool defaultValue) const override
    {
        return getBoolIm(_pSourceStr, pDataPath, defaultValue, _pChainedRaftJson);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get array elements using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param strList a vector which is filled with the array elements
    /// @return true if the array was found
    virtual bool getArrayElems(const char *pDataPath, std::vector<String>& strList) const override
    {
        return getArrayElemsIm(_pSourceStr, pDataPath, strList, _pChainedRaftJson);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get keys of an object using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param keysVector a vector which is filled with the keys
    /// @return true if the object was found
    virtual bool getKeys(const char *pDataPath, std::vector<String>& keysVector) const override
    {
        return getKeysIm(_pSourceStr, pDataPath, keysVector, _pChainedRaftJson);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Check if the member JSON document contains the key specified by the path
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @return true if the key was found
    virtual bool contains(const char* pDataPath) const override
    {
        int arrayLen = 0;
        RaftJsonType elemType = getTypeIm(_pSourceStr, pDataPath, arrayLen, _pChainedRaftJson);
        return elemType != RAFT_JSON_UNDEFINED;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get type of element from a JSON document at the specified path
    /// @param pDataPath the path of the required object in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param arrayLen the length of the array if the element is an array
    /// @return the type of the element
    virtual RaftJsonType getType(const char* pDataPath, int &arrayLen) const override
    {
        return getTypeIm(_pSourceStr, pDataPath, arrayLen, _pChainedRaftJson);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Static methods
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief gets a string from a JSON document (immediate - i.e. static function, doc passed in)
    /// @param pJsonDoc the JSON document (string)
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @return the value of the variable or the default value if not found
    static String getStringIm(const char* pJsonDoc,
            const char* pDataPath, const char* defaultValue,
            const RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Locate the element
        const char* pJsonDocPos = pJsonDoc;
        pJsonDocPos = RaftJson::locateElementByPath(pJsonDocPos, pDataPath, pChainedRaftJson);
        // Check if we found the element
        if (!pJsonDocPos)
            return defaultValue;
        const char* pElemStart = nullptr;
        const char* pElemEnd = nullptr;
        if (!RaftJson::locateElementBounds(pJsonDocPos, pElemStart, pElemEnd))
            return defaultValue;
        // Skip quotes
        if (*pElemStart == '"')
            pElemStart++;
        String outStr = String(pElemStart, pElemEnd - pElemStart);
        unescapeString(outStr);
        return outStr;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief gets a double from a JSON document (immediate - i.e. static function, doc passed in)
    /// @param pJsonDoc the JSON document (string)
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @return the value of the variable or the default value if not found
    static double getDoubleIm(const char* pJsonDoc,
            const char* pDataPath, double defaultValue,
            const RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Locate the element
        const char* pJsonDocPos = pJsonDoc;
        pJsonDocPos = RaftJson::locateElementByPath(pJsonDocPos, pDataPath, pChainedRaftJson);
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
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @return the value of the variable or the default value if not found
    static long getLongIm(const char* pJsonDoc,
            const char* pDataPath, long defaultValue,
            const RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Locate the element
        const char* pJsonDocPos = pJsonDoc;
        pJsonDocPos = RaftJson::locateElementByPath(pJsonDocPos, pDataPath, pChainedRaftJson);
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
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @return the value of the variable or the default value if not found
    static bool getBoolIm(const char* pJsonDoc, 
            const char* pDataPath, bool defaultValue,
            const RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Use long method to get value
        return RaftJson::getLongIm(pJsonDoc, pDataPath, defaultValue, pChainedRaftJson) != 0;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief gets the elements of an array from the JSON (immediate - i.e. static function, doc passed in)
    /// @param pJsonDoc the JSON document (string)
    /// @param pDataPath the path of the required array in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param strList a vector which is filled with the array elements
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @return true if the array was found
    static bool getArrayElemsIm(const char* pJsonDoc,
            const char *pDataPath, std::vector<String>& strList,
            const RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Locate the element
        const char* pJsonDocPos = pJsonDoc;
        pJsonDocPos = RaftJson::locateElementByPath(pJsonDocPos, pDataPath, pChainedRaftJson);
        // Check if we found the element
        if (!pJsonDocPos)
            return false;
        // Check if it's an array
        if (*pJsonDocPos != '[')
            return false;
        // Skip over array start
        pJsonDocPos++;
        // Iterate over array elements
        while (*pJsonDocPos)
        {
            // Skip whitespace
            while (*pJsonDocPos && (*pJsonDocPos <= ' '))
                pJsonDocPos++;
            // Check for end of array
            if (*pJsonDocPos == ']')
                return true;
            // Locate element
            const char* pElemStart = nullptr;
            const char* pElemEnd = nullptr;
            pJsonDocPos = RaftJson::locateElementBounds(pJsonDocPos, pElemStart, pElemEnd);
            if (!pJsonDocPos)
                return false;
            // Skip quotes
            if (*pElemStart == '"')
                pElemStart++;
            // Add to list
            strList.push_back(String(pElemStart, pElemEnd - pElemStart));
        }
        return true;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief gets the keys of an object from the JSON (immediate - i.e. static function, doc passed in)
    /// @param pJsonDoc the JSON document (string)
    /// @param pDataPath the path of the required object in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param keysVector a vector which is filled with the keys
    /// @param pChainRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @return true if the object was found
    static bool getKeysIm(const char* pJsonDoc,
            const char *pDataPath, std::vector<String>& keysVector,
            const RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Locate the element
        const char* pJsonDocPos = pJsonDoc;
        pJsonDocPos = RaftJson::locateElementByPath(pJsonDocPos, pDataPath, pChainedRaftJson);
        // Check if we found the element
        if (!pJsonDocPos)
            return false;
        // Check if it's an object
        if (*pJsonDocPos != '{')
            return false;
        // Skip over object start
        pJsonDocPos++;
        // Iterate over object elements
        while (*pJsonDocPos)
        {
            // Skip whitespace
            while (*pJsonDocPos && (*pJsonDocPos <= ' '))
                pJsonDocPos++;
            // Check for end of object
            if (*pJsonDocPos == '}')
                return true;
            // Locate key
            const char* pKeyStart = nullptr;
            const char* pKeyEnd = nullptr;
            pJsonDocPos = locateStringElement(pJsonDocPos, pKeyStart, pKeyEnd, false);
            if (!pJsonDocPos)
                return false;
            // Add to list
            keysVector.push_back(String(pKeyStart, pKeyEnd - pKeyStart));
            // Skip to end of element
            const char* pElemEnd = nullptr;
            const char* pElemStart = nullptr;
            pJsonDocPos = RaftJson::locateElementBounds(pJsonDocPos, pElemStart, pElemEnd);
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
    /// @param pDataPath the path of the required object in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param arrayLen the length of the array if the element is an array
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @return the type of the element
    static RaftJsonType getTypeIm(const char* pJsonDoc,
            const char* pDataPath, int &arrayLen,
            const RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Locate the element
        const char* pJsonDocPos = pJsonDoc;
        pJsonDocPos = RaftJson::locateElementByPath(pJsonDocPos, pDataPath, pChainedRaftJson);
        // Check if we found the element
        if (!pJsonDocPos)
            return RAFT_JSON_UNDEFINED;
        // Check if it's an object
        if (*pJsonDocPos == '{')
            return RAFT_JSON_OBJECT;
        // Check if it's an array
        if (*pJsonDocPos == '[')
        {
            // Skip over array start
            pJsonDocPos++;
            // Iterate over array elements
            arrayLen = 0;
            while (*pJsonDocPos)
            {
                // Skip whitespace
                while (*pJsonDocPos && (*pJsonDocPos <= ' '))
                    pJsonDocPos++;
                // Check for end of array
                if (*pJsonDocPos == ']')
                    return RAFT_JSON_ARRAY;
                // Locate element
                const char* pElemStart = nullptr;
                const char* pElemEnd = nullptr;
                pJsonDocPos = RaftJson::locateElementBounds(pJsonDocPos, pElemStart, pElemEnd);
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
        // Check if it's a number
        if ((*pJsonDocPos >= '0') && (*pJsonDocPos <= '9'))
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
        RaftJson::getKeysIm(jsonStr.c_str(), "", keyStrs);
        if (keyStrs.size() == 0)
            return "";

        // Fill object
        String outStr;
        for (String& keyStr : keyStrs)
        {
            String valStr = RaftJson::getStringIm(jsonStr.c_str(), keyStr.c_str(), "");
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
    /// @brief Get JSON doc contents
    /// @return const char* : JSON doc contents
    const char* getJsonDoc() const override
    {
        return _pSourceStr;
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
    /// @param sourceStrLen length of source string
    void setSourceStr(const char* pSourceStr, bool makeCopy, uint32_t sourceStrLen)
    {
        // Check valid
        if (!pSourceStr)
            return;

        // Make copy if required
        if (makeCopy)
        {
#ifdef USE_RAFT_SPIRAM_AWARE_ALLOCATOR
            _jsonStr = std::vector<char, SpiramAwareAllocator<char>>(pSourceStr, pSourceStr + sourceStrLen + 1);
#else
            _jsonStr = std::vector<char>(pSourceStr, pSourceStr + sourceStrLen + 1);
#endif
            // Reference the copy
            _pSourceStr = _jsonStr.data();
        }
        else
        {
            // Reference the source string
            _pSourceStr = pSourceStr;
        }
    }

protected:
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Locate an element in a JSON document using a path
    /// @param pPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @return the position of the element or nullptr if not found
    virtual const char* locateElementByPath(const char* pPath) const override
    {
        return locateElementByPath(_pSourceStr, pPath, _pChainedRaftJson);
    }

private:
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Locate a string element from the current position in a JSON document
    /// @param pJsonDocPos the current position in the JSON document
    /// @param pElemStart [out] the start of the element
    /// @param pElemEnd [out] the end of the element
    /// @param includeQuotes if true include the quotes in the returned element
    /// @return a position in the document after the end of the element or nullptr if not found
    static const char* locateStringElement(const char* pJsonDocPos, const char*& pElemStart, const char*& pElemEnd, bool includeQuotes = false)
    {
        // Skip quote if at start of string
        if (!includeQuotes && (*pJsonDocPos == '"'))
            pJsonDocPos++;

        // Find end of string
        pElemStart = pJsonDocPos;
        bool isEscaped = false;
        while (*pJsonDocPos && (isEscaped || (*pJsonDocPos != '"')))
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
    /// @param pElemStart [out] the start of the element
    /// @param pElemEnd [out] the end of the element
    /// @return a position in the document after the end of the element or nullptr if not found
    static const char* locateElementBounds(const char* pJsonDocPos, const char*& pElemStart, const char*& pElemEnd)
    {
        // Skip whitespace, commas and colons
        while (*pJsonDocPos && ((*pJsonDocPos <= ' ') || (*pJsonDocPos == ',') || (*pJsonDocPos == ':')))
            pJsonDocPos++;
        if (!*pJsonDocPos)
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
            while (*pJsonDocPos && (numBraces > 0))
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
            while (*pJsonDocPos && ((*pJsonDocPos <= ' ') || (*pJsonDocPos == ',')))
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
            while (*pJsonDocPos && (isEscaped || (*pJsonDocPos != '"')))
            {
                isEscaped = (*pJsonDocPos == '\\');
                pJsonDocPos++;
            }
            if (!*pJsonDocPos)
            {
#ifdef DEBUG_JSON_LOCATE_ELEMENT_BOUNDS
                LOG_I("RaftJson", "locateElementBounds str unexpectedly reached end of document pJsonDocPos %p", pJsonDocPos);
#endif
                return nullptr;
            }
            pElemEnd = pJsonDocPos;
            pJsonDocPos++;
            // Skip whitespace and commas
            while (*pJsonDocPos && ((*pJsonDocPos <= ' ') || (*pJsonDocPos == ',')))
                pJsonDocPos++;
            return pJsonDocPos;
        }
        else
        {
#ifdef DEBUG_JSON_LOCATE_ELEMENT_BOUNDS
            LOG_I("RaftJson", "locateElementBounds found number pJsonDocPos %p", pJsonDocPos);
#endif        
            // Find end of element
            while (*pJsonDocPos && (*pJsonDocPos > ' ') && (*pJsonDocPos != ',') && (*pJsonDocPos != '}') && (*pJsonDocPos != ']'))
                pJsonDocPos++;
            pElemEnd = pJsonDocPos;
            // Skip whitespace and commas
            while (*pJsonDocPos && ((*pJsonDocPos <= ' ') || (*pJsonDocPos == ',')))
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
    /// @param pReqdKey [in/out] the key of the required variable (note this is modified by the function)
    /// @return the position of the element or nullptr if not found
    /// @note The key can be empty in which case the entire object is returned
    /// @note The key can be an array index (e.g. "[0]") in which case the value at that index is returned if the element is an array
    /// @note The key can be a string in which case the value for that key is returned if the element is an object
    static const char* locateElementValueWithKey(const char* pJsonDocPos, const char*& pReqdKey)
    {
        // Check valid
        if (!pJsonDocPos)
            return nullptr;

        // If key is empty return the entire element
        if (!pReqdKey || !*pReqdKey || (*pReqdKey == '/'))
        {
            // Skip any whitespace
            while (*pJsonDocPos && (*pJsonDocPos <= ' '))
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
        while (*pJsonDocPos)
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
                // Check for longer of key and path part
                uint32_t keyLen = (pKeyEnd - pKeyStart) > (pReqdKeyEnd - pReqdKeyStart) ? (pKeyEnd - pKeyStart) : (pReqdKeyEnd - pReqdKeyStart);
                if (strncmp(pKeyStart, pReqdKeyStart, keyLen) == 0)
                    return pJsonDocPos;
                // Skip whitespace
                while (*pJsonDocPos && (*pJsonDocPos <= ' '))
                    pJsonDocPos++;
            }
            else
            {
                if (arrayIdx == elemCount)
                    return pJsonDocPos;
                elemCount++;
                // Skip whitespace
                while (*pJsonDocPos && (*pJsonDocPos <= ' '))
                    pJsonDocPos++;
            }

            // Skip to end of element
            const char* pElemEnd = nullptr;
            const char* pElemStart = nullptr;
            pJsonDocPos = RaftJson::locateElementBounds(pJsonDocPos, pElemStart, pElemEnd);
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
    /// @param pPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @return the position of the element or nullptr if not found
    static const char* locateElementByPath(const char* pJsonDocPos, const char* pPath,
                const RaftJsonIF* pChainedRaftJson)
    {
        // Check valid
        if (!pPath || !pJsonDocPos)
            return nullptr;

        // Iterate over path
        const char* pPathPos = pPath;
#ifdef DEBUG_JSON_BY_SPECIFIC_PATH
        if (strcmp(pPath, DEBUG_JSON_BY_SPECIFIC_PATH) == 0)
        {
            LOG_I(RAFT_JSON_PREFIX, "locateElementByPath path <<<%s>>> jsonDoc %s chainedPtr %p", pPath, pJsonDocPos, pChainedRaftJson);
        }
#endif

#ifdef DEBUG_CHAINED_RAFT_JSON
        const char* pOriginalDoc = pJsonDocPos;
#endif
        while(true)
        {
            // Locate element (note that pPath is modified by each call)
            pJsonDocPos = locateElementValueWithKey(pJsonDocPos, pPathPos);
            if (!pJsonDocPos)
            {
#ifdef DEBUG_CHAINED_RAFT_JSON
                LOG_I(RAFT_JSON_PREFIX, "locateElementByPath path %s not found, chainedPtr %p originalDoc %s", 
                            pOriginalPath, pChainedRaftJson, pOriginalDoc ? pOriginalDoc : "null");
#endif
                // Handle chained documents searching with the original path
                return pChainedRaftJson ? pChainedRaftJson->locateElementByPath(pPath) : nullptr;
            }

            // Check if we're at the end of the path
            if (!*pPathPos)
            {
#ifdef DEBUG_JSON_BY_SPECIFIC_PATH
                if (strcmp(pPath, DEBUG_JSON_BY_SPECIFIC_PATH) == 0)
                {
                    LOG_I(RAFT_JSON_PREFIX, "locateElementByPath path <<<%s>>> returning jsonDoc %s", pPath, pJsonDocPos);
                }
#endif
                return pJsonDocPos;
            }
        }
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

    // Pointer to an alternate RaftJsonIF implementation
    // This allows chaining so that if a value is not found in this object
    // it can be searched for in another object
    // This pointer should not be deleted by this object
    const RaftJsonIF* _pChainedRaftJson = nullptr;

    // Debug
    static constexpr const char *RAFT_JSON_PREFIX = "RaftJson";
};
