/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RestAPIEndpointManager
// Endpoints for REST API implementations
//
// Rob Dobson 2012-2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <list>
#include "RestAPIEndpoint.h"
#include "RaftJson.h"

// Collection of endpoints
class RestAPIEndpointManager
{
public:
    /// @brief Constructor
    RestAPIEndpointManager();

    /// @brief Destructor
    virtual ~RestAPIEndpointManager();

    /// @brief Get number of endpoints
    /// @return Number of endpoints
    int getNumEndpoints();

    /// @brief Get nth endpoint
    /// @param n Nth index
    /// @return Pointer to nth endpoint or nullptr if invalid
    RestAPIEndpoint *getNthEndpoint(int n);

    /// @brief Add Endpoint to the endpoint manager
    /// @param pEndpointStr Endpoint string
    /// @param endpointType Endpoint type
    /// @param endpointMethod Endpoint method
    /// @param callbackMain Main callback function
    /// @param pDescription Endpoint description
    /// @param pContentType Content type
    /// @param pContentEncoding Content encoding
    /// @param pCache Cache control
    /// @param pExtraHeaders Extra headers
    /// @param callbackBody Body callback function
    /// @param callbackChunk Chunk callback function
    /// @param callbackIsReady Is ready callback function
    void addEndpoint(const char *pEndpointStr, 
                    RestAPIEndpoint::EndpointType endpointType,
                    RestAPIEndpoint::EndpointMethod endpointMethod,
                    RestAPIFunction callbackMain,
                    const char *pDescription,
                    const char *pContentType = nullptr,
                    const char *pContentEncoding = nullptr,
                    RestAPIEndpoint::EndpointCache_t pCache = RestAPIEndpoint::ENDPOINT_CACHE_NEVER,
                    const char *pExtraHeaders = nullptr,
                    RestAPIFnBody callbackBody = nullptr,
                    RestAPIFnChunk callbackChunk = nullptr,
                    RestAPIFnIsReady callbackIsReady = nullptr);

    /// @brief Get the endpoint definition corresponding to a requested endpoint
    /// @param pEndpointStr Endpoint string
    /// @return Pointer to endpoint or NULL if not found
    RestAPIEndpoint *getEndpoint(const char *pEndpointStr);

    /// @brief Handle an API request
    /// @param requestStr Request string
    /// @param retStr Response string
    /// @param sourceInfo Source of the request
    /// @return RaftRetCode
    RaftRetCode handleApiRequest(const char *requestStr, String &retStr, const APISourceInfo& sourceInfo);

    /// @brief Get matching endpoint definition for REST API request
    /// @param requestStr Request string
    /// @param endpointMethod Endpoint method
    /// @param optionsMatchesAll If true, OPTIONS method matches all endpoint methods
    /// @return Pointer to matching endpoint or NULL if none matches
    RestAPIEndpoint* getMatchingEndpoint(const char *requestStr,
                    RestAPIEndpoint::EndpointMethod endpointMethod = RestAPIEndpoint::ENDPOINT_GET,
                    bool optionsMatchesAll = false);

    /// @brief Remove first argument from string
    /// @param argStr Argument string
    /// @return String with first argument removed
    static String removeFirstArgStr(const char *argStr);

    /// @brief Get nth argument from argument string
    /// @param argStr Argument string
    /// @param argIdx Nth argument index
    /// @param splitOnQuestionMark When true, split on '?' character
    /// @return Nth argument string
    static String getNthArgStr(const char *argStr, int argIdx, bool splitOnQuestionMark = true);

    /// @brief Get pointer and length of nth argument from argument string
    /// @param argStr Argument string
    /// @param argIdx Nth argument index
    /// @param argLen Reference to receive argument length
    /// @param splitOnQuestionMark When true, split on '?' character
    /// @return Pointer to start of nth argument string
    static const char *getArgPtrAndLen(const char *argStr, int argIdx, int &argLen, bool splitOnQuestionMark = true);

    /// @brief Get number of arguments in argument string
    /// @param argStr Argument string
    /// @return Number of arguments
    static int getNumArgs(const char *argStr);

    /// @brief Unencode HTTP characters in string
    /// @param inStr Input string
    /// @return Unencoded string
    static String unencodeHTTPChars(String &inStr);

    /// @brief Get endpoint type string
    /// @param endpointType Endpoint type
    /// @return Type string
    static const char *getEndpointTypeStr(RestAPIEndpoint::EndpointType endpointType);

    /// @brief Get endpoint method string
    /// @param endpointMethod Endpoint method
    /// @return Method string
    static const char *getEndpointMethodStr(RestAPIEndpoint::EndpointMethod endpointMethod);

    /// @brief Elements of REST request to get JSON from
    enum RESTRequestJSONElements
    {
        PATH_AND_PARAMS,
        PATH_ONLY,
        PARAMS_ONLY
    };

    /// @brief Get JSON from REST request
    /// @param reqStr REST request string
    /// @return RaftJson object
    static RaftJson getJSONFromRESTRequest(const char* reqStr, RESTRequestJSONElements elements = PATH_AND_PARAMS);

    /// @brief Get parameters and name/value pairs from REST request
    /// @param reqStr REST request string
    /// @param params Vector to receive parameters
    /// @param nameValuePairs Vector to receive name/value pairs
    /// @return true if successful
    static bool getParamsAndNameValues(const char* reqStr, std::vector<String>& params, std::vector<RaftJson::NameValuePair>& nameValuePairs);

    /// @brief Get query parameters string from REST request
    /// @param reqStr REST request string (e.g., "motors?cmd=motion&mode=abs")
    /// @return Query parameters string (e.g., "cmd=motion&mode=abs") or empty string if no query params
    static String getQueryParamsStr(const char* reqStr);

    /// @brief Channel IDs for various REST API sources
    static const uint32_t CHANNEL_ID_EVENT_DETECTOR = 20000;
    static const uint32_t CHANNEL_ID_ROBOT_CONTROLLER = 20001;
    static const uint32_t CHANNEL_ID_COMMAND_FILE = 20002;
    static const uint32_t CHANNEL_ID_SERIAL_CONSOLE = 20003;
    static const uint32_t CHANNEL_ID_COMMAND_SCHEDULER = 20004;
    static const uint32_t CHANNEL_ID_MQTT_COMMS = 20005;
    static const uint32_t CHANNEL_ID_REMOTE_CONTROL = 20006;

private:
    /// @brief List of endpoints
    std::list<RestAPIEndpoint> _endpointsList;

    // Debug
    static constexpr const char* MODULE_PREFIX = "RestAPI";
};
