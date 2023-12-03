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
    bool setup(const ConfigBase& config, const char* pConfigPrefix);
    bool setup(LEDStripConfig& ledStripConfig);

    // Service
    void service();

    // Set pattern
    void setPattern(const String& patternName);

    // Write to an individual LED
    void setPixelColor(uint32_t ledIdx, uint32_t r, uint32_t g, uint32_t b, bool applyBrightness=true);
    void setPixelColor(uint32_t ledIdx, uint32_t c, bool applyBrightness=true);
    void setPixelColor(uint32_t ledIdx, const LEDPixel& pixRGB);

    // Clear all pixels
    void clear(bool showAfterClear=false);

    // Get number of pixels
    uint32_t getNumPixels() const
    {
        return _ledStripConfig.numPixels;
    }

    // Get data pin
    int getDataPin() const
    {
        return _ledStripConfig.ledDataPin;
    }
    
    // Show 
    bool show();
    bool canShow()
    {
        return true;
    }

    // Wait until show complete
    void waitUntilShowComplete();

private:
    // Pixels
    std::vector<LEDPixel> _pixels;

    // Config
    LEDStripConfig _ledStripConfig;

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
