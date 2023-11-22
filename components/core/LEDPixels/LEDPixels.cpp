/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// LEDPixels.cpp
//
// Rob Dobson 2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "LEDPixels.h"
#include <RaftUtils.h>
#include <ConfigBase.h>

// #define DEBUG_LED_PIXEL_VALUES

// Module prefix
static const char *MODULE_PREFIX = "LEDPixels";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor and destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

LEDPixels::LEDPixels(NamedValueProvider* pNamedValueProvider)
    : _pNamedValueProvider(pNamedValueProvider)
{
}

LEDPixels::~LEDPixels()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool LEDPixels::setup(LEDStripConfig& ledStripConfig)
{
    // Colour order
    _colourOrder = ledStripConfig.colourOrder;

    // Setup pixels
    _pixels.resize(ledStripConfig.numPixels);

    // Setup hardware
    bool rslt = _ledStrip.setup(ledStripConfig);

    // Set pattern
    setPattern(ledStripConfig.initialPattern);

    // Log
    LOG_I(MODULE_PREFIX, "setup %s numPixels %d pixelsPin %d", 
                rslt ? "OK" : "FAILED", (int)ledStripConfig.numPixels, (int)ledStripConfig.ledDataPin);
    return rslt;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LEDPixels::service()
{
    // Service pattern that is active
    if (_pCurrentPattern)
    {
        _pCurrentPattern->service();
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set a pattern
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LEDPixels::setPattern(const String& patternName)
{
    // Check for existing pattern with differnt name and remove if so
    if (_pCurrentPattern && _currentPatternName != patternName)
    {
        delete _pCurrentPattern;
        _pCurrentPattern = nullptr;
        _currentPatternName = "";
    }

    // Find pattern
    for (auto& pattern : _ledPatterns)
    {
        if (pattern.name.equalsIgnoreCase(patternName))
        {
            // Build pattern
            LEDPatternBase* pPattern = pattern.buildFn(_pNamedValueProvider, _pixels, _ledStrip);
            if (pPattern)
            {
                // Set pattern
                _pCurrentPattern = pPattern;
                _currentPatternName = patternName;
            }
            return;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Write to an individual LED
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LEDPixels::setPixelColor(uint32_t ledIdx, uint32_t r, uint32_t g, uint32_t b)
{
    if (ledIdx >= _pixels.size())
        return;
    _pixels[ledIdx].fromRGB(r, g, b, _colourOrder);
#ifdef DEBUG_LED_PIXEL_VALUES
    LOG_I(MODULE_PREFIX, "setPixelColor %d r %d g %d b %d order %d val %08x", ledIdx, r, g, b, _colourOrder, _pixels[ledIdx].getRaw());
#endif
}

void LEDPixels::setPixelColor(uint32_t ledIdx, uint32_t c)
{
    if (ledIdx >= _pixels.size())
        return;
    _pixels[ledIdx].fromRGB(c, _colourOrder);
}

void LEDPixels::setPixelColor(uint32_t ledIdx, const LEDPixel& pixel)
{
    if (ledIdx >= _pixels.size())
        return;
    _pixels[ledIdx] = pixel;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Show pixels
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool LEDPixels::show()
{
    // Show
    _ledStrip.showPixels(_pixels);

#ifdef DEBUG_LED_PIXEL_VALUES
    String outStr;
    for (auto& pix : _pixels)
    {
        outStr += String(pix.c1) + "," + String(pix.c2) + "," + String(pix.c3) + " | ";
    }
    LOG_I(MODULE_PREFIX, "show %d pixels %s", (int)_pixels.size(), outStr.c_str());
#endif

    return true;
}
