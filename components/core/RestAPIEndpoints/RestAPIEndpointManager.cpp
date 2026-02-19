/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RestAPIEndpointManager
// Endpoints for REST API implementations
//
// Rob Dobson 2012-2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "RaftUtils.h"
#include "RestAPIEndpointManager.h"

// Warn
#define WARN_ON_NON_MATCHING_ENDPOINTS

// Debug
// #define DEBUG_REST_API_ENDPOINTS_ADD
// #define DEBUG_REST_API_ENDPOINTS_GET
// #define DEBUG_HANDLE_API_REQUEST_AND_RESPONSE
// #define DEBUG_NAME_VALUE_PAIR_EXTRACTION

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
RestAPIEndpointManager::RestAPIEndpointManager()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor
RestAPIEndpointManager::~RestAPIEndpointManager()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get number of endpoints
/// @return Number of endpoints
int RestAPIEndpointManager::getNumEndpoints()
{
    return _endpointsList.size();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get nth endpoint
/// @param n Nth index
/// @return Pointer to nth endpoint or nullptr if invalid
RestAPIEndpoint *RestAPIEndpointManager::getNthEndpoint(int n)
{
    // Check valid
    if (n < 0 || n >= (int)_endpointsList.size())
        return nullptr;

    // Get N'th endpoint
    auto it = _endpointsList.begin();
    std::advance(it, n);
    return &(*it);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Add Endpoint to the endpoint manager
/// @param pEndpointStr Endpoint string
/// @param endpointType Endpoint type
/// @param endpointMethod Endpoint method
/// @param callback Main callback function
/// @param pDescription Endpoint description
/// @param pContentType Content type
/// @param pContentEncoding Content encoding
/// @param cacheControl Cache control
/// @param pExtraHeaders Extra headers
/// @param callbackBody Body callback function
/// @param callbackChunk Chunk callback function
/// @param callbackIsReady Is ready callback function
void RestAPIEndpointManager::addEndpoint(const char *pEndpointStr, 
                    RestAPIEndpoint::EndpointType endpointType,
                    RestAPIEndpoint::EndpointMethod endpointMethod,
                    RestAPIFunction callback,
                    const char *pDescription,
                    const char *pContentType,
                    const char *pContentEncoding,
                    RestAPIEndpoint::EndpointCache_t cacheControl,
                    const char *pExtraHeaders,
                    RestAPIFnBody callbackBody,
                    RestAPIFnChunk callbackChunk,
                    RestAPIFnIsReady callbackIsReady
                    )
{
    // Create new command definition and add
    _endpointsList.push_back(RestAPIEndpoint(pEndpointStr, endpointType,
                                endpointMethod, callback,
                                pDescription,
                                pContentType, pContentEncoding,
                                cacheControl, pExtraHeaders,
                                callbackBody, callbackChunk, callbackIsReady));
#ifdef DEBUG_REST_API_ENDPOINTS_ADD
    LOG_I(MODULE_PREFIX, "addEndpoint %s", pEndpointStr);
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get the endpoint definition corresponding to a requested endpoint
/// @param pEndpointStr Endpoint string
/// @return Pointer to endpoint or NULL if not found
RestAPIEndpoint* RestAPIEndpointManager::getEndpoint(const char *pEndpointStr)
{
    // Look for the command in the registered callbacks
    for (RestAPIEndpoint& endpoint : _endpointsList)
    {
        if (strcasecmp(endpoint._endpointStr.c_str(), pEndpointStr) == 0)
        {
            return &endpoint;
        }
    }
    return NULL;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get matching endpoint definition for REST API request
/// @param requestStr Request string
/// @param endpointMethod Endpoint method
/// @param optionsMatchesAll If true, OPTIONS method matches all endpoint methods
/// @return Pointer to matching endpoint or NULL if none matches
RestAPIEndpoint* RestAPIEndpointManager::getMatchingEndpoint(const char *requestStr,
                    RestAPIEndpoint::EndpointMethod endpointMethod, bool optionsMatchesAll)
{
    // Get req endpoint name
    String requestEndpoint = getNthArgStr(requestStr, 0);

#ifdef DEBUG_REST_API_ENDPOINTS_GET
    // Debug
    LOG_I(MODULE_PREFIX, "reqStr %s requestEndpoint %s, num endpoints %zu",
                requestStr, requestEndpoint.c_str(), _endpointsList.size());
#endif

    // Find endpoint
    for (RestAPIEndpoint& endpoint : _endpointsList)
    {
        if (endpoint._endpointType != RestAPIEndpoint::ENDPOINT_CALLBACK)
            continue;
        if ((endpoint._endpointMethod != endpointMethod) && 
                !((endpointMethod == RestAPIEndpoint::EndpointMethod::ENDPOINT_OPTIONS) && optionsMatchesAll))
            continue;
        if (requestEndpoint.equalsIgnoreCase(endpoint._endpointStr))
            return &endpoint;
    }
#ifdef WARN_ON_NON_MATCHING_ENDPOINTS
    LOG_W(MODULE_PREFIX, "getMatchingEndpoint %s method %s not found", 
                requestEndpoint.c_str(), getEndpointMethodStr(endpointMethod));
#endif
    return NULL;    
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle an API request
/// @param requestStr Request string
/// @param retStr Response string
/// @param sourceInfo Source of the request
/// @return RaftRetCode
RaftRetCode RestAPIEndpointManager::handleApiRequest(const char *requestStr, String &retStr, const APISourceInfo& sourceInfo)
{
    // Get matching def
    RestAPIEndpoint* pEndpoint = getMatchingEndpoint(requestStr);

    // Check valid
    if (!pEndpoint)
    {
        Raft::setJsonErrorResult(requestStr, retStr, "failUnknownAPI");
        return RAFT_INVALID_DATA;
    }

    // Call endpoint
    String reqStr(requestStr);
    pEndpoint->callbackMain(reqStr, retStr, sourceInfo);
#ifdef DEBUG_HANDLE_API_REQUEST_AND_RESPONSE
    LOG_W(MODULE_PREFIX, "handleApiRequest %s resp %s channelID %d", requestStr, retStr.c_str(), sourceInfo.channelID);
#endif
    return RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Remove first argument from a REST API argument string
/// @param argStr Argument string
/// @return Argument string with first argument removed
String RestAPIEndpointManager::removeFirstArgStr(const char *argStr)
{
    // Get location of / (excluding first char if needed)
    String oStr = argStr;
    oStr = unencodeHTTPChars(oStr);
    int idxSlash = oStr.indexOf('/', 1);
    if (idxSlash == -1)
        return String("");
    return oStr.substring(idxSlash+1);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get Nth argument from a REST API argument string
/// @param argStr Argument string
/// @param argIdx Argument index
/// @param splitOnQuestionMark If true, split on question mark as well as slash
/// @return Nth argument string
String RestAPIEndpointManager::getNthArgStr(const char *argStr, int argIdx, bool splitOnQuestionMark)
{
    int argLen = 0;
    String oStr;
    const char *pStr = getArgPtrAndLen(argStr, *argStr == '/' ? argIdx + 1 : argIdx, argLen, splitOnQuestionMark);

    if (pStr)
    {
        oStr = String(pStr, argLen);
    }
    oStr = unencodeHTTPChars(oStr);
    return oStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get position and length of nth arg in a REST API argument string
/// @param argStr Argument string
/// @param argIdx Argument index
/// @param argLen Reference to receive argument length
/// @param splitOnQuestionMark If true, split on question mark as well as slash
/// @return Pointer to argument string or NULL if not found
const char* RestAPIEndpointManager::getArgPtrAndLen(const char *argStr, int argIdx, int &argLen, bool splitOnQuestionMark)
{
    int curArgIdx = 0;
    const char *pCh = argStr;
    const char *pArg = argStr;
    bool insideInvCommas = false;

    while (true)
    {
        if (*pCh == '\"')
        {
            insideInvCommas = !insideInvCommas;
        }
        if (((*pCh == '/') && !insideInvCommas) || 
            (splitOnQuestionMark && (*pCh == '?') && !insideInvCommas) || 
            (*pCh == '\0'))
        {
            if (curArgIdx == argIdx)
            {
                argLen = pCh - pArg;
                return pArg;
            }
            if (*pCh == '\0')
            {
                return NULL;
            }
            pArg = pCh + 1;
            curArgIdx++;
        }
        pCh++;
    }
    return NULL;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get the number of arguments in a REST API argument string
/// @param argStr Argument string
/// @return Number of arguments
int RestAPIEndpointManager::getNumArgs(const char *argStr)
{
    int numArgs = 0;
    int numChSinceSep = 0;
    const char *pCh = argStr;
    bool insideInvCommas = false;

    // Count args
    while (*pCh)
    {
        if ((*pCh == '/') && !insideInvCommas)
        {
            numArgs++;
            numChSinceSep = 0;
        }
        else if (*pCh == '?' && !insideInvCommas)
        {
            break;
        }
        else if (*pCh == '\"')
        {
            insideInvCommas = !insideInvCommas;
        }
        pCh++;
        numChSinceSep++;
    }
    if (numChSinceSep > 0)
    {
        return numArgs + 1;
    }
    return numArgs;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief HTTP URL decode
/// @param inStr Input string
/// @return Decoded string
String RestAPIEndpointManager::unencodeHTTPChars(String &inStr)
{
    inStr.replace("%20", " ");
    inStr.replace("%21", "!");
    inStr.replace("%22", "\"");
    inStr.replace("%23", "#");
    inStr.replace("%24", "$");
    inStr.replace("%25", "%");
    inStr.replace("%26", "&");
    inStr.replace("%27", "^");
    inStr.replace("%28", "(");
    inStr.replace("%29", ")");
    inStr.replace("%2A", "*");
    inStr.replace("%2B", "+");
    inStr.replace("%2C", ",");
    inStr.replace("%2D", "-");
    inStr.replace("%2E", ".");
    inStr.replace("%2F", "/");
    inStr.replace("%3A", ":");
    inStr.replace("%3B", ";");
    inStr.replace("%3C", "<");
    inStr.replace("%3D", "=");
    inStr.replace("%3E", ">");
    inStr.replace("%3F", "?");
    inStr.replace("%5B", "[");
    inStr.replace("%5C", "\\");
    inStr.replace("%5D", "]");
    inStr.replace("%5E", "^");
    inStr.replace("%5F", "_");
    inStr.replace("%60", "`");
    inStr.replace("%7B", "{");
    inStr.replace("%7C", "|");
    inStr.replace("%7D", "}");
    inStr.replace("%7E", "~");
    return inStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get endpoint type string
/// @param endpointType Endpoint type
/// @return Type string
const char* RestAPIEndpointManager::getEndpointTypeStr(RestAPIEndpoint::EndpointType endpointType)
{
    if (endpointType == RestAPIEndpoint::ENDPOINT_CALLBACK)
        return "Callback";
    return "Unknown";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get endpoint method string
/// @param endpointMethod Endpoint method
/// @return Method string
const char* RestAPIEndpointManager::getEndpointMethodStr(RestAPIEndpoint::EndpointMethod endpointMethod)
{
    switch(endpointMethod)
    {
        case RestAPIEndpoint::ENDPOINT_POST: return "POST";
        case RestAPIEndpoint::ENDPOINT_PUT: return "PUT";
        case RestAPIEndpoint::ENDPOINT_DELETE: return "DELETE";
        case RestAPIEndpoint::ENDPOINT_OPTIONS: return "OPTIONS";
        default: return "GET";
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get parameters and name/value pairs from REST request
/// @param reqStr REST request string
/// @param params Vector to receive parameters
/// @param nameValuePairs Vector to receive name/value pairs
/// @return true if successful
bool RestAPIEndpointManager::getParamsAndNameValues(const char* reqStr, std::vector<String>& params, 
            std::vector<RaftJson::NameValuePair>& nameValuePairs)
{
    // Single-pass parsing - extract params and name/value pairs in one iteration
    params.clear();
    nameValuePairs.clear();
    
    const char* pCh = reqStr;
    const char* segmentStart = pCh;
    bool insideInvCommas = false;
    
    // Skip leading slash if present
    if (*pCh == '/')
    {
        pCh++;
        segmentStart = pCh;
    }
    
    // Parse path segments until we hit '?' or end
    while (*pCh && *pCh != '?')
    {
        if (*pCh == '\"')
        {
            insideInvCommas = !insideInvCommas;
        }
        else if (*pCh == '/' && !insideInvCommas)
        {
            // Found a segment separator
            if (pCh > segmentStart)
            {
                String segment = String(segmentStart, pCh - segmentStart);
                segment = unencodeHTTPChars(segment);
                params.push_back(segment);
            }
            segmentStart = pCh + 1;
        }
        pCh++;
    }
    
    // Handle last segment before '?' or end
    if (pCh > segmentStart && *segmentStart != '?')
    {
        String segment = String(segmentStart, pCh - segmentStart);
        segment = unencodeHTTPChars(segment);
        params.push_back(segment);
    }
    
    // Parse query parameters if present
    if (*pCh == '?')
    {
        pCh++;  // Skip '?'
        
        const char* nameStart = pCh;
        const char* valueStart = nullptr;
        String name, val;
        
        while (*pCh)
        {
            if (*pCh == '=')
            {
                // Found name=value separator
                name = String(nameStart, pCh - nameStart);
                name.trim();
                name = unencodeHTTPChars(name);
                valueStart = pCh + 1;
            }
            else if ((*pCh == '&' || *pCh == ';') && valueStart)
            {
                // Found value terminator
                val = String(valueStart, pCh - valueStart);
                val = unencodeHTTPChars(val);
                val.trim();
                nameValuePairs.push_back({name, val});
                
                nameStart = pCh + 1;
                valueStart = nullptr;
            }
            pCh++;
        }
        
        // Handle last parameter
        if (valueStart && pCh > valueStart)
        {
            val = String(valueStart, pCh - valueStart);
            val = unencodeHTTPChars(val);
            val.trim();
            nameValuePairs.push_back({name, val});
        }
    }

    // Debug
#ifdef DEBUG_NAME_VALUE_PAIR_EXTRACTION
    LOG_I(MODULE_PREFIX, "getParamsAndNameValues reqStr=%s params.size()=%d", reqStr, params.size());
    for (size_t i = 0; i < params.size(); i++)
        LOG_I(MODULE_PREFIX, "  param[%d]='%s'", i, params[i].c_str());
    for (RaftJson::NameValuePair& pair : nameValuePairs)
        LOG_I(MODULE_PREFIX, "  nameValue %s=%s", pair.name.c_str(), pair.value.c_str());
#endif
    return true;

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get query parameters string from REST request
/// @param reqStr REST request string (e.g., "motors?cmd=motion&mode=abs")
/// @return Query parameters string (e.g., "cmd=motion&mode=abs") or empty string if no query params
String RestAPIEndpointManager::getQueryParamsStr(const char* reqStr)
{
    // Find the '?' character
    const char* pCh = reqStr;
    while (*pCh && *pCh != '?')
    {
        pCh++;
    }
    
    // If no query params found, return empty string
    if (*pCh != '?')
    {
        return String();
    }
    
    // Skip the '?' and return everything after it
    pCh++;
    return String(pCh);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if a string represents a valid JSON number
/// @param str String to check
/// @return true if the string is a valid number format
static bool isValidNumber(const String& str)
{
    if (str.length() == 0)
        return false;
    
    const char* pCh = str.c_str();
    bool hasDot = false;
    bool hasDigit = false;
    
    // First character can be '-' or a digit
    if (*pCh == '-')
    {
        pCh++;
        if (*pCh == '\0')
            return false;  // Just a '-' is not valid
    }
    
    // Check remaining characters
    while (*pCh)
    {
        if (*pCh >= '0' && *pCh <= '9')
        {
            hasDigit = true;
        }
        else if (*pCh == '.')
        {
            if (hasDot)
                return false;  // Multiple dots not allowed
            hasDot = true;
        }
        else
        {
            return false;  // Invalid character
        }
        pCh++;
    }
    
    return hasDigit;  // Must have at least one digit
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get JSON from REST request
/// @param reqStr REST request string
/// @param elements Which elements to include in the result
/// @return RaftJson object
/// @note For PATH_AND_PARAMS, the returned JSON has the form:
/// {
///    "path": [ "segment0", "segment1", ... ],
///    "params": { "name0": "value0", "name1": "value1", ... }
/// }
/// For PATH_ONLY, the returned JSON is an array:
/// [ "segment0", "segment1", ... ]
/// For PARAMS_ONLY, the returned JSON is an object:
/// { "name0": "value0", "name1": "value1", ... }
/// for a URL of the form:
/// /segment0/segment1/.../segmentN?name0=value0&name1=value1&...
RaftJson RestAPIEndpointManager::getJSONFromRESTRequest(const char* reqStr, RESTRequestJSONElements elements)
{
    // Single-pass optimized parsing
    String result;
    result.reserve(strlen(reqStr) * 2 + 100);  // Pre-allocate to avoid reallocations
    
    const char* pCh = reqStr;
    const char* segmentStart = pCh;
    bool insideInvCommas = false;
    
    // Skip leading slash if present
    if (*pCh == '/')
    {
        pCh++;
        segmentStart = pCh;
    }
    
    // Build path segments if needed
    if (elements == PATH_ONLY || elements == PATH_AND_PARAMS)
    {
        bool firstSegment = true;
        
        if (elements == PATH_AND_PARAMS)
            result = "{\"path\":[";
        else
            result = "[";
        
        // Parse path segments until we hit '?' or end
        while (*pCh && *pCh != '?')
        {
            if (*pCh == '\"')
            {
                insideInvCommas = !insideInvCommas;
            }
            else if (*pCh == '/' && !insideInvCommas)
            {
                // Found a segment separator
                if (pCh > segmentStart)
                {
                    if (!firstSegment)
                        result += ",";
                    result += "\"";
                    
                    // Copy and unencode segment if needed
                    String segment = String(segmentStart, pCh - segmentStart);
                    if (segment.indexOf('%') >= 0)
                        segment = unencodeHTTPChars(segment);
                    result += segment;
                    result += "\"";
                    firstSegment = false;
                }
                segmentStart = pCh + 1;
            }
            pCh++;
        }
        
        // Handle last segment before '?' or end
        if (pCh > segmentStart && *segmentStart != '?')
        {
            if (!firstSegment)
                result += ",";
            result += "\"";
            String segment = String(segmentStart, pCh - segmentStart);
            if (segment.indexOf('%') >= 0)
                segment = unencodeHTTPChars(segment);
            result += segment;
            result += "\"";
        }
        
        result += "]";
        
        if (elements == PATH_AND_PARAMS)
            result += ",\"params\":{";
    }
    else
    {
        // PARAMS_ONLY - skip to query parameters
        while (*pCh && *pCh != '?')
            pCh++;
        result = "{";
    }
    
    // Parse parameters if present and needed
    if ((elements == PARAMS_ONLY || elements == PATH_AND_PARAMS) && *pCh == '?')
    {
        pCh++;  // Skip '?'
        
        const char* nameStart = pCh;
        const char* valueStart = nullptr;
        bool firstParam = true;
        
        while (*pCh)
        {
            if (*pCh == '=')
            {
                // Found name=value separator
                if (!firstParam)
                    result += ",";
                
                result += "\"";
                String name = String(nameStart, pCh - nameStart);
                name.trim();
                if (name.indexOf('%') >= 0)
                    name = unencodeHTTPChars(name);
                result += name;
                result += "\":";
                
                valueStart = pCh + 1;
                firstParam = false;
            }
            else if ((*pCh == '&' || *pCh == ';') && valueStart)
            {
                // Found value terminator
                String value = String(valueStart, pCh - valueStart);
                value.trim();
                if (value.indexOf('%') >= 0)
                    value = unencodeHTTPChars(value);
                
                // Smart type detection: check if value is a number, array, or object
                if (value.length() > 0 && (value.charAt(0) == '[' || value.charAt(0) == '{'))
                {
                    // Array or object - add without quotes
                    result += value;
                }
                else if (isValidNumber(value))
                {
                    // Number - add without quotes
                    result += value;
                }
                else
                {
                    // String - add with quotes
                    result += "\"";
                    result += value;
                    result += "\"";
                }
                
                nameStart = pCh + 1;
                valueStart = nullptr;
            }
            pCh++;
        }
        
        // Handle last parameter
        if (valueStart && pCh > valueStart)
        {
            String value = String(valueStart, pCh - valueStart);
            value.trim();
            if (value.indexOf('%') >= 0)
                value = unencodeHTTPChars(value);
            
            // Smart type detection: check if value is a number, array, or object
            if (value.length() > 0 && (value.charAt(0) == '[' || value.charAt(0) == '{'))
            {
                // Array or object - add without quotes
                result += value;
            }
            else if (isValidNumber(value))
            {
                // Number - add without quotes
                result += value;
            }
            else
            {
                // String - add with quotes
                result += "\"";
                result += value;
                result += "\"";
            }
        }
    }
    
    // Close the result based on element type
    if (elements == PATH_ONLY)
    {
        // Already closed with "]"
    }
    else if (elements == PARAMS_ONLY)
    {
        result += "}";
    }
    else // PATH_AND_PARAMS
    {
        result += "}}";
    }
    
    return RaftJson(result.c_str());
}
