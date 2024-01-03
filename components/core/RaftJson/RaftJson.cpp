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
#include "RaftUtils.h"

static const char *MODULE_PREFIX = "RaftJson";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// @brief Escape a string
// @param strToEsc : string in which to replace characters which are invalid in JSON
void RaftJson_jsmn::escapeString(String &strToEsc)
{
    // Replace characters which are invalid in JSON
    strToEsc.replace("\\", "\\\\");
    strToEsc.replace("\"", "\\\"");
    strToEsc.replace("\n", "\\n");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Unescape a string
/// @param strToUnEsc : string in which to restore characters which are invalid in JSON
void RaftJson_jsmn::unescapeString(String &strToUnEsc)
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
String RaftJson_jsmn::getJSONFromNVPairs(std::vector<NameValuePair>& nameValuePairs, bool includeOuterBraces)
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
String RaftJson_jsmn::getHTMLQueryFromJSON(const String& jsonStr)
{
    // Get keys of object
    std::vector<String> keyStrs;
    RaftJson_jsmn::getKeys(jsonStr.c_str(), "", keyStrs);
    if (keyStrs.size() == 0)
        return "";

    // Fill object
    String outStr;
    for (String& keyStr : keyStrs)
    {
        String valStr = getString(jsonStr.c_str(), keyStr.c_str(), "");
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
void RaftJson_jsmn::extractNameValues(const String& inStr, 
        const char* pNameValueSep, const char* pPairDelim, const char* pPairDelimAlt, 
        std::vector<RaftJson_jsmn::NameValuePair>& nameValuePairs)
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
    for (RaftJson_jsmn::NameValuePair& pair : nameValuePairs)
        LOG_I(MODULE_PREFIX, "extractNameValues name %s val %s", pair.name.c_str(), pair.value.c_str());
#endif
}
