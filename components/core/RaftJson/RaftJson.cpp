/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// JSON field extraction
// Many of the methods here support a pDataPath parameter. This uses a syntax like a much simplified XPath:
// [0] returns the 0th element of an array
// / is a separator of nodes
//
// Rob Dobson 2017-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "RaftJson.h"

// #define DEBUG_JSON_BY_SPECIFIC_PATH ""
// #define DEBUG_JSON_BY_SPECIFIC_PATH_PART ""
// #define DEBUG_JSON_LOCATE_ELEMENT_BOUNDS
// #define DEBUG_EXTRACT_NAME_VALUES
// #define DEBUG_CHAINED_RAFT_JSON

#if defined(DEBUG_CHAINED_RAFT_JSON) || \
    defined(DEBUG_EXTRACT_NAME_VALUES) || \
    defined(DEBUG_JSON_BY_SPECIFIC_PATH) || \
    defined(DEBUG_JSON_BY_SPECIFIC_PATH_PART) || \
    defined(DEBUG_JSON_LOCATE_ELEMENT_BOUNDS)
static const char *MODULE_PREFIX = "RaftJson";
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Empty JSON document
const char* RaftJson::EMPTY_JSON_DOCUMENT = "{}";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Treat strings as numbers
bool RaftJson::RAFT_JSON_TREAT_STRINGS_AS_NUMBERS = true;

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Locate an element in a JSON document using a path
/// @param pJsonDocPos the current position in the JSON document
/// @param pPath the path of the required variable in XPath-like syntax (e.g. "a/b/c[0]/d")
/// @param pChainedRaftJson a chained RaftJson object to use if the key is not found in this object
/// @return the position of the element or nullptr if not found
const char* RaftJson::locateElementByPath(const char* pJsonDocPos, const char* pPath,
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
        LOG_I(MODULE_PREFIX, "locateElementByPath path <<<%s>>> jsonDoc %s chainedPtr %p", pPath, pJsonDocPos, pChainedRaftJson);
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
            LOG_I(MODULE_PREFIX, "locateElementByPath path %s not found, chainedPtr %p originalDoc %s", 
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
                LOG_I(MODULE_PREFIX, "locateElementByPath path <<<%s>>> returning jsonDoc %s", pPath, pJsonDocPos);
            }
#endif
            return pJsonDocPos;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Locate an element in a JSON document using a single key (part of a path)
/// @param pReqdKey [in/out] the key of the required variable (note this is modified by the function)
/// @return the position of the element or nullptr if not found
/// @note The key can be empty in which case the entire object is returned
/// @note The key can be an array index (e.g. "[0]") in which case the value at that index is returned if the element is an array
/// @note The key can be a string in which case the value for that key is returned if the element is an object
const char* RaftJson::locateElementValueWithKey(const char* pJsonDocPos, const char*& pReqdKey)
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
        LOG_I(MODULE_PREFIX, "locateElementValueWithKey key <<<%s>>> jsonDoc %s", pReqdKey, pJsonDocPos);
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
        pJsonDocPos = locateElementBounds(pJsonDocPos, pElemStart, pElemEnd);
        if (!pJsonDocPos)
            return nullptr;

        // Check if we've reached the end of the object or array
        if ((*pJsonDocPos == '}') || (*pJsonDocPos == ']'))
            return nullptr;
    }
    return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Locate the bounds of an element in a JSON document
/// @param pJsonDocPos the current position in the JSON document
/// @param pElemStart [out] the start of the element
/// @param pElemEnd [out] the end of the element
/// @return a position in the document after the end of the element or nullptr if not found
const char* RaftJson::locateElementBounds(const char* pJsonDocPos, const char*& pElemStart, const char*& pElemEnd)
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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// @brief Escape a string
// @param strToEsc : string in which to replace characters which are invalid in JSON
void RaftJson::escapeString(String &strToEsc)
{
    // Replace characters which are invalid in JSON
    strToEsc.replace("\\", "\\\\");
    strToEsc.replace("\"", "\\\"");
    strToEsc.replace("\n", "\\n");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Unescape a string
/// @param strToUnEsc : string in which to restore characters which are invalid in JSON
void RaftJson::unescapeString(String &strToUnEsc)
{
    // Replace characters which are invalid in JSON
    strToUnEsc.replace("\\\"", "\"");
    strToUnEsc.replace("\\\\", "\\");
    strToUnEsc.replace("\\n", "\n");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// @brief Convert name:value pairs to a JSON string
// @param nameValuePairs : vector of name:value pairs
// @param includeOuterBraces : true to include outer braces
// @return String : JSON string
String RaftJson::getJSONFromNVPairs(std::vector<NameValuePair>& nameValuePairs, bool includeOuterBraces)
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
// @brief Convert JSON object to HTML query string syntax
// @param jsonStr : JSON string
// @return String : HTML query string
String RaftJson::getHTMLQueryFromJSON(const String& jsonStr)
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
// @brief Extract name-value pairs from string
// @param inStr : input string
// @param pNameValueSep : separator between name and value, e.g. "=" for HTTP
// @param pPairDelim : separator between pairs, e.g. "&" for HTTP
// @param pPairDelimAlt : alternate separator between pairs (pass 0 if not needed), e.g. ";" for HTTP
// @param nameValuePairs : vector of name-value pairs
// @return void
void RaftJson::extractNameValues(const String& inStr, 
        const char* pNameValueSep, const char* pPairDelim, const char* pPairDelimAlt, 
        std::vector<RaftJson::NameValuePair>& nameValuePairs)
{
   // Count the pairs
    uint32_t pairCount = 0;
    const char* pCurSep = inStr.c_str();
    while(pCurSep)
    {
        pCurSep = strstr(pCurSep, pNameValueSep);
        if (pCurSep)
        {
            pairCount++;
            pCurSep++;
        }
    }

#ifdef DEBUG_EXTRACT_NAME_VALUES
    // Debug
    LOG_I(MODULE_PREFIX, "extractNameValues found %d nameValues", pairCount);
#endif

    // Extract the pairs
    nameValuePairs.resize(pairCount);
    pCurSep = inStr.c_str();
    bool sepTypeIsEqualsSign = true;
    uint32_t pairIdx = 0;
    String name, val;
    while(pCurSep)
    {
        // Each pair has the form "name=val;" (semicolon missing on last pair)
        const char* pElemStart = pCurSep;
        if (sepTypeIsEqualsSign)
        {
            // Check for missing =
            pCurSep = strstr(pElemStart, pNameValueSep);
            if (!pCurSep)
                break;
            name = String((uint8_t*)pElemStart, pCurSep-pElemStart);
            pCurSep++;
        }
        else
        {
            // Handle two alternatives - sep or no sep
            pCurSep = strstr(pElemStart, pPairDelim);
            if (!pCurSep && pPairDelimAlt)
                pCurSep = strstr(pElemStart, pPairDelimAlt);
            if (pCurSep)
            {
                val = String((uint8_t*)pElemStart, pCurSep-pElemStart);
                pCurSep++;
            }
            else
            {
                val = pElemStart;
            }
        }

        // Next separator
        sepTypeIsEqualsSign = !sepTypeIsEqualsSign;
        if (!sepTypeIsEqualsSign)
            continue;

        // Store and move on
        if (pairIdx >= pairCount)
            break;
        name.trim();
        val.trim();
        nameValuePairs[pairIdx] = {name,val};
        pairIdx++;
    }

#ifdef DEBUG_EXTRACT_NAME_VALUES
    // Debug
    for (RaftJson::NameValuePair& pair : nameValuePairs)
        LOG_I(MODULE_PREFIX, "extractNameValues name %s val %s", pair.name.c_str(), pair.value.c_str());
#endif
}
