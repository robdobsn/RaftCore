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

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)

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

    // Turn power on if required
    if (_ledStripConfig.powerPin >= 0)
    {
        pinMode(_ledStripConfig.powerPin, OUTPUT);
        digitalWrite(_ledStripConfig.powerPin, _ledStripConfig.powerOnLevel);
    }

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
    if (_ledStripConfig.initialPattern.length() > 0)
    {
        setPattern(_ledStripConfig.initialPattern, ("{\"forMs\":" + String(_ledStripConfig.initialPatternMs) + "}").c_str());
    }

    // Log
    LOG_I(MODULE_PREFIX, "setup %s numStrips %d totalPixels %d", 
                rslt ? "OK" : "FAILED", (int)_ledStripConfig.hwConfigs.size(), (int)_ledStripConfig.totalPixels);
    return rslt;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LEDPixels::loop()
{
    // Check if pattern active
    if (_pCurrentPattern)
    {
        if ((_patternDurationMs > 0) && Raft::isTimeout(millis(), _patternStartMs, _patternDurationMs))
        {
            setPattern("");
            return;
        }

        // Service pattern
        _pCurrentPattern->loop();
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add a pattern
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LEDPixels::addPattern(const String& patternName, LEDPatternCreateFn createFn)
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
    _ledPatterns.push_back(LEDPatternListItem(patternName, createFn));
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
            // Create pattern
            LEDPatternBase* pPattern = pattern.createFn(_pNamedValueProvider, *this);
            if (pPattern)
            {
                // Set pattern
                _pCurrentPattern = pPattern;
                _currentPatternName = patternName;

                // Setup
                _pCurrentPattern->setup(pParamsJson);

                // Check if pattern duration is specified
                if (pParamsJson)
                {
                    RaftJson paramsJson(pParamsJson);
                    _patternDurationMs = paramsJson.getInt("forMs", 0);
                }
                _patternStartMs = millis();

                // Debug
                LOG_I(MODULE_PREFIX, "setPattern %s OK paramsJson %s durationMs %d", 
                        patternName.c_str(), pParamsJson ? pParamsJson : "NONE", _patternDurationMs);
            }
            return;
        }
    }

    // Clear LEDs
    clear(true);

    // Debug
    LOG_I(MODULE_PREFIX, "setPattern %s", patternName.length() > 0 ? "PATTERN NOT FOUND" : "pattern cleared");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get pattern names
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LEDPixels::getPatternNames(std::vector<String>& patternNames)
{
    for (auto& pattern : _ledPatterns)
    {
        patternNames.push_back(pattern.name);
    }
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
    LOG_I(MODULE_PREFIX, "setPixelColor %d r %d g %d b %d order %d val %08x", ledIdx, r, g, b, _ledStripConfig.colourOrder, _pixels[ledIdx].getRaw());
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
    // Clear pixels
    for (auto& pix : _pixels)
    {
        pix.clear();
    }
    if (showAfterClear)
        show();
}

#endif // ESP_IDF_VERSION