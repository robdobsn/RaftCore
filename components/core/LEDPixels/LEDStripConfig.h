/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// LedStripConfig.h - Configuration for LED strip
//
// Rob Dobson 2023
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

class LEDStripHwConfig
{
public:
    bool setup(const RaftJsonIF& config)
    {
        // Get data pin for LED strip
        ledDataPin = config.getLong("pin", -1);
        if (ledDataPin < 0)
        {
            LOG_W("LEDStripConfig", "setup invalid pixelDataPin");
            return false;
        }

        // Get number of pixels
        numPixels = config.getLong("num", DEFAULT_NUM_PIXELS);
        if (numPixels > MAX_NUM_PIXELS)
            numPixels = MAX_NUM_PIXELS;

        // RMT resolution
        rmtResolutionHz = config.getLong("rmtResolutionHz", rmtResolutionHz);

        // LED speed parameters
        bit0Duration0Us = config.getDouble("bit0Duration0Us", bit0Duration0Us);
        bit0Duration1Us = config.getDouble("bit0Duration1Us", bit0Duration1Us);
        bit1Duration0Us = config.getDouble("bit1Duration0Us", bit1Duration0Us);
        bit1Duration1Us = config.getDouble("bit1Duration1Us", bit1Duration1Us);
        resetDurationUs = config.getDouble("resetDurationUs", resetDurationUs);

        // MSB first
        msbFirst = config.getBool("msbFirst", msbFirst);
        return true;
    }

    String debugStr() const
    {
        String str = "numPix=" + String(numPixels) + " ledDataPin=" + String(ledDataPin) + " rmtResolutionHz=" + String(rmtResolutionHz) +
                     " bit0Duration0Us=" + String(bit0Duration0Us) + " bit0Duration1Us=" + String(bit0Duration1Us) +
                     " bit1Duration0Us=" + String(bit1Duration0Us) + " bit1Duration1Us=" + String(bit1Duration1Us) +
                     " resetDurationUs=" + String(resetDurationUs) + " msbFirst=" + String(msbFirst);
        return str;
    }

    // Parameters
    int ledDataPin = -1;
    uint32_t numPixels = 0;
    uint32_t rmtResolutionHz = 10000000;
    float bit0Duration0Us = 0.3;
    float bit0Duration1Us = 0.9;
    float bit1Duration0Us = 0.9;
    float bit1Duration1Us = 0.3;
    float resetDurationUs = 50;
    bool msbFirst = true;

    // Default and max num pixels
    static const uint32_t MAX_NUM_PIXELS = 1000;    
    static const uint32_t DEFAULT_NUM_PIXELS = 60;
};

class LEDStripConfig
{
public:
    LEDStripConfig()
    {
    }
    bool setup(const RaftJsonIF& config)
    {
        // Colour order
        String colourOrderStr = config.getString("colorOrder", config.getString("colourOrder", "GRB").c_str());
        colourOrder = LEDPixel::getColourOrderCode(colourOrderStr.c_str());

        // Get initial pattern
        initialPattern = config.getString("pattern", "");
        initialPatternMs = config.getLong("patternMs", 0);

        // Brightness percent
        pixelBrightnessFactor = config.getDouble("brightnessPC", pixelBrightnessFactor*100.0) / 100.0;

        // Startup first pixel colour
        String startupFirstPixelStr = config.getString("startupFirstPixel", "000000");
        startupFirstPixelColour = Raft::getRGBFromHex(startupFirstPixelStr);

        // Power pin for LED strips (powerLevel is the pin level to turn power on)
        powerPin = config.getLong("powerPin", -1);
        powerOnLevel = config.getLong("powerOnLevel", 1);

        // Strip hardware configs
        std::vector<String> stripHwConfigStrs;
        config.getArrayElems("strips", stripHwConfigStrs);

        // Check strip hardware config size
        if (stripHwConfigStrs.size() == 0)
        {
            LOG_W("LEDStripConfig", "setup no strip hardware configs");
            return false;
        }

        // Convert strip hardware configs
        totalPixels = 0;
        hwConfigs.resize(stripHwConfigStrs.size());
        for (int stripIdx = 0; stripIdx < stripHwConfigStrs.size(); stripIdx++)
        {
            if (!hwConfigs[stripIdx].setup(RaftJson(stripHwConfigStrs[stripIdx])))
            {
                totalPixels = 0;
                LOG_W("LEDStripConfig", "setup strip hardware config %d invalid", stripIdx);
                return false;
            }
            totalPixels += hwConfigs[stripIdx].numPixels;
        }
        return true;
    }

    // Get offset to start pixel for a strip
    uint32_t getPixelStartOffset(uint32_t stripIdx) const
    {
        if (stripIdx >= hwConfigs.size())
            return 0;
        uint32_t offset = 0;
        for (uint32_t idx = 0; idx < stripIdx; idx++)
            offset += hwConfigs[idx].numPixels;
        return offset;
    }

    // Parameters
    LEDPixel::ColourOrder colourOrder = LEDPixel::RGB;
    String initialPattern;
    uint32_t initialPatternMs = 0;
    Raft::RGBValue startupFirstPixelColour;
    float pixelBrightnessFactor = 1.0;
    int powerPin = -1;
    int powerLevel = 1;

    // LED strips
    uint32_t totalPixels = 0;
    std::vector<LEDStripHwConfig> hwConfigs;
};
