# RaftCore REST API Endpoints

This document describes how to add REST API endpoints from a user SysMod (System Module) in the Raft framework. The REST API system provides a unified interface for handling commands that can be received over multiple communication channels.

## Table of Contents

1. [Overview](#overview)
2. [Adding REST API Endpoints](#adding-rest-api-endpoints)
3. [Communication Channels](#communication-channels)
4. [API Handler Methods](#api-handler-methods)
5. [Parsing Request Arguments](#parsing-request-arguments)
6. [Creating Responses](#creating-responses)
7. [Advanced Endpoint Types](#advanced-endpoint-types)
8. [Complete Example](#complete-example)
9. [API Reference](#api-reference)

---

## Overview

The Raft framework implements a REST-like API system that allows System Modules (SysMods) to expose endpoints for control and configuration. Unlike traditional REST APIs that are solely HTTP-based, Raft's API endpoints are **transport-agnostic** and can be reached through multiple communication channels.

### Key Concepts

- **RestAPIEndpointManager**: Central manager that routes API requests to registered endpoint handlers
- **RaftSysMod**: Base class for system modules that can register API endpoints
- **ProtocolExchange**: Routes messages from various communication channels to the API endpoint manager
- **APISourceInfo**: Contains information about the source of an API request (e.g., which channel it came from)

---

## Adding REST API Endpoints

### Step 1: Override `addRestAPIEndpoints()`

In your SysMod class derived from `RaftSysMod`, override the `addRestAPIEndpoints()` method:

```cpp
#include "RaftSysMod.h"
#include "RestAPIEndpointManager.h"

class MySysMod : public RaftSysMod
{
public:
    MySysMod(const char* pModuleName, RaftJsonIF& sysConfig)
        : RaftSysMod(pModuleName, sysConfig)
    {
    }

protected:
    // Override to register API endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& endpointManager) override
    {
        // Register endpoints here
    }
};
```

### Step 2: Register Endpoints

Use `endpointManager.addEndpoint()` to register your API endpoints:

```cpp
void MySysMod::addRestAPIEndpoints(RestAPIEndpointManager& endpointManager)
{
    // Basic GET endpoint
    endpointManager.addEndpoint(
        "myapi",                                    // Endpoint name (URL path segment)
        RestAPIEndpoint::ENDPOINT_CALLBACK,         // Endpoint type
        RestAPIEndpoint::ENDPOINT_GET,              // HTTP method (GET, POST, PUT, DELETE)
        std::bind(&MySysMod::apiHandler, this,      // Callback function
                  std::placeholders::_1, 
                  std::placeholders::_2, 
                  std::placeholders::_3),
        "Description of what this endpoint does"    // Help text
    );
}
```

### Endpoint Parameters

The `addEndpoint()` method accepts the following parameters:

| Parameter | Type | Description |
|-----------|------|-------------|
| `pEndpointStr` | `const char*` | The endpoint name (first path segment) |
| `endpointType` | `EndpointType` | Usually `ENDPOINT_CALLBACK` |
| `endpointMethod` | `EndpointMethod` | `ENDPOINT_GET`, `ENDPOINT_POST`, `ENDPOINT_PUT`, `ENDPOINT_DELETE` |
| `callbackMain` | `RestAPIFunction` | Main callback for handling requests |
| `pDescription` | `const char*` | Description string (for documentation/help) |
| `pContentType` | `const char*` | Optional content type (default: `application/json`) |
| `pContentEncoding` | `const char*` | Optional content encoding |
| `cacheControl` | `EndpointCache_t` | `ENDPOINT_CACHE_NEVER` or `ENDPOINT_CACHE_ALWAYS` |
| `pExtraHeaders` | `const char*` | Optional extra HTTP headers |
| `callbackBody` | `RestAPIFnBody` | Optional callback for handling request body data |
| `callbackChunk` | `RestAPIFnChunk` | Optional callback for handling chunked/streamed data |
| `callbackIsReady` | `RestAPIFnIsReady` | Optional callback to check if endpoint is ready |

---

## Communication Channels

One of the key features of Raft's API system is that endpoints registered with `RestAPIEndpointManager` can be accessed through **multiple communication channels**, not just HTTP. This allows the same command to work regardless of how it's delivered to the device.

### Supported Channels

| Channel | Description | Example Use Case |
|---------|-------------|------------------|
| **HTTP/HTTPS** | Via RaftWebServer over WiFi | Browser-based control, mobile apps |
| **BLE (Bluetooth Low Energy)** | Via BLEManager | Mobile app communication without WiFi |
| **Serial Console** | Via SerialConsole | USB debugging, command-line interface |
| **Command Serial** | Via CommandSerial | External microcontrollers, UART bridges |
| **WebSocket** | Via RaftWebServer WebSocket handler | Real-time bidirectional communication |

### How It Works

The `ProtocolExchange` SysMod acts as a central hub for routing API requests:

1. Messages arrive on a communication channel (HTTP, BLE, Serial, etc.)
2. The channel decodes the message using the appropriate protocol codec (e.g., RICSerial, RICREST)
3. `ProtocolExchange` extracts the API request string from the message
4. The request is passed to `RestAPIEndpointManager::handleApiRequest()`
5. The matching endpoint handler is called with the request
6. The response is sent back through the same channel

```
┌─────────────┐   ┌─────────────┐   ┌─────────────┐   ┌─────────────┐
│ RaftWebServer│   │ BLEManager  │   │SerialConsole│   │CommandSerial│
└──────┬──────┘   └──────┬──────┘   └──────┬──────┘   └──────┬──────┘
       │                 │                 │                 │
       ▼                 ▼                 ▼                 ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    CommsChannelManager                              │
└──────────────────────────────┬──────────────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      ProtocolExchange                               │
│   - Decodes protocol-specific messages (RICSerial, RICREST, etc.)   │
│   - Extracts API request strings                                    │
│   - Routes to RestAPIEndpointManager                                │
└──────────────────────────────┬──────────────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    RestAPIEndpointManager                           │
│   - Matches request to registered endpoints                         │
│   - Calls endpoint handler callbacks                                │
└──────────────────────────────┬──────────────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│                Your SysMod's API Handler Method                     │
└─────────────────────────────────────────────────────────────────────┘
```

### Channel-Agnostic Responses

Because endpoints are channel-agnostic, your API handlers should:
- Not assume any specific transport mechanism
- Return JSON responses (the standard format for all channels)
- Use the `sourceInfo.channelID` if you need to identify or respond to specific channels

---

## API Handler Methods

### Callback Signature

The main API handler callback has this signature:

```cpp
RaftRetCode apiHandler(const String& reqStr, String& respStr, const APISourceInfo& sourceInfo);
```

**Parameters:**

| Parameter | Type | Direction | Description |
|-----------|------|-----------|-------------|
| `reqStr` | `const String&` | Input | The full request string (e.g., `"myapi/subcmd/param1?name=value"`) |
| `respStr` | `String&` | Output | The response string to be filled with JSON response |
| `sourceInfo` | `const APISourceInfo&` | Input | Information about the request source |

**Return Value:**

Return a `RaftRetCode` value:

| Return Code | Meaning |
|-------------|---------|
| `RAFT_OK` | Success |
| `RAFT_INVALID_DATA` | Invalid parameters or request |
| `RAFT_NOT_IMPLEMENTED` | Feature not implemented |
| `RAFT_OTHER_FAILURE` | General failure |

### APISourceInfo Structure

The `APISourceInfo` structure provides information about where the request originated:

```cpp
class APISourceInfo
{
public:
    APISourceInfo(uint32_t channelID)
    {
        this->channelID = channelID;
    }
    uint32_t channelID;
};
```

The `channelID` identifies which communication channel the request came from. This can be useful for:
- Logging and debugging
- Implementing channel-specific behavior
- Sending responses on specific channels

---

## Parsing Request Arguments

### Request String Format

Request strings follow a URL-like format:

```
endpoint/subcommand/param1/param2?query_name1=value1&query_name2=value2
```

For example:
- `myapi/status` - Get status
- `myapi/setvalue/123` - Set value to 123
- `myapi/config?timeout=1000&enable=true` - Set config with query parameters

### Method 1: Using `getNthArgStr()` (Simple Path Arguments)

For extracting path segments by position:

```cpp
RaftRetCode MySysMod::apiHandler(const String& reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // reqStr = "myapi/subcommand/value123?param=xyz"
    
    // Get path segments (0-indexed, argument 0 is the endpoint name)
    String endpointName = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 0);  // "myapi"
    String subCommand = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);    // "subcommand"
    String value = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 2);         // "value123"
    
    // Process the command...
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
}
```

### Method 2: Using `getParamsAndNameValues()` (Path + Query Parameters)

For extracting both path segments and query parameters:

```cpp
RaftRetCode MySysMod::apiHandler(const String& reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // reqStr = "myapi/command?timeout=1000&enable=true"
    
    // Parse request
    std::vector<String> params;                      // Path segments
    std::vector<RaftJson::NameValuePair> nameValues; // Query parameters
    RestAPIEndpointManager::getParamsAndNameValues(reqStr.c_str(), params, nameValues);
    
    // Access path segments
    if (params.size() < 2)
    {
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "notEnoughParams");
    }
    
    String endpointName = params[0];  // "myapi"
    String command = params[1];       // "command"
    
    // Convert query parameters to JSON for easy access
    RaftJson queryJson = RaftJson::getJSONFromNVPairs(nameValues, true);
    
    // Access query parameters
    int timeout = queryJson.getLong("timeout", 0);       // 1000
    bool enable = queryJson.getBool("enable", false);    // true
    
    // Process the command...
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
}
```

### Method 3: Using `getJSONFromRESTRequest()` (Structured JSON)

For getting a fully structured JSON representation:

```cpp
RaftRetCode MySysMod::apiHandler(const String& reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Get complete JSON with path and params
    // Result: {"path":["myapi","command"],"params":{"timeout":1000,"enable":true}}
    RaftJson requestJson = RestAPIEndpointManager::getJSONFromRESTRequest(
        reqStr.c_str(), 
        RestAPIEndpointManager::PATH_AND_PARAMS
    );
    
    // Access components
    String command = requestJson.getString("path[1]", "");
    int timeout = requestJson.getLong("params/timeout", 0);
    
    // Process the command...
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
}
```

### Utility Methods Summary

| Method | Purpose | Example |
|--------|---------|---------|
| `getNthArgStr(reqStr, n)` | Get nth path segment | Path: `/api/cmd/value` → `getNthArgStr(s, 1)` returns `"cmd"` |
| `getParamsAndNameValues(reqStr, params, nvPairs)` | Parse path and query params | Full parsing into vectors |
| `getJSONFromRESTRequest(reqStr, elements)` | Get JSON representation | Returns RaftJson with path array and params object |
| `getNumArgs(reqStr)` | Count path segments | Returns number of `/`-separated segments |
| `removeFirstArgStr(reqStr)` | Remove first path segment | For delegating to sub-handlers |

---

## Creating Responses

### Standard Response Format

Raft uses a consistent JSON response format:

```json
{
    "req": "original_request_string",
    "rslt": "ok",              // or "fail"
    "error": "error message",  // Optional, on failure
    // ... additional fields
}
```

### Response Helper Functions

The `Raft` namespace provides helper functions for creating properly formatted responses:

#### `Raft::setJsonBoolResult()` - Simple Success/Failure

```cpp
// Simple success
return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
// Response: {"req":"myapi/cmd","rslt":"ok"}

// Simple failure  
return Raft::setJsonBoolResult(reqStr.c_str(), respStr, false);
// Response: {"req":"myapi/cmd","rslt":"fail"}

// Success with additional data
return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, 
    R"("value":42,"name":"test")");
// Response: {"req":"myapi/cmd","value":42,"name":"test","rslt":"ok"}
```

**Signature:**
```cpp
RaftRetCode Raft::setJsonBoolResult(
    const char* req,          // Original request string
    String& resp,             // Output response string
    bool rslt,                // Success (true) or failure (false)
    const char* otherJson = nullptr  // Optional additional JSON fields
);
```

#### `Raft::setJsonErrorResult()` - Error with Message

```cpp
return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "invalidParameter");
// Response: {"req":"myapi/cmd","rslt":"fail","error":"invalidParameter"}

// With additional data
return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "deviceNotFound",
    R"("deviceId":"ABC123")");
// Response: {"req":"myapi/cmd","deviceId":"ABC123","rslt":"fail","error":"deviceNotFound"}
```

**Signature:**
```cpp
RaftRetCode Raft::setJsonErrorResult(
    const char* req,                  // Original request string
    String& resp,                     // Output response string
    const char* errorMsg,             // Error message
    const char* otherJson = nullptr,  // Optional additional JSON fields
    RaftRetCode retCode = RAFT_OTHER_FAILURE  // Return code
);
```

#### `Raft::setJsonResult()` - Full Control

```cpp
// Full control over response
return Raft::setJsonResult(reqStr.c_str(), respStr, true, nullptr, 
    R"("status":"running","count":5)");
// Response: {"req":"myapi/cmd","status":"running","count":5,"rslt":"ok"}

// Error with message and additional data
return Raft::setJsonResult(reqStr.c_str(), respStr, false, "timeout",
    R"("elapsedMs":5000)");
// Response: {"req":"myapi/cmd","elapsedMs":5000,"rslt":"fail","error":"timeout"}
```

**Signature:**
```cpp
RaftRetCode Raft::setJsonResult(
    const char* pReq,                 // Original request string
    String& resp,                     // Output response string
    bool rslt,                        // Success or failure
    const char* errorMsg = nullptr,   // Optional error message
    const char* otherJson = nullptr   // Optional additional JSON fields
);
```

### Custom Response Formatting

For complex responses, you can build the JSON string directly:

```cpp
RaftRetCode MySysMod::apiGetDevices(const String& reqStr, String& respStr, const APISourceInfo& sourceInfo)
{
    // Build a custom response
    char jsonBuffer[512];
    snprintf(jsonBuffer, sizeof(jsonBuffer),
        R"({"req":"%s","rslt":"ok","devices":[{"id":1,"name":"Sensor1"},{"id":2,"name":"Sensor2"}],"count":2})",
        reqStr.c_str()
    );
    respStr = jsonBuffer;
    return RAFT_OK;
}
```

---

## Advanced Endpoint Types

### POST Endpoints with Body Data

For endpoints that receive body data (e.g., file uploads, JSON payloads):

```cpp
void MySysMod::addRestAPIEndpoints(RestAPIEndpointManager& endpointManager)
{
    endpointManager.addEndpoint(
        "upload",
        RestAPIEndpoint::ENDPOINT_CALLBACK,
        RestAPIEndpoint::ENDPOINT_POST,
        std::bind(&MySysMod::apiUpload, this, 
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
        "Upload data",
        "application/json",  // Content type expected
        NULL,                // Content encoding
        RestAPIEndpoint::ENDPOINT_CACHE_NEVER,
        NULL,                // Extra headers
        std::bind(&MySysMod::apiUploadBody, this,  // Body callback
                  std::placeholders::_1, std::placeholders::_2, 
                  std::placeholders::_3, std::placeholders::_4,
                  std::placeholders::_5, std::placeholders::_6),
        NULL                 // Chunk callback
    );
}

RaftRetCode MySysMod::apiUpload(const String& reqStr, String& respStr, 
                                 const APISourceInfo& sourceInfo)
{
    // Called when the request is complete
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
}

RaftRetCode MySysMod::apiUploadBody(const String& reqStr, const uint8_t* pData, 
                                     size_t len, size_t index, size_t total,
                                     const APISourceInfo& sourceInfo)
{
    // Called for each chunk of body data
    // pData: Pointer to data chunk
    // len: Length of this chunk
    // index: Offset in the total data
    // total: Total expected data length
    
    // Process data chunk...
    return RAFT_OK;
}
```

### Chunked/Streaming Endpoints

For endpoints that handle file streaming:

```cpp
void MySysMod::addRestAPIEndpoints(RestAPIEndpointManager& endpointManager)
{
    endpointManager.addEndpoint(
        "fileupload",
        RestAPIEndpoint::ENDPOINT_CALLBACK,
        RestAPIEndpoint::ENDPOINT_POST,
        std::bind(&MySysMod::apiFileUpload, this,
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
        "File upload endpoint",
        NULL, NULL,
        RestAPIEndpoint::ENDPOINT_CACHE_NEVER,
        NULL,
        NULL,  // Body callback (not used)
        std::bind(&MySysMod::apiFileUploadChunk, this,  // Chunk callback
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
    );
}

RaftRetCode MySysMod::apiFileUploadChunk(const String& reqStr, 
                                          FileStreamBlock& fileStreamBlock,
                                          const APISourceInfo& sourceInfo)
{
    // Handle file stream block
    // fileStreamBlock contains file data chunks, filename, etc.
    return RAFT_OK;
}
```

### Ready Check Callback

For endpoints that may need to signal when they're not ready to accept requests:

```cpp
endpointManager.addEndpoint(
    "slowapi",
    RestAPIEndpoint::ENDPOINT_CALLBACK,
    RestAPIEndpoint::ENDPOINT_GET,
    std::bind(&MySysMod::apiSlow, this, 
              std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
    "Endpoint that may not always be ready",
    NULL, NULL,
    RestAPIEndpoint::ENDPOINT_CACHE_NEVER,
    NULL,
    NULL,  // Body callback
    NULL,  // Chunk callback
    std::bind(&MySysMod::apiSlowIsReady, this, std::placeholders::_1)  // Ready check
);

bool MySysMod::apiSlowIsReady(const APISourceInfo& sourceInfo)
{
    // Return false if not ready to handle requests
    return _isInitialized && !_isBusy;
}
```

---

## Complete Example

Here's a complete example SysMod with REST API endpoints:

```cpp
// MySysMod.h
#pragma once

#include "RaftSysMod.h"
#include "RestAPIEndpointManager.h"

class MySysMod : public RaftSysMod
{
public:
    MySysMod(const char* pModuleName, RaftJsonIF& sysConfig);
    virtual ~MySysMod();
    
    // Factory method for SysManager
    static RaftSysMod* create(const char* pModuleName, RaftJsonIF& sysConfig)
    {
        return new MySysMod(pModuleName, sysConfig);
    }

protected:
    virtual void setup() override;
    virtual void loop() override;
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& endpointManager) override;

private:
    // API handlers
    RaftRetCode apiStatus(const String& reqStr, String& respStr, const APISourceInfo& sourceInfo);
    RaftRetCode apiConfig(const String& reqStr, String& respStr, const APISourceInfo& sourceInfo);
    RaftRetCode apiControl(const String& reqStr, String& respStr, const APISourceInfo& sourceInfo);
    
    // State
    int _counter = 0;
    bool _enabled = false;
    
    static constexpr const char* MODULE_PREFIX = "MySysMod";
};
```

```cpp
// MySysMod.cpp
#include "MySysMod.h"
#include "RaftUtils.h"

MySysMod::MySysMod(const char* pModuleName, RaftJsonIF& sysConfig)
    : RaftSysMod(pModuleName, sysConfig)
{
}

MySysMod::~MySysMod()
{
}

void MySysMod::setup()
{
    // Read config
    _enabled = configGetBool("enable", false);
    _counter = configGetLong("initialCount", 0);
    
    LOG_I(MODULE_PREFIX, "setup enabled=%d counter=%d", _enabled, _counter);
}

void MySysMod::loop()
{
    // Periodic processing
    if (_enabled)
    {
        _counter++;
    }
}

void MySysMod::addRestAPIEndpoints(RestAPIEndpointManager& endpointManager)
{
    // Status endpoint - GET /mymod or /mymod/status
    endpointManager.addEndpoint(
        "mymod",
        RestAPIEndpoint::ENDPOINT_CALLBACK,
        RestAPIEndpoint::ENDPOINT_GET,
        std::bind(&MySysMod::apiStatus, this, 
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
        "Get status: mymod, mymod/status"
    );
    
    // Config endpoint - GET /myconfig?param=value
    endpointManager.addEndpoint(
        "myconfig",
        RestAPIEndpoint::ENDPOINT_CALLBACK,
        RestAPIEndpoint::ENDPOINT_GET,
        std::bind(&MySysMod::apiConfig, this,
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
        "Configuration: myconfig?enable=1, myconfig?reset=1"
    );
    
    // Control endpoint - GET /mycontrol/start, /mycontrol/stop
    endpointManager.addEndpoint(
        "mycontrol",
        RestAPIEndpoint::ENDPOINT_CALLBACK,
        RestAPIEndpoint::ENDPOINT_GET,
        std::bind(&MySysMod::apiControl, this,
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
        "Control: mycontrol/start, mycontrol/stop, mycontrol/reset"
    );
    
    LOG_I(MODULE_PREFIX, "addRestAPIEndpoints registered mymod, myconfig, mycontrol");
}

RaftRetCode MySysMod::apiStatus(const String& reqStr, String& respStr, 
                                 const APISourceInfo& sourceInfo)
{
    // Check for subcommand
    String subCmd = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);
    
    // Build status response
    char extraJson[128];
    snprintf(extraJson, sizeof(extraJson),
        R"("enabled":%s,"counter":%d,"channelID":%u)",
        _enabled ? "true" : "false",
        _counter,
        sourceInfo.channelID
    );
    
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, extraJson);
}

RaftRetCode MySysMod::apiConfig(const String& reqStr, String& respStr,
                                 const APISourceInfo& sourceInfo)
{
    // Parse parameters
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    RestAPIEndpointManager::getParamsAndNameValues(reqStr.c_str(), params, nameValues);
    RaftJson queryJson = RaftJson::getJSONFromNVPairs(nameValues, true);
    
    // Process enable parameter
    if (queryJson.contains("enable"))
    {
        _enabled = queryJson.getBool("enable", _enabled);
        LOG_I(MODULE_PREFIX, "apiConfig enable=%d", _enabled);
    }
    
    // Process reset parameter
    if (queryJson.getBool("reset", false))
    {
        _counter = 0;
        LOG_I(MODULE_PREFIX, "apiConfig reset counter");
    }
    
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
}

RaftRetCode MySysMod::apiControl(const String& reqStr, String& respStr,
                                  const APISourceInfo& sourceInfo)
{
    // Get control command
    String command = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);
    
    if (command.isEmpty())
    {
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "noCommand");
    }
    
    if (command.equalsIgnoreCase("start"))
    {
        _enabled = true;
        LOG_I(MODULE_PREFIX, "apiControl start");
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
    }
    else if (command.equalsIgnoreCase("stop"))
    {
        _enabled = false;
        LOG_I(MODULE_PREFIX, "apiControl stop");
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
    }
    else if (command.equalsIgnoreCase("reset"))
    {
        _counter = 0;
        LOG_I(MODULE_PREFIX, "apiControl reset");
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
    }
    
    return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "unknownCommand");
}
```

### Testing the Endpoints

Once registered, the endpoints can be accessed via:

**HTTP (via RaftWebServer):**
```
GET http://192.168.1.100/api/mymod
GET http://192.168.1.100/api/myconfig?enable=1
GET http://192.168.1.100/api/mycontrol/start
```

**Serial Console:**
```
mymod
myconfig?enable=1
mycontrol/start
```

**BLE (via mobile app):**
The same command strings are sent via the BLE RICSerial protocol.

---

## API Reference

### RestAPIEndpointManager Methods

| Method | Description |
|--------|-------------|
| `addEndpoint(...)` | Register a new API endpoint |
| `getEndpoint(name)` | Get endpoint definition by name |
| `handleApiRequest(req, resp, sourceInfo)` | Handle an API request |
| `getNthArgStr(reqStr, n)` | Get nth path segment |
| `getParamsAndNameValues(reqStr, params, nvPairs)` | Parse path and query params |
| `getJSONFromRESTRequest(reqStr, elements)` | Get JSON representation of request |
| `getNumArgs(reqStr)` | Count path segments |
| `removeFirstArgStr(reqStr)` | Remove first path segment |
| `unencodeHTTPChars(str)` | Decode URL-encoded characters |

### Raft Response Helpers

| Method | Description |
|--------|-------------|
| `Raft::setJsonBoolResult(req, resp, rslt, otherJson)` | Create success/fail response |
| `Raft::setJsonErrorResult(req, resp, errorMsg, otherJson, retCode)` | Create error response |
| `Raft::setJsonResult(req, resp, rslt, errorMsg, otherJson)` | Create full response |

### RestAPIEndpoint Types

| Enum Value | Description |
|------------|-------------|
| `ENDPOINT_CALLBACK` | Standard callback endpoint |
| `ENDPOINT_NONE` | No endpoint (placeholder) |

### RestAPIEndpoint Methods

| Enum Value | Description |
|------------|-------------|
| `ENDPOINT_GET` | HTTP GET method |
| `ENDPOINT_POST` | HTTP POST method |
| `ENDPOINT_PUT` | HTTP PUT method |
| `ENDPOINT_DELETE` | HTTP DELETE method |
| `ENDPOINT_OPTIONS` | HTTP OPTIONS method |

### RaftRetCode Values

| Code | Value | Description |
|------|-------|-------------|
| `RAFT_OK` | 0 | Success |
| `RAFT_INVALID_DATA` | | Invalid parameters |
| `RAFT_NOT_IMPLEMENTED` | | Not implemented |
| `RAFT_OTHER_FAILURE` | | General failure |

---

## Best Practices

1. **Always return JSON responses** - All channels expect JSON-formatted responses

2. **Use the helper functions** - `Raft::setJsonBoolResult()` and friends ensure consistent formatting

3. **Validate input parameters** - Check for required arguments before processing

4. **Handle missing arguments gracefully** - Return meaningful error messages

5. **Log important operations** - Use `LOG_I()` for debugging (can be disabled in production)

6. **Keep handlers efficient** - API handlers should complete quickly; use the loop() for long operations

7. **Document your endpoints** - Provide clear descriptions in the `addEndpoint()` calls

8. **Consider all channels** - Your endpoint may be called via HTTP, BLE, or Serial

9. **Use meaningful endpoint names** - Short, descriptive names that follow URL conventions

10. **Group related functionality** - Use path segments for sub-commands rather than many separate endpoints
