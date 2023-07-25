/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// LEDPixels.h
//
// Rob Dobson 2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <vector>
#include <LEDPixel.h>
#include <ESP32RMTLedStrip.h>
#include <LEDPatternBase.h>

class BusBase;
class ConfigBase;
class BusRequestResult;
class NamedValueProvider;

class LEDPixels
{
public:
    LEDPixels(NamedValueProvider* pNamedValueProvider = nullptr);
    virtual ~LEDPixels();

    // Setup
    bool setup(ConfigBase& config, const char* pConfigPrefix);
    bool setup(LEDStripConfig& ledStripConfig);

    // Service
    void service();

    // Set pattern
    void setPattern(const String& patternName);

    // Write to an individual LED
    void setPixelColor(uint32_t ledIdx, uint32_t r, uint32_t g, uint32_t b);
    void setPixelColor(uint32_t ledIdx, uint32_t c);
    void setPixelColor(uint32_t ledIdx, const LEDPixel& pixRGB);

    // Show 
    bool show();
    bool canShow()
    {
        return true;
    }

private:
    // Pixels
    std::vector<LEDPixel> _pixels;

    // Colour order for this LED strip
    LEDPixel::ColourOrder _colourOrder = LEDPixel::RGB;

    // Default and max num pixels
    static const uint32_t DEFAULT_NUM_PIXELS = 60;
    static const uint32_t MAX_NUM_PIXELS = 1000;

    // LED strip
    ESP32RMTLedStrip _ledStrip;

    // Interface to named values used in pattern generation
    NamedValueProvider* _pNamedValueProvider = nullptr;

    // LED pattern list item
    struct LEDPatternListItem
    {
        String name;
        LEDPatternBuildFunc buildFn;
    };

    // LED patterns
    std::vector<LEDPatternListItem> _ledPatterns;

    // Current pattern
    LEDPatternBase* _pCurrentPattern = nullptr;
    String _currentPatternName;
};
