/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// LEDSegmentConfig.h - Configuration for LED segment
//
// Rob Dobson 2023-24
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Logger.h"
#include "RaftArduino.h"
#include "LEDPixel.h"
#include "RaftUtils.h"
#include "RaftJsonPrefixed.h"
#include "RaftJson.h"
#include "esp_idf_version.h"

class LEDSegmentConfig
{
public:
    LEDSegmentConfig()
    {
    }
    bool setup(const RaftJsonIF& config, float defaultBrightnessFactor)
    {
        // Segment name
        name = config.getString("name", "");

        // Segment start offset from the start of the LED pixels
        startOffset = config.getLong("start", 0);

        // Number of pixels
        numPixels = config.getLong("num", 0);

        // Get initial pattern
        initialPattern = config.getString("pattern", "");
        initialPatternMs = config.getLong("patternMs", 0);
        String patternParamsQueryStr = config.getString("patternParams", "");
        initialPatternParamsJson = Raft::getJSONFromHTTPQueryStr(patternParamsQueryStr.c_str(), false, true);

        // Brightness percent
        pixelBrightnessFactor = config.getDouble("brightnessPC", defaultBrightnessFactor*100.0) / 100.0;

        // Startup first pixel colour
        String startupFirstPixelStr = config.getString("startupFirstPixel", "000000");
        startupFirstPixelColour = Raft::getRGBFromHex(startupFirstPixelStr);

        // Colour order
        String colourOrderStr = config.getString("colorOrder", config.getString("colourOrder", "GRB").c_str());
        colourOrder = LEDPixel::getColourOrderCode(colourOrderStr.c_str());

        return true;
    }

    // Segment name
    String name;

    // Segment start offset from the start of the LED pixels
    uint32_t startOffset = 0;

    // Number of pixels in the segment
    uint32_t numPixels = 0;

    // Parameters
    float pixelBrightnessFactor = 1.0;

    // Initial pattern
    String initialPattern;
    uint32_t initialPatternMs = 0;
    String initialPatternParamsJson = "{}";

    // Startup first pixel colour
    Raft::RGBValue startupFirstPixelColour;

    // Colour order - all strips covered by this segment must use the same colour order
    LEDPixel::ColourOrder colourOrder = LEDPixel::BGR;

};
