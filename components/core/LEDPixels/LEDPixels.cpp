/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// LEDPixels.cpp
//
// Rob Dobson 2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "LEDPixels.h"
#include "RaftUtils.h"
#include "RaftJsonIF.h"
#include "esp_idf_version.h"

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

bool LEDPixels::setup(const RaftJsonIF& config)
{
    // LED strip config
    LEDStripConfig ledStripConfig;
    if (!ledStripConfig.setup(config))
    {
        LOG_E(MODULE_PREFIX, "setup failed to get LED strip config");
        return false;
    }

    // Setup
    return setup(ledStripConfig);
}

bool LEDPixels::setup(LEDStripConfig& ledStripConfig)
{
    // Copy config
    _ledStripConfig = ledStripConfig;

    // Setup pixels
    _pixels.resize(_ledStripConfig.totalPixels);

    // Setup hardware
    _ledStrips.resize(_ledStripConfig.hwConfigs.size());
    bool rslt = false;
    for (uint32_t ledStripIdx = 0; ledStripIdx < _ledStripConfig.hwConfigs.size(); ledStripIdx++)
    {
        rslt = _ledStrips[ledStripIdx].setup(ledStripIdx, _ledStripConfig);
        if (!rslt)
            break;
    }

    // Set pattern
    setPattern(_ledStripConfig.initialPattern);

    // Log
    LOG_I(MODULE_PREFIX, "setup %s numStrips %d totalPixels %d", 
                rslt ? "OK" : "FAILED", (int)_ledStripConfig.hwConfigs.size(), (int)_ledStripConfig.totalPixels);
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
// Add a pattern
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LEDPixels::addPattern(const String& patternName, LEDPatternBuildFn buildFn)
{
    // Check for existing pattern with same name and remove if so
    for (auto it = _ledPatterns.begin(); it != _ledPatterns.end(); ++it)
    {
        if ((*it).name.equalsIgnoreCase(patternName))
        {
            _ledPatterns.erase(it);
            break;
        }
    }

    // Add pattern
    _ledPatterns.push_back(LEDPatternListItem(patternName, buildFn));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set a pattern
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LEDPixels::setPattern(const String& patternName, const char* pParamsJson)
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
            LEDPatternBase* pPattern = pattern.buildFn(_pNamedValueProvider, *this);
            if (pPattern)
            {
                // Set pattern
                _pCurrentPattern = pPattern;
                _currentPatternName = patternName;

                // Setup
                _pCurrentPattern->setup(pParamsJson);

                // Debug
                LOG_I(MODULE_PREFIX, "setPattern %s OK", patternName.c_str());
            }
            return;
        }
    }

    // Debug
    LOG_W(MODULE_PREFIX, "setPattern %s PATTERN NOT FOUND", patternName.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Write to an individual LED
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LEDPixels::setRGB(uint32_t ledIdx, uint32_t r, uint32_t g, uint32_t b, bool applyBrightness)
{
    // Check for mapping function & valid index
    if (_pixelMappingFn)
        ledIdx = _pixelMappingFn(ledIdx);
    if (ledIdx >= _pixels.size())
        return;

    // Set pixel
    _pixels[ledIdx].fromRGB(r, g, b, _ledStripConfig.colourOrder, applyBrightness ? _ledStripConfig.pixelBrightnessFactor : 1.0f);
#ifdef DEBUG_LED_PIXEL_VALUES
    LOG_I(MODULE_PREFIX, "setPixelColor %d r %d g %d b %d order %d val %08x", ledIdx, r, g, b, _colourOrder, _pixels[ledIdx].getRaw());
#endif
}

void LEDPixels::setRGB(uint32_t ledIdx, uint32_t c, bool applyBrightness)
{
    // Check for mapping function & valid index
    if (_pixelMappingFn)
        ledIdx = _pixelMappingFn(ledIdx);
    if (ledIdx >= _pixels.size())
        return;
    _pixels[ledIdx].fromRGB(c, _ledStripConfig.colourOrder, applyBrightness ? _ledStripConfig.pixelBrightnessFactor : 1.0f);
}

void LEDPixels::setRGB(uint32_t ledIdx, const LEDPixel& pixel)
{
    // Check for mapping function & valid index
    if (_pixelMappingFn)
        ledIdx = _pixelMappingFn(ledIdx);
    if (ledIdx >= _pixels.size())
        return;
    _pixels[ledIdx] = pixel;
}

void LEDPixels::setHSV(uint32_t ledIdx, uint32_t h, uint32_t s, uint32_t v)
{
    // Check for mapping function & valid index
    if (_pixelMappingFn)
        ledIdx = _pixelMappingFn(ledIdx);
    if (ledIdx >= _pixels.size())
        return;
    setRGB(ledIdx, LEDPixHSV::toRGB(h, s, v));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Show pixels
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool LEDPixels::show()
{
    // Show
    for (auto& ledStrip : _ledStrips)
    {
        ledStrip.showPixels(_pixels);
    }

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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Wait until show complete
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LEDPixels::waitUntilShowComplete()
{
    for (auto& ledStrip : _ledStrips)
    {
        ledStrip.waitUntilShowComplete();
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clear all pixels
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LEDPixels::clear(bool showAfterClear)
{
    for (auto& pix : _pixels)
    {
        pix.clear();
    }
    if (showAfterClear)
        show();
}
