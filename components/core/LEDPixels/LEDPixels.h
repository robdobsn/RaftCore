/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// LEDPixels.h
//
// Rob Dobson 2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include <functional>
#include <stdint.h>
#include "LEDPixel.h"
#include "ESP32RMTLedStrip.h"
#include "LEDPatternBase.h"
#include "esp_idf_version.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)

class RaftJsonIF;
class BusRequestResult;
class NamedValueProvider;

// Create function for LED pattern factory
typedef LEDPatternBase* (*LEDPatternCreateFn)(NamedValueProvider* pNamedValueProvider, LEDPixels& pixels);

// Pixel mapping function
typedef std::function<uint32_t(uint32_t)> LEDPixelMappingFn;

class LEDPixels
{
public:
    LEDPixels(NamedValueProvider* pNamedValueProvider = nullptr);
    virtual ~LEDPixels();

    // Setup
    bool setup(const RaftJsonIF& config);
    bool setup(LEDStripConfig& ledStripConfig);

    // Service
    void loop();

    // Set a mapping function to map from a pixel index to a physical LED index
    void setPixelMappingFn(LEDPixelMappingFn pixelMappingFn)
    {
        _pixelMappingFn = pixelMappingFn;
    }

    // Pattern handling
    void setPattern(const String& patternName, const char* pParamsJson=nullptr);
    void addPattern(const String& patternName, LEDPatternCreateFn createFn);
    void getPatternNames(std::vector<String>& patternNames);

    // Write to an individual LED
    void setRGB(uint32_t ledIdx, uint32_t r, uint32_t g, uint32_t b, bool applyBrightness=true);
    void setRGB(uint32_t ledIdx, uint32_t c, bool applyBrightness=true);
    void setRGB(uint32_t ledIdx, const LEDPixel& pixRGB);
    void setHSV(uint32_t ledIdx, uint32_t h, uint32_t s, uint32_t v);

    // Clear all pixels
    void clear(bool showAfterClear=false);

    // Get number of pixels
    uint32_t getNumPixels() const
    {
        return _ledStripConfig.totalPixels;
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

    // LED strips
    std::vector<ESP32RMTLedStrip> _ledStrips;

    // Interface to named values used in pattern generation
    NamedValueProvider* _pNamedValueProvider = nullptr;

    // Pixel mapping function
    LEDPixelMappingFn _pixelMappingFn = nullptr;

    // LED pattern list item
    struct LEDPatternListItem
    {
        String name;
        LEDPatternCreateFn createFn;
    };

    // LED patterns
    std::vector<LEDPatternListItem> _ledPatterns;

    // Current pattern
    LEDPatternBase* _pCurrentPattern = nullptr;
    String _currentPatternName;
    uint32_t _patternStartMs = 0;
    uint32_t _patternDurationMs = 0;

    // Debug
    static constexpr const char* MODULE_PREFIX = "LEDPix";
};

#endif