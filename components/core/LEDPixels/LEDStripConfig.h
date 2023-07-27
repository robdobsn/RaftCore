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
                   uint32_t legacyChannel, const String& startupFirstPixel)
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
        this->legacyChannel = legacyChannel;
        this->startupFirstPixelColour = Raft::getRGBFromHex(startupFirstPixel);
    }
    String toStr() const
    {
        String str = "numPix=" + String(numPixels) + (initialPattern.isEmpty() ? String("") : " " + initialPattern) + 
                     " colourOrder=" + String(LEDPixel::getColourOrderStr(colourOrder)) +
                     " ledDataPin=" + String(ledDataPin) + " rmtResolutionHz=" + String(rmtResolutionHz) +
                     " bit0Duration0Us=" + String(bit0Duration0Us) + " bit0Duration1Us=" + String(bit0Duration1Us) +
                     " bit1Duration0Us=" + String(bit1Duration0Us) + " bit1Duration1Us=" + String(bit1Duration1Us) +
                     " resetDurationUs=" + String(resetDurationUs) + " msbFirst=" + String(msbFirst) +
                     " ledStripLegacyChannel=" + String(legacyChannel);
        return str;
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
    uint32_t legacyChannel = 0;
    Raft::RGBValue startupFirstPixelColour;
};
