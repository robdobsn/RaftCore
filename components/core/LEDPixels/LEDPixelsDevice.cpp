////////////////////////////////////////////////////////////////////////////////
//
// LEDPixelsDevice.cpp
//
////////////////////////////////////////////////////////////////////////////////

#include "LEDPixelsDevice.h"

// #define DEBUG_LED_PIXELS_SETUP
// #define DEBUG_BUS_PIXELS_API

////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
/// @param pClassName
/// @param pDevConfigJson
LEDPixelsDevice::LEDPixelsDevice(const char* pClassName, const char *pDevConfigJson)
    : RaftDevice(pClassName, pDevConfigJson)
{
}

////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor
LEDPixelsDevice::~LEDPixelsDevice()
{
}

////////////////////////////////////////////////////////////////////////////////
/// @brief Setup function
void LEDPixelsDevice::setup()
{
    // Setup LED Pixels
    bool rslt = _ledPixels.setup(deviceConfig);

    // Log
#ifdef DEBUG_LED_PIXELS_SETUP
    LOG_I(MODULE_PREFIX, "setup %s numPixels %d numSegments %d",
            rslt ? "OK" : "FAILED", 
            _ledPixels.getNumPixels(), 
            _ledPixels.getNumSegments());
#endif
}

////////////////////////////////////////////////////////////////////////////////
/// @brief Main loop for the device (called frequently)
void LEDPixelsDevice::loop()
{
    // Loop LED pixels
    _ledPixels.loop();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief Add REST API endpoints
/// @param endpointManager
void LEDPixelsDevice::addRestAPIEndpoints(RestAPIEndpointManager &endpointManager)
{
    // LED
    endpointManager.addEndpoint("led", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                                std::bind(&LEDPixelsDevice::apiLED, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                                "led/<elem>/<cmd>/<data>?<args> - where <elem> is defined in SysTypes.json or index e.g. led/ring/set/0/FF0000 or led/ring/pattern/RainbowSnake?speed=1000&brightness=50");
    LOG_I(MODULE_PREFIX, "addRestAPIEndpoints led");
}

////////////////////////////////////////////////////////////////////////////////
/// @brief API for LED control
/// @param reqStr REST API request string
/// @param respStr REST API response string (JSON)
/// @param sourceInfo Source of the API request (channel)
RaftRetCode LEDPixelsDevice::apiLED(const String &reqStr, String &respStr, const APISourceInfo &sourceInfo)
{
    // Extract parameters
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    RestAPIEndpointManager::getParamsAndNameValues(reqStr.c_str(), params, nameValues);
    RaftJson nameValuesJson = RaftJson::getJSONFromNVPairs(nameValues, true);

    // Get element name or type
    String elemNameOrIdx = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);
    int32_t segmentIdx = _ledPixels.getSegmentIdx(elemNameOrIdx);
    if (segmentIdx < 0)
    {
        // Check for elemNameOrIdx being a segment index number - i.e. digits only
        bool isDigit = true;
        for (uint32_t i = 0; i < elemNameOrIdx.length(); i++)
        {
            if (!isdigit(elemNameOrIdx[i]))
            {
                isDigit = false;
                break;
            }
        }
        if (isDigit)
            segmentIdx = elemNameOrIdx.toInt();
    }
    if (segmentIdx < 0)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "invalidElement");
    String cmd = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 2);
    cmd.trim();
    String data = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 3);

    // Debug
#ifdef DEBUG_BUS_PIXELS_API
    LOG_I(MODULE_PREFIX, "apiLEDs req %s numParams %d elemNameOrIdx %s segmentIdx %d cmd %s data %s args %s",
          reqStr.c_str(), params.size(), elemNameOrIdx.c_str(), segmentIdx, cmd.c_str(), data.c_str(), nameValuesJson.c_str());
#endif

    // Handle commands
    bool rslt = false;
    if (cmd.equalsIgnoreCase("setall") || cmd.equalsIgnoreCase("color") || cmd.equalsIgnoreCase("colour"))
    {
        // Stop any pattern
        _ledPixels.stopPattern(segmentIdx, false);

        // Set all LEDs to a single colour
        if (data.startsWith("#"))
            data = data.substring(1);
        if (data.length() == 10)
        {
            // RRGGBBCCWW format (5-channel RGBWW: red, green, blue, cold white, warm white)
            uint64_t val = strtoull(data.c_str(), nullptr, 16);
            uint8_t r = (val >> 32) & 0xff, g = (val >> 24) & 0xff, b = (val >> 16) & 0xff;
            uint8_t cw = (val >> 8) & 0xff, ww = val & 0xff;
            for (uint32_t i = 0; i < _ledPixels.getNumPixels(segmentIdx); i++)
                _ledPixels.setRGBWW(segmentIdx, i, r, g, b, cw, ww, true);
        }
        else
        {
            auto rgb = Raft::getRGBFromHex(data);
            for (uint32_t i = 0; i < _ledPixels.getNumPixels(segmentIdx); i++)
                _ledPixels.setRGB(segmentIdx, i, rgb.r, rgb.g, rgb.b, true);
        }

        // Show
        _ledPixels.show();
        rslt = true;
    }
    else if (cmd.equalsIgnoreCase("setleds"))
    {
        // Stop any pattern
        _ledPixels.stopPattern(segmentIdx, false);

        // Set LEDs to a series of specified colours.
        // Stride is 10 chars (RRGGBBCCWW) when divisible by 10 but not 6, otherwise 6 chars (RRGGBB).
        const uint32_t stride = (data.length() % 10 == 0 && data.length() % 6 != 0) ? 10 : 6;
        for (uint32_t i = 0; i < _ledPixels.getNumPixels(segmentIdx); i++)
        {
            String subCol = data.substring(i * stride, i * stride + stride);
            if ((uint32_t)subCol.length() != stride)
                break;
            if (stride == 10)
            {
                uint64_t val = strtoull(subCol.c_str(), nullptr, 16);
                uint8_t r = (val >> 32) & 0xff, g = (val >> 24) & 0xff, b = (val >> 16) & 0xff;
                uint8_t cw = (val >> 8) & 0xff, ww = val & 0xff;
                _ledPixels.setRGBWW(segmentIdx, i, r, g, b, cw, ww, true);
            }
            else
            {
                auto rgb = Raft::getRGBFromHex(subCol);
                _ledPixels.setRGB(segmentIdx, i, rgb.r, rgb.g, rgb.b, true);
            }
        }

        // Show
        _ledPixels.show();
        rslt = true;
    }
    else if (cmd.equalsIgnoreCase("setled") || cmd.equalsIgnoreCase("set"))
    {
        // Stop pattern
        _ledPixels.stopPattern(segmentIdx, false);

        // Get LED and RGB for a single LED
        int ledID = strtol(data.c_str(), NULL, 10);
        String rgbStr = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 4);
        if (rgbStr.startsWith("#"))
            rgbStr = rgbStr.substring(1);
        if (rgbStr.length() == 10)
        {
            // RRGGBBCCWW format (5-channel RGBWW: red, green, blue, cold white, warm white)
            uint64_t val = strtoull(rgbStr.c_str(), nullptr, 16);
            uint8_t r = (val >> 32) & 0xff, g = (val >> 24) & 0xff, b = (val >> 16) & 0xff;
            uint8_t cw = (val >> 8) & 0xff, ww = val & 0xff;
#ifdef DEBUG_BUS_PIXELS_API
            LOG_I(MODULE_PREFIX, "setled %d %s r %d g %d b %d cw %d ww %d", ledID, rgbStr.c_str(), r, g, b, cw, ww);
#endif
            _ledPixels.setRGBWW(segmentIdx, ledID, r, g, b, cw, ww, true);
        }
        else
        {
            auto rgb = Raft::getRGBFromHex(rgbStr);
#ifdef DEBUG_BUS_PIXELS_API
            LOG_I(MODULE_PREFIX, "setled %d %s r %d g %d b %d", ledID, rgbStr.c_str(), rgb.r, rgb.g, rgb.b);
#endif
            _ledPixels.setRGB(segmentIdx, ledID, rgb.r, rgb.g, rgb.b, true);
        }
        _ledPixels.show();
        rslt = true;
    }
    else if (cmd.equalsIgnoreCase("off"))
    {
        // Turn off all LEDs
        _ledPixels.stopPattern(segmentIdx, false);
        _ledPixels.show();
        rslt = true;
    }
    else if (cmd.equalsIgnoreCase("pattern"))
    {
        // Set a named pattern
#ifdef DEBUG_BUS_PIXELS_API
        std::vector<String> patternNames;
        _ledPixels.getPatternNames(patternNames);
        String patternList;
        for (auto& name : patternNames)
        {
            if (patternList.length() > 0)
                patternList += ",";
            patternList += name;
        }
        LOG_I(MODULE_PREFIX, "pattern request %s segmentIdx %d patterns [%s]", data.c_str(), segmentIdx, patternList.c_str());
#endif
        _ledPixels.clear(false);
        _ledPixels.show();
        _ledPixels.setPattern(segmentIdx, data, nameValuesJson.c_str());
        rslt = true;
    }
    else if (cmd.equalsIgnoreCase("listpatterns"))
    {
        // Get list of patterns
        std::vector<String> patternNames;
        _ledPixels.getPatternNames(patternNames);
        String jsonResp;
        for (auto& name : patternNames)
        {
            if (jsonResp.length() > 0)
                jsonResp += ",";
            jsonResp += "\"" + name + "\"";
        }
        jsonResp = "\"patterns\":[" + jsonResp + "]";
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, jsonResp.c_str());
    }

    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, rslt);
}
