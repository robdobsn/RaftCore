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
// #define DEBUG_LED_PIXELS_LOOP_SHOW

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor and destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

LEDPixels::LEDPixels()
{
}

LEDPixels::~LEDPixels()
{
    // Clean up LED strip drivers
    for (auto* ledStrip : _ledStripDrivers)
    {
        delete ledStrip;
    }
    _ledStripDrivers.clear();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool LEDPixels::setup(const RaftJsonIF& config)
{
    // LED pixels config
    LEDPixelConfig ledPixelConfig;
    if (!ledPixelConfig.setup(config))
    {
        LOG_E(MODULE_PREFIX, "setup failed to get LED pixel config");
        return false;
    }

    // Setup
    return setup(ledPixelConfig);
}

bool LEDPixels::setup(LEDPixelConfig& config)
{
    // Setup pixels
    _pixels.resize(config.totalPixels);

    // Setup hardware drivers
    _ledStripDrivers.reserve(config.stripConfigs.size());
    bool rslt = false;
    uint32_t pixelCount = 0;
    for (uint32_t ledStripIdx = 0; ledStripIdx < config.stripConfigs.size(); ledStripIdx++)
    {
        ESP32RMTLedStrip* ledStrip = new ESP32RMTLedStrip();
        _ledStripDrivers.push_back(ledStrip);
        rslt = ledStrip->setup(config.stripConfigs[ledStripIdx], pixelCount);
        if (!rslt)
            break;
        pixelCount += config.stripConfigs[ledStripIdx].numPixels;
    }

    // Check if any segments are specified, if not create a single segment from the entire pixel array
    if (config.segmentConfigs.size() == 0)
    {
        LEDSegmentConfig segCfg;
        segCfg.startOffset = 0;
        segCfg.numPixels = config.totalPixels;
        segCfg.name = "All";
        segCfg.pixelBrightnessFactor = config.globalBrightnessFactor;
        _segments.resize(1);
        _segments[0].setNamedValueProvider(_pDefaultNamedValueProvider, true);
        _segments[0].setup(segCfg, &_pixels, &_ledPatterns);
    }
    else
    {
        // Setup segments
        _segments.resize(config.segmentConfigs.size());

        // If there is exactly one segment and it does not specify a numPixels, assume it covers all pixels
        if ((config.segmentConfigs.size() == 1) && (config.segmentConfigs[0].numPixels == 0))
        {
            config.segmentConfigs[0].numPixels = config.totalPixels;
        }
        for (uint32_t segIdx = 0; segIdx < _segments.size(); segIdx++)
        {
            _segments[segIdx].setNamedValueProvider(_pDefaultNamedValueProvider, true);
            _segments[segIdx].setup(config.segmentConfigs[segIdx], &_pixels, &_ledPatterns);
        }
    }

    // Log
    LOG_I(MODULE_PREFIX, "setup %s numStrips %d numSegments %d totalPixels %d", 
                rslt ? "OK" : "FAILED",
                (int)_ledStripDrivers.size(), 
                (int)_segments.size(),
                config.totalPixels);
    return rslt;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LEDPixels::loop()
{
    // Loop over LED strips
    for (auto* ledStrip : _ledStripDrivers)
    {
        ledStrip->loop();
    }

    // Loop over segments
    for (auto& segment : _segments)
    {
        if (segment.loop())
        {
            show();
#ifdef DEBUG_LED_PIXELS_LOOP_SHOW
            LOG_I(MODULE_PREFIX, "loop segment %s show", segment.getName().c_str());
#endif
        }
        if (segment.isStopRequested())
        {
#ifdef DEBUG_LED_PIXELS_LOOP_SHOW
            LOG_I(MODULE_PREFIX, "loop segment %s stop requested", segment.getName().c_str());
#endif
            segment.stopPattern(true);
            show();
        }
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
    _ledPatterns.push_back(LEDPatternBase::LEDPatternListItem(patternName, createFn));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get pattern names
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LEDPixels::getPatternNames(std::vector<String>& patternNames) const
{
    for (auto& pattern : _ledPatterns)
    {
        patternNames.push_back(pattern.name);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// @brief Get segment index from name
// @param segmentName Name of segment
// @return Index of segment or -1 if not found
int32_t LEDPixels::getSegmentIdx(const String& segmentName) const
{
    for (uint32_t segIdx = 0; segIdx < _segments.size(); segIdx++)
    {
        if (_segments[segIdx].getName().equalsIgnoreCase(segmentName))
            return segIdx;
    }
    return -1;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Show pixels
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool LEDPixels::show()
{
    // Show on all strips, tracking if any fail
    bool allSucceeded = true;
    uint32_t ledStripIdx = 0;
    for (auto* ledStrip : _ledStripDrivers)
    {
        // Pre-show callback if specified
        if (_showCB)
            _showCB(ledStripIdx, false, _pixels);

        // Show - continue even if this one fails
        if (!ledStrip->showPixels(_pixels))
            allSucceeded = false;

        // Post-show callback if specified
        if (_showCB)
            _showCB(ledStripIdx, true, _pixels);

        // Next LED strip
        ledStripIdx++;
    }

#ifdef DEBUG_LED_PIXEL_VALUES
    String outStr;
    for (auto& pix : _pixels)
    {
        outStr += String(pix.c1) + "," + String(pix.c2) + "," + String(pix.c3) + " | ";
    }
    LOG_I(MODULE_PREFIX, "show %d pixels %s", (int)_pixels.size(), outStr.c_str());
#endif

    return allSucceeded;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Wait until show complete
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LEDPixels::waitUntilShowComplete()
{
    for (auto* ledStrip : _ledStripDrivers)
    {
        ledStrip->waitUntilShowComplete();
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
