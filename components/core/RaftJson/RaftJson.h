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
        RAFT_JSON_UNDEFINED = 0,
        RAFT_JSON_OBJECT = 1,
        RAFT_JSON_ARRAY = 2,
        RAFT_JSON_STRING = 3,
        RAFT_JSON_BOOLEAN = 4,
        RAFT_JSON_NUMBER = 5,
        RAFT_JSON_NULL = 6
    } RaftJsonType;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Construct a new RaftJson object
    /// @param pJsonStr 
    /// @param makeCopy when false the string pointer must remain valid for the lifetime of this object
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @note The makeCopy option is provided to avoid copying strings in flash memory - please don't use it in other cases
    RaftJson(const char* pJsonStr, bool makeCopy = true, RaftJsonIF* pChainedRaftJson = nullptr)
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
        _pChainedRaftJson = pChainedRaftJson;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Construct a new RaftJson object from Arduino String
    /// @param pJsonStr
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @note makes a copy
    RaftJson(const String& jsonStr, RaftJsonIF* pChainedRaftJson = nullptr)
    {
#ifdef ESP_PLATFORM        
        _jsonStr = std::vector<char, SpiramAwareAllocator<char>>(jsonStr.c_str(), jsonStr.c_str() + jsonStr.length() + 1);
#else
        _jsonStr = std::vector<char>(jsonStr.c_str(), jsonStr.c_str() + jsonStr.length() + 1);
#endif
        _pSourceStr = _jsonStr.data();
        _pChainedRaftJson = pChainedRaftJson;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Construct a new RaftJson object from std::string
    /// @param pJsonStr
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @note makes a copy
    RaftJson(const std::string& jsonStr, RaftJsonIF* pChainedRaftJson = nullptr)
    {
#ifdef ESP_PLATFORM
        _jsonStr = std::vector<char, SpiramAwareAllocator<char>>(jsonStr.c_str(), jsonStr.c_str() + jsonStr.length() + 1);
#else
        _jsonStr = std::vector<char>(jsonStr.c_str(), jsonStr.c_str() + jsonStr.length() + 1);
#endif
        _pSourceStr = _jsonStr.data();
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

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get string value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual String getString(const char* pDataPath, const char* defaultValue) const override
    {
        return getString(_pSourceStr, pDataPath, defaultValue, _pChainedRaftJson);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get double value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual double getDouble(const char* pDataPath, double defaultValue) const override
    {
        return getDouble(_pSourceStr, pDataPath, defaultValue, _pChainedRaftJson);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get long value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual long getLong(const char* pDataPath, long defaultValue) const override
    {
        return getLong(_pSourceStr, pDataPath, defaultValue);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get boolean value using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @return the value of the variable or the default value if not found
    virtual bool getBool(const char* pDataPath, bool defaultValue) const override
    {
        return getBool(_pSourceStr, pDataPath, defaultValue);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get array elements using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param strList a vector which is filled with the array elements
    /// @return true if the array was found
    virtual bool getArrayElems(const char *pDataPath, std::vector<String>& strList) const override
    {
        return getArrayElems(_pSourceStr, pDataPath, strList);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get keys of an object using the member variable JSON document
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param keysVector a vector which is filled with the keys
    /// @return true if the object was found
    virtual bool getKeys(const char *pDataPath, std::vector<String>& keysVector) const override
    {
        return getKeys(_pSourceStr, pDataPath, keysVector);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Check if the member JSON document contains the key specified by the path
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @return true if the key was found
    virtual bool contains(const char* pDataPath) const override
    {
        int arrayLen = 0;
        RaftJsonType elemType = getType(_pSourceStr, pDataPath, arrayLen);
        return elemType != RAFT_JSON_UNDEFINED;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Static methods
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief gets a string from a JSON document
    /// @param pJsonDoc the JSON document (string)
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @return the value of the variable or the default value if not found
    static String getString(const char* pJsonDoc,
            const char* pDataPath, const char* defaultValue,
            RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Locate the element
        const char* pJsonDocPos = pJsonDoc;
        pJsonDocPos = locateElementByPath(pJsonDocPos, pDataPath, pChainedRaftJson);
        // Check if we found the element
        if (!pJsonDocPos)
            return defaultValue;
        const char* pElemStart = nullptr;
        const char* pElemEnd = nullptr;
        if (!locateElementBounds(pJsonDocPos, pElemStart, pElemEnd))
            return defaultValue;
        // Skip quotes
        if (*pElemStart == '"')
            pElemStart++;
        return String(pElemStart, pElemEnd - pElemStart);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief gets a double from a JSON document
    /// @param pJsonDoc the JSON document (string)
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @return the value of the variable or the default value if not found
    static double getDouble(const char* pJsonDoc,
            const char* pDataPath, double defaultValue,
            RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Locate the element
        const char* pJsonDocPos = pJsonDoc;
        pJsonDocPos = locateElementByPath(pJsonDocPos, pDataPath, pChainedRaftJson);
        // Check if we found the element
        if (!pJsonDocPos)
            return defaultValue;
        // Check if it's a boolean
        int retValue = 0;
        if (RaftJson::isBoolean(pJsonDocPos, retValue))
            return retValue;
        // Check for a string value - if so skip quotes
        if (*pJsonDocPos == '"')
            pJsonDocPos++;
        // Convert to double
        return strtod(pJsonDocPos, NULL);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief gets a long from a JSON document
    /// @param pJsonDoc the JSON document (string)
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @return the value of the variable or the default value if not found
    static long getLong(const char* pJsonDoc,
            const char* pDataPath, long defaultValue,
            RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Locate the element
        const char* pJsonDocPos = pJsonDoc;
        pJsonDocPos = locateElementByPath(pJsonDocPos, pDataPath, pChainedRaftJson);
        // Check if we found the element
        if (!pJsonDocPos)
            return defaultValue;
        // Check if it's a boolean
        int retValue = 0;
        if (RaftJson::isBoolean(pJsonDocPos, retValue))
            return retValue;
        // Check for a string value - if so skip quotes
        if (*pJsonDocPos == '"')
            pJsonDocPos++;
        // Convert to long
        return strtol(pJsonDocPos, NULL, 0);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief gets a boolean from a JSON document
    /// @param pJsonDoc the JSON document (string)
    /// @param pDataPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param defaultValue the default value to return if the variable is not found
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @return the value of the variable or the default value if not found
    static bool getBool(const char* pJsonDoc, 
            const char* pDataPath, bool defaultValue,
            RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Use long method to get value
        return RaftJson::getLong(pJsonDoc, pDataPath, defaultValue, pChainedRaftJson) != 0;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief gets the elements of an array from the JSON document
    /// @param pJsonDoc the JSON document (string)
    /// @param pDataPath the path of the required array in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param strList a vector which is filled with the array elements
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @return true if the array was found
    static bool getArrayElems(const char* pJsonDoc,
            const char *pDataPath, std::vector<String>& strList,
            RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Locate the element
        const char* pJsonDocPos = pJsonDoc;
        pJsonDocPos = locateElementByPath(pJsonDocPos, pDataPath, pChainedRaftJson);
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
            pJsonDocPos = locateElementBounds(pJsonDocPos, pElemStart, pElemEnd);
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
    /// @brief gets the keys of an object from the JSON document
    /// @param pJsonDoc the JSON document (string)
    /// @param pDataPath the path of the required object in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param keysVector a vector which is filled with the keys
    /// @param pChainRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @return true if the object was found
    static bool getKeys(const char* pJsonDoc,
            const char *pDataPath, std::vector<String>& keysVector,
            RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Locate the element
        const char* pJsonDocPos = pJsonDoc;
        pJsonDocPos = locateElementByPath(pJsonDocPos, pDataPath, pChainedRaftJson);
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
            pJsonDocPos = locateElementBounds(pJsonDocPos, pElemStart, pElemEnd);
            if (!pJsonDocPos)
                return false;
        }
        return true;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Check if element at the current position in a JSON document is a boolean value
    /// @param pJsonDocPos the current position in the JSON document
    /// @param retValue the value of the boolean
    /// @return true if the element is a boolean
    static bool isBoolean(const char* pJsonDocPos, int &retValue)
    {
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
    /// @brief Get type of element from a JSON document at the specified path
    /// @param pJsonDoc the JSON document (string)
    /// @param pDataPath the path of the required object in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @param arrayLen the length of the array if the element is an array
    /// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
    /// @return the type of the element
    static RaftJsonType getType(const char* pJsonDoc,
            const char* pDataPath, int &arrayLen,
            RaftJsonIF* pChainedRaftJson = nullptr)
    {
        // Locate the element
        const char* pJsonDocPos = pJsonDoc;
        pJsonDocPos = locateElementByPath(pJsonDocPos, pDataPath, pChainedRaftJson);
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
                pJsonDocPos = locateElementBounds(pJsonDocPos, pElemStart, pElemEnd);
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
        if (RaftJson::isBoolean(pJsonDocPos, retValue))
            return RAFT_JSON_BOOLEAN;
        // Check if it's a number
        if ((*pJsonDocPos >= '0') && (*pJsonDocPos <= '9'))
            return RAFT_JSON_NUMBER;
        // Check if it's null
        if (strncmp(pJsonDocPos, "null", 4) == 0)
            return RAFT_JSON_NULL;
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
    static String getJSONFromNVPairs(std::vector<NameValuePair>& nameValuePairs, bool includeOuterBraces);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Escape a string
    /// @param strToEsc : string in which to replace characters which are invalid in JSON
    static void escapeString(String& strToEsc);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Unescape a string
    /// @param strToUnEsc : string in which to restore characters which are invalid in JSON
    static void unescapeString(String& strToUnEsc);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Convert JSON object to HTML query string syntax
    /// @param jsonStr : JSON string
    /// @return String : HTML query string
    static String getHTMLQueryFromJSON(const String& jsonStr);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Extract name-value pairs from string
    /// @param inStr : input string
    /// @param pNameValueSep : separator between name and value, e.g. "=" for HTTP
    /// @param pPairDelim : separator between pairs, e.g. "&" for HTTP
    /// @param pPairDelimAlt : alternate separator between pairs (pass 0 if not needed), e.g. ";" for HTTP
    /// @param nameValuePairs : vector of name-value pairs
    /// @return void
    static void extractNameValues(const String& inStr, 
        const char* pNameValueSep, const char* pPairDelim, const char* pPairDelimAlt, 
        std::vector<RaftJson::NameValuePair>& nameValuePairs);

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

protected:
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Locate an element in a JSON document using a path
    /// @param pPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
    /// @return the position of the element or nullptr if not found
    virtual const char* locateElementByPath(const char* pPath) override
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
            // Find end of object
            char braceChar = *pJsonDocPos;
            int numBraces = 1;
            pJsonDocPos++;
            // Skip to end of object
            bool insideString = false;
            while (*pJsonDocPos && (numBraces > 0))
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
            // Skip whitespace and commas
            while (*pJsonDocPos && ((*pJsonDocPos <= ' ') || (*pJsonDocPos == ',')))
                pJsonDocPos++;
            return pJsonDocPos;
        }
        else if (*pJsonDocPos == '"')
        {
            // Find end of string
            pJsonDocPos++;
            while (*pJsonDocPos && (*pJsonDocPos != '"'))
                pJsonDocPos++;
            if (!*pJsonDocPos)
                return nullptr;
            pElemEnd = pJsonDocPos;
            pJsonDocPos++;
            // Skip whitespace and commas
            while (*pJsonDocPos && ((*pJsonDocPos <= ' ') || (*pJsonDocPos == ',')))
                pJsonDocPos++;
            return pJsonDocPos;
        }
        else
        {
            // Find end of element
            while (*pJsonDocPos && (*pJsonDocPos > ' ') && (*pJsonDocPos != ',') && (*pJsonDocPos != '}') && (*pJsonDocPos != ']'))
                pJsonDocPos++;
            pElemEnd = pJsonDocPos;
            // Skip whitespace and commas
            while (*pJsonDocPos && ((*pJsonDocPos <= ' ') || (*pJsonDocPos == ',')))
                pJsonDocPos++;
            return pJsonDocPos;
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Locate an element in a JSON document using a single key (part of a path)
    /// @param pReqdKey the key of the required variable
    /// @return the position of the element or nullptr if not found
    /// @note The key can be empty in which case the entire object is returned
    /// @note The key can be an array index (e.g. "[0]") in which case the value at that index is returned if the element is an array
    /// @note The key can be a string in which case the value for that key is returned if the element is an object
    static const char* locateElementValueWithKey(const char* pJsonDocPos,
                const char*& pReqdKey)
    {
        // If key is empty return the entire element
        if (!pReqdKey || !*pReqdKey || (*pReqdKey == '/'))
        {
            // Skip any whitespace
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
                // Check for end of object
                if (*pJsonDocPos == '}')
                    return nullptr;
            }
            else
            {
                if (arrayIdx == elemCount)
                    return pJsonDocPos;
                elemCount++;
                // Skip whitespace
                while (*pJsonDocPos && (*pJsonDocPos <= ' '))
                    pJsonDocPos++;
                // Check for end of array
                if (*pJsonDocPos == ']')
                    return nullptr;
            }
            // Skip to end of element
            const char* pElemEnd = nullptr;
            const char* pElemStart = nullptr;
            pJsonDocPos = locateElementBounds(pJsonDocPos, pElemStart, pElemEnd);
            if (!pJsonDocPos)
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
                RaftJsonIF* pChainedRaftJson)
    {
        // Iterate over path
        while(true)
        {
            // Locate element
            pJsonDocPos = locateElementValueWithKey(pJsonDocPos, pPath);
            if (!pJsonDocPos)
                return pChainedRaftJson ? pChainedRaftJson->locateElementByPath(pPath) : nullptr;

            // Check if we're at the end of the path
            if (!*pPath)
                return pJsonDocPos;
        }
    }

private:
    // JSON document string
#ifdef ESP_PLATFORM
    std::vector<char, SpiramAwareAllocator<char>> _jsonStr;
#else
    std::vector<char> _jsonStr;
#endif

    // Copy of pointer to JSON string
    // Note this pointer should NEVER be deleted by this object
    // Either it is a pointer to a string in flash memory
    // Or it is a pointer to the string in the _jsonStr vector
    const char* _pSourceStr = nullptr;

    // Pointer to an alternate RaftJsonIF implementation
    // This allows chaining so that if a value is not found in this object
    // it can be searched for in another object
    // This pointer should not be deleted by this object
    RaftJsonIF* _pChainedRaftJson = nullptr;
};
