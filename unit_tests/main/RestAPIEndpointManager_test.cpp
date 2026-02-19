/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Unit test of RestAPIEndpointManager
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "unity.h"
#include "RestAPIEndpointManager.h"
#include "RaftJson.h"
#include "Logger.h"
#include <vector>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test getJSONFromRESTRequest with simple path segments
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("getJSONFromRESTRequest - simple path", "[restapi]")
{
    RaftJson json = RestAPIEndpointManager::getJSONFromRESTRequest("/api/test/endpoint");
    
    // Check path segments
    std::vector<String> segments;
    TEST_ASSERT_TRUE(json.getArrayElems("path", segments));
    TEST_ASSERT_EQUAL(3, segments.size());
    TEST_ASSERT_EQUAL_STRING("api", segments[0].c_str());
    TEST_ASSERT_EQUAL_STRING("test", segments[1].c_str());
    TEST_ASSERT_EQUAL_STRING("endpoint", segments[2].c_str());
    
    // Check params object - should have no keys
    std::vector<String> paramKeys;
    json.getKeys("params", paramKeys);
    TEST_ASSERT_EQUAL(0, paramKeys.size());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test getJSONFromRESTRequest with query parameters
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("getJSONFromRESTRequest - with query parameters", "[restapi]")
{
    RaftJson json = RestAPIEndpointManager::getJSONFromRESTRequest("/api/device?id=123&name=sensor&active=true");
    
    // Check path segments
    std::vector<String> segments;
    TEST_ASSERT_TRUE(json.getArrayElems("path", segments));
    TEST_ASSERT_EQUAL(2, segments.size());
    TEST_ASSERT_EQUAL_STRING("api", segments[0].c_str());
    TEST_ASSERT_EQUAL_STRING("device", segments[1].c_str());
    
    // Check params
    String id = json.getString("params/id", "");
    String name = json.getString("params/name", "");
    String active = json.getString("params/active", "");
    
    TEST_ASSERT_EQUAL_STRING("123", id.c_str());
    TEST_ASSERT_EQUAL_STRING("sensor", name.c_str());
    TEST_ASSERT_EQUAL_STRING("true", active.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test getJSONFromRESTRequest with URL encoded characters
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("getJSONFromRESTRequest - URL encoded", "[restapi]")
{
    RaftJson json = RestAPIEndpointManager::getJSONFromRESTRequest("/api/search?query=hello%20world&filter=test%2Bdata");
    
    // Check path segments
    std::vector<String> segments;
    TEST_ASSERT_TRUE(json.getArrayElems("path", segments));
    TEST_ASSERT_EQUAL(2, segments.size());
    TEST_ASSERT_EQUAL_STRING("api", segments[0].c_str());
    TEST_ASSERT_EQUAL_STRING("search", segments[1].c_str());
    
    // Check params with decoded values
    String query = json.getString("params/query", "");
    String filter = json.getString("params/filter", "");
    
    TEST_ASSERT_EQUAL_STRING("hello world", query.c_str());
    TEST_ASSERT_EQUAL_STRING("test+data", filter.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test getJSONFromRESTRequest with multiple parameters
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("getJSONFromRESTRequest - multiple parameters", "[restapi]")
{
    RaftJson json = RestAPIEndpointManager::getJSONFromRESTRequest(
        "/config/set?delayMs=10&skipCount=5&enabled=1&mode=test");
    
    // Check path segments
    std::vector<String> segments;
    TEST_ASSERT_TRUE(json.getArrayElems("path", segments));
    TEST_ASSERT_EQUAL(2, segments.size());
    TEST_ASSERT_EQUAL_STRING("config", segments[0].c_str());
    TEST_ASSERT_EQUAL_STRING("set", segments[1].c_str());
    
    // Check params
    String delayMs = json.getString("params/delayMs", "");
    String skipCount = json.getString("params/skipCount", "");
    String enabled = json.getString("params/enabled", "");
    String mode = json.getString("params/mode", "");
    
    TEST_ASSERT_EQUAL_STRING("10", delayMs.c_str());
    TEST_ASSERT_EQUAL_STRING("5", skipCount.c_str());
    TEST_ASSERT_EQUAL_STRING("1", enabled.c_str());
    TEST_ASSERT_EQUAL_STRING("test", mode.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test getJSONFromRESTRequest with empty path
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("getJSONFromRESTRequest - empty path", "[restapi]")
{
    RaftJson json = RestAPIEndpointManager::getJSONFromRESTRequest("?param1=value1");
    
    // Check path segments
    std::vector<String> segments;
    json.getArrayElems("path", segments);
    TEST_ASSERT_TRUE(segments.size() == 0);
    
    // Check params
    String param1 = json.getString("params/param1", "");
    TEST_ASSERT_EQUAL_STRING("value1", param1.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test getJSONFromRESTRequest with semicolon separator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("getJSONFromRESTRequest - semicolon separator", "[restapi]")
{
    RaftJson json = RestAPIEndpointManager::getJSONFromRESTRequest("/api/test?param1=val1;param2=val2");
    
    // Check params
    String param1 = json.getString("params/param1", "");
    String param2 = json.getString("params/param2", "");
    
    TEST_ASSERT_EQUAL_STRING("val1", param1.c_str());
    TEST_ASSERT_EQUAL_STRING("val2", param2.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test getParamsAndNameValues with simple path
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("getParamsAndNameValues - simple path", "[restapi]")
{
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    
    bool result = RestAPIEndpointManager::getParamsAndNameValues("/api/device/status", params, nameValues);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(3, params.size());
    TEST_ASSERT_EQUAL_STRING("api", params[0].c_str());
    TEST_ASSERT_EQUAL_STRING("device", params[1].c_str());
    TEST_ASSERT_EQUAL_STRING("status", params[2].c_str());
    TEST_ASSERT_EQUAL(0, nameValues.size());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test getParamsAndNameValues with query parameters
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("getParamsAndNameValues - with query parameters", "[restapi]")
{
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    
    bool result = RestAPIEndpointManager::getParamsAndNameValues(
        "/config/set?delayMs=10&skipCount=5", params, nameValues);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(2, params.size());
    TEST_ASSERT_EQUAL_STRING("config", params[0].c_str());
    TEST_ASSERT_EQUAL_STRING("set", params[1].c_str());
    
    TEST_ASSERT_EQUAL(2, nameValues.size());
    TEST_ASSERT_EQUAL_STRING("delayMs", nameValues[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("10", nameValues[0].value.c_str());
    TEST_ASSERT_EQUAL_STRING("skipCount", nameValues[1].name.c_str());
    TEST_ASSERT_EQUAL_STRING("5", nameValues[1].value.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test getParamsAndNameValues with URL encoded values
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("getParamsAndNameValues - URL encoded", "[restapi]")
{
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    
    bool result = RestAPIEndpointManager::getParamsAndNameValues(
        "/search?query=hello%20world&tag=c%2B%2B", params, nameValues);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(1, params.size());
    TEST_ASSERT_EQUAL_STRING("search", params[0].c_str());
    
    TEST_ASSERT_EQUAL(2, nameValues.size());
    TEST_ASSERT_EQUAL_STRING("query", nameValues[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("hello world", nameValues[0].value.c_str());
    TEST_ASSERT_EQUAL_STRING("tag", nameValues[1].name.c_str());
    TEST_ASSERT_EQUAL_STRING("c++", nameValues[1].value.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test getParamsAndNameValues with empty values
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("getParamsAndNameValues - empty values", "[restapi]")
{
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    
    bool result = RestAPIEndpointManager::getParamsAndNameValues(
        "/test?flag1=&flag2=&param=value", params, nameValues);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(1, params.size());
    TEST_ASSERT_EQUAL_STRING("test", params[0].c_str());
    
    TEST_ASSERT_EQUAL(3, nameValues.size());
    TEST_ASSERT_EQUAL_STRING("flag1", nameValues[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("", nameValues[0].value.c_str());
    TEST_ASSERT_EQUAL_STRING("flag2", nameValues[1].name.c_str());
    TEST_ASSERT_EQUAL_STRING("", nameValues[1].value.c_str());
    TEST_ASSERT_EQUAL_STRING("param", nameValues[2].name.c_str());
    TEST_ASSERT_EQUAL_STRING("value", nameValues[2].value.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test getParamsAndNameValues with semicolon separator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("getParamsAndNameValues - semicolon separator", "[restapi]")
{
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    
    bool result = RestAPIEndpointManager::getParamsAndNameValues(
        "/api/test?param1=val1;param2=val2;param3=val3", params, nameValues);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(2, params.size());
    TEST_ASSERT_EQUAL_STRING("api", params[0].c_str());
    TEST_ASSERT_EQUAL_STRING("test", params[1].c_str());
    
    TEST_ASSERT_EQUAL(3, nameValues.size());
    TEST_ASSERT_EQUAL_STRING("param1", nameValues[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("val1", nameValues[0].value.c_str());
    TEST_ASSERT_EQUAL_STRING("param2", nameValues[1].name.c_str());
    TEST_ASSERT_EQUAL_STRING("val2", nameValues[1].value.c_str());
    TEST_ASSERT_EQUAL_STRING("param3", nameValues[2].name.c_str());
    TEST_ASSERT_EQUAL_STRING("val3", nameValues[2].value.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test getParamsAndNameValues with complex URL
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("getParamsAndNameValues - complex URL", "[restapi]")
{
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    
    bool result = RestAPIEndpointManager::getParamsAndNameValues(
        "/devman/cmdraw?bus=I2C0&addr=0x20&hexWr=0123&numToRd=4&msgKey=test123", 
        params, nameValues);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(2, params.size());
    TEST_ASSERT_EQUAL_STRING("devman", params[0].c_str());
    TEST_ASSERT_EQUAL_STRING("cmdraw", params[1].c_str());
    
    TEST_ASSERT_EQUAL(5, nameValues.size());
    TEST_ASSERT_EQUAL_STRING("bus", nameValues[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("I2C0", nameValues[0].value.c_str());
    TEST_ASSERT_EQUAL_STRING("addr", nameValues[1].name.c_str());
    TEST_ASSERT_EQUAL_STRING("0x20", nameValues[1].value.c_str());
    TEST_ASSERT_EQUAL_STRING("hexWr", nameValues[2].name.c_str());
    TEST_ASSERT_EQUAL_STRING("0123", nameValues[2].value.c_str());
    TEST_ASSERT_EQUAL_STRING("numToRd", nameValues[3].name.c_str());
    TEST_ASSERT_EQUAL_STRING("4", nameValues[3].value.c_str());
    TEST_ASSERT_EQUAL_STRING("msgKey", nameValues[4].name.c_str());
    TEST_ASSERT_EQUAL_STRING("test123", nameValues[4].value.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test getParamsAndNameValues without leading slash
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("getParamsAndNameValues - no leading slash", "[restapi]")
{
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    
    bool result = RestAPIEndpointManager::getParamsAndNameValues(
        "api/test?param=value", params, nameValues);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(2, params.size());
    TEST_ASSERT_EQUAL_STRING("api", params[0].c_str());
    TEST_ASSERT_EQUAL_STRING("test", params[1].c_str());
    
    TEST_ASSERT_EQUAL(1, nameValues.size());
    TEST_ASSERT_EQUAL_STRING("param", nameValues[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("value", nameValues[0].value.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test getParamsAndNameValues with whitespace in values
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("getParamsAndNameValues - whitespace trimming", "[restapi]")
{
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    
    bool result = RestAPIEndpointManager::getParamsAndNameValues(
        "/test?name=%20%20trimmed%20%20", params, nameValues);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(1, params.size());
    TEST_ASSERT_EQUAL_STRING("test", params[0].c_str());
    
    TEST_ASSERT_EQUAL(1, nameValues.size());
    TEST_ASSERT_EQUAL_STRING("name", nameValues[0].name.c_str());
    // The value should be URL-decoded to "  trimmed  " and then trimmed to "trimmed"
    TEST_ASSERT_EQUAL_STRING("trimmed", nameValues[0].value.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test getJSONFromRESTRequest matches getParamsAndNameValues
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("JSON and Params methods consistency", "[restapi]")
{
    const char* testURL = "/api/device/control?id=42&action=start&mode=auto";
    
    // Test with getJSONFromRESTRequest
    RaftJson json = RestAPIEndpointManager::getJSONFromRESTRequest(testURL);
    std::vector<String> jsonSegments;
    json.getArrayElems("path", jsonSegments);
    
    // Test with getParamsAndNameValues
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    RestAPIEndpointManager::getParamsAndNameValues(testURL, params, nameValues);
    
    // Compare path segments
    TEST_ASSERT_EQUAL(jsonSegments.size(), params.size());
    for (size_t i = 0; i < jsonSegments.size(); i++)
    {
        TEST_ASSERT_EQUAL_STRING(jsonSegments[i].c_str(), params[i].c_str());
    }
    
    // Compare parameters
    TEST_ASSERT_EQUAL(3, nameValues.size());
    String id = json.getString("params/id", "");
    String action = json.getString("params/action", "");
    String mode = json.getString("params/mode", "");
    
    TEST_ASSERT_EQUAL_STRING(id.c_str(), nameValues[0].value.c_str());
    TEST_ASSERT_EQUAL_STRING(action.c_str(), nameValues[1].value.c_str());
    TEST_ASSERT_EQUAL_STRING(mode.c_str(), nameValues[2].value.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test edge case - only query parameters
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Edge case - only query parameters", "[restapi]")
{
    const char* testURL = "?key1=value1&key2=value2";
    
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    bool result = RestAPIEndpointManager::getParamsAndNameValues(testURL, params, nameValues);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(0, params.size());
    TEST_ASSERT_EQUAL(2, nameValues.size());
    TEST_ASSERT_EQUAL_STRING("key1", nameValues[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("value1", nameValues[0].value.c_str());
    TEST_ASSERT_EQUAL_STRING("key2", nameValues[1].name.c_str());
    TEST_ASSERT_EQUAL_STRING("value2", nameValues[1].value.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test edge case - single path segment
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("Edge case - single path segment", "[restapi]")
{
    const char* testURL = "/endpoint?param=value";
    
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    bool result = RestAPIEndpointManager::getParamsAndNameValues(testURL, params, nameValues);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(1, params.size());
    TEST_ASSERT_EQUAL_STRING("endpoint", params[0].c_str());
    TEST_ASSERT_EQUAL(1, nameValues.size());
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test getJSONFromRESTRequest with PATH_ONLY
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("getJSONFromRESTRequest - PATH_ONLY", "[restapi]")
{
    const char* testURL = "/api/device/control?id=42&action=start";
    
    RaftJson json = RestAPIEndpointManager::getJSONFromRESTRequest(
        testURL, RestAPIEndpointManager::PATH_ONLY);
    
    // Result should be a JSON array of path segments only
    std::vector<String> segments;
    TEST_ASSERT_TRUE(json.getArrayElems("", segments));
    TEST_ASSERT_EQUAL(3, segments.size());
    TEST_ASSERT_EQUAL_STRING("api", segments[0].c_str());
    TEST_ASSERT_EQUAL_STRING("device", segments[1].c_str());
    TEST_ASSERT_EQUAL_STRING("control", segments[2].c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test getJSONFromRESTRequest with PARAMS_ONLY
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("getJSONFromRESTRequest - PARAMS_ONLY", "[restapi]")
{
    const char* testURL = "/api/device/control?id=42&action=start&mode=auto";
    
    RaftJson json = RestAPIEndpointManager::getJSONFromRESTRequest(
        testURL, RestAPIEndpointManager::PARAMS_ONLY);
    
    // Result should be a JSON object with params only (no "params" wrapper)
    String id = json.getString("id", "");
    String action = json.getString("action", "");
    String mode = json.getString("mode", "");
    
    TEST_ASSERT_EQUAL_STRING("42", id.c_str());
    TEST_ASSERT_EQUAL_STRING("start", action.c_str());
    TEST_ASSERT_EQUAL_STRING("auto", mode.c_str());
    
    // Should not have path key
    std::vector<String> pathSegments;
    TEST_ASSERT_FALSE(json.getArrayElems("path", pathSegments));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test getJSONFromRESTRequest with PATH_AND_PARAMS (default behavior)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("getJSONFromRESTRequest - PATH_AND_PARAMS explicit", "[restapi]")
{
    const char* testURL = "/config/set?delay=100&enabled=true";
    
    RaftJson json = RestAPIEndpointManager::getJSONFromRESTRequest(
        testURL, RestAPIEndpointManager::PATH_AND_PARAMS);
    
    // Result should have both path and params
    std::vector<String> segments;
    TEST_ASSERT_TRUE(json.getArrayElems("path", segments));
    TEST_ASSERT_EQUAL(2, segments.size());
    TEST_ASSERT_EQUAL_STRING("config", segments[0].c_str());
    TEST_ASSERT_EQUAL_STRING("set", segments[1].c_str());
    
    String delay = json.getString("params/delay", "");
    String enabled = json.getString("params/enabled", "");
    
    TEST_ASSERT_EQUAL_STRING("100", delay.c_str());
    TEST_ASSERT_EQUAL_STRING("true", enabled.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test getJSONFromRESTRequest PATH_ONLY without query params
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("getJSONFromRESTRequest - PATH_ONLY no params", "[restapi]")
{
    const char* testURL = "/api/status/check";
    
    RaftJson json = RestAPIEndpointManager::getJSONFromRESTRequest(
        testURL, RestAPIEndpointManager::PATH_ONLY);
    
    // Result should be a JSON array of path segments
    std::vector<String> segments;
    TEST_ASSERT_TRUE(json.getArrayElems("", segments));
    TEST_ASSERT_EQUAL(3, segments.size());
    TEST_ASSERT_EQUAL_STRING("api", segments[0].c_str());
    TEST_ASSERT_EQUAL_STRING("status", segments[1].c_str());
    TEST_ASSERT_EQUAL_STRING("check", segments[2].c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test getJSONFromRESTRequest PARAMS_ONLY without path
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("getJSONFromRESTRequest - PARAMS_ONLY no path", "[restapi]")
{
    const char* testURL = "?cmd=motion&speed=100";
    
    RaftJson json = RestAPIEndpointManager::getJSONFromRESTRequest(
        testURL, RestAPIEndpointManager::PARAMS_ONLY);
    
    // Result should be a JSON object with params only
    String cmd = json.getString("cmd", "");
    String speed = json.getString("speed", "");
    
    TEST_ASSERT_EQUAL_STRING("motion", cmd.c_str());
    TEST_ASSERT_EQUAL_STRING("100", speed.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test getJSONFromRESTRequest PARAMS_ONLY with URL encoding
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE("getJSONFromRESTRequest - PARAMS_ONLY with encoding", "[restapi]")
{
    const char* testURL = "/ignored/path?query=hello%20world&value=test%2Bdata";
    
    RaftJson json = RestAPIEndpointManager::getJSONFromRESTRequest(
        testURL, RestAPIEndpointManager::PARAMS_ONLY);
    
    // Result should have decoded params
    String query = json.getString("query", "");
    String value = json.getString("value", "");
    
    TEST_ASSERT_EQUAL_STRING("hello world", query.c_str());
    TEST_ASSERT_EQUAL_STRING("test+data", value.c_str());
}