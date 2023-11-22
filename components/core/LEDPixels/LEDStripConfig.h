/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// LedStripConfig.h - Configuration for LED strip
//
// Rob Dobson 2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RaftArduino.h>
#include <LEDPixel.h>
#include <RaftUtils.h>

class LEDStripConfig
{
public:
    LEDStripConfig()
    {
    }
    LEDStripConfig(uint32_t numPixels, const char* pColourOrder, const char* pInitialPattern, 
                   int ledDataPin, uint32_t rmtResolutionHz, float bit0Duration0Us, float bit0Duration1Us,
                   float bit1Duration0Us, float bit1Duration1Us, float resetDurationUs, bool msbFirst,
                   const String& startupFirstPixel)
    {
        set(numPixels, pColourOrder, pInitialPattern, ledDataPin, rmtResolutionHz, bit0Duration0Us, bit0Duration1Us,
            bit1Duration0Us, bit1Duration1Us, resetDurationUs, msbFirst, startupFirstPixel);
    }
    void set(uint32_t numPixels, const char* pColourOrder, const char* pInitialPattern, 
             int ledDataPin, uint32_t rmtResolutionHz, float bit0Duration0Us, float bit0Duration1Us,
             float bit1Duration0Us, float bit1Duration1Us, float resetDurationUs, bool msbFirst,
             const String& startupFirstPixel)
    {
        this->numPixels = numPixels;
        this->colourOrder = LEDPixel::getColourOrderCode(pColourOrder);
        this->initialPattern = pInitialPattern ? pInitialPattern : "";
        this->ledDataPin = ledDataPin;
        this->rmtResolutionHz = rmtResolutionHz;
        this->bit0Duration0Us = bit0Duration0Us;
        this->bit0Duration1Us = bit0Duration1Us;
        this->bit1Duration0Us = bit1Duration0Us;
        this->bit1Duration1Us = bit1Duration1Us;
        this->resetDurationUs = resetDurationUs;
        this->msbFirst = msbFirst;
        this->startupFirstPixelColour = Raft::getRGBFromHex(startupFirstPixel);
    }
    String toStr() const
    {
        String str = "numPix=" + String(numPixels) + (initialPattern.isEmpty() ? String("") : " " + initialPattern) + 
                     " colourOrder=" + String(LEDPixel::getColourOrderStr(colourOrder)) +
                     " ledDataPin=" + String(ledDataPin) + " rmtResolutionHz=" + String(rmtResolutionHz) +
                     " bit0Duration0Us=" + String(bit0Duration0Us) + " bit0Duration1Us=" + String(bit0Duration1Us) +
                     " bit1Duration0Us=" + String(bit1Duration0Us) + " bit1Duration1Us=" + String(bit1Duration1Us) +
                     " resetDurationUs=" + String(resetDurationUs) + " msbFirst=" + String(msbFirst)
        return str;
    }
    bool setup(ConfigBase& config, const char* pConfigPrefix)
    {
        // Get number of pixels
        uint32_t numPixels = config.getLong("numPixels", DEFAULT_NUM_PIXELS, pConfigPrefix);
        if (numPixels > MAX_NUM_PIXELS)
            numPixels = MAX_NUM_PIXELS;

        // Colour order
        String colourOrderStr = config.getString("colourOrder", "GRB");

        // Get pattern
        String patternName = config.getString("pattern", "", pConfigPrefix);

        // Get data pin for LED strip
        int ledDataPin = config.getLong("pixelDataPin", -1, pConfigPrefix);
        if (ledDataPin < 0)
        {
            LOG_E(MODULE_PREFIX, "setup invalid pixelDataPin");
            return false;
        }

        // RMT resolution
        uint32_t rmtResolutionHz = config.getLong("rmtResolutionHz", ledStripConfig.rmtResolutionHz, pConfigPrefix);

        // LED speed parameters
        float bit0Duration0Us = config.getDouble("bit0Duration0Us", ledStripConfig.bit0Duration0Us, pConfigPrefix);
        float bit0Duration1Us = config.getDouble("bit0Duration1Us", ledStripConfig.bit0Duration1Us, pConfigPrefix);
        float bit1Duration0Us = config.getDouble("bit1Duration0Us", ledStripConfig.bit1Duration0Us, pConfigPrefix);
        float bit1Duration1Us = config.getDouble("bit1Duration1Us", ledStripConfig.bit1Duration1Us, pConfigPrefix);
        float resetDurationUs = config.getDouble("resetDurationUs", ledStripConfig.resetDurationUs, pConfigPrefix);

        // MSB first
        bool msbFirst = config.getBool("msbFirst", ledStripConfig.msbFirst, pConfigPrefix);

        // Startup first pixel colour
        String startupFirstPixel = config.getString("startupFirstPixel", "000000", pConfigPrefix);

        // Set parameters
        set(numPixels, colourOrderStr.c_str(), patternName.c_str(), ledDataPin, rmtResolutionHz, bit0Duration0Us, bit0Duration1Us,
            bit1Duration0Us, bit1Duration1Us, resetDurationUs, msbFirst, startupFirstPixel);

        return true;
    }

    uint32_t numPixels = 0;
    LEDPixel::ColourOrder colourOrder = LEDPixel::RGB;
    String initialPattern;
    int ledDataPin = -1;
    uint32_t rmtResolutionHz = 10000000;
    float bit0Duration0Us = 0.3;
    float bit0Duration1Us = 0.9;
    float bit1Duration0Us = 0.9;
    float bit1Duration1Us = 0.3;
    float resetDurationUs = 50;
    bool msbFirst = true;
    Raft::RGBValue startupFirstPixelColour;
};
