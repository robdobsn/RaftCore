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
// #define DEBUG_LED_PIXEL_NONZERO
// #define DEBUG_LED_PIXELS_LOOP_SHOW
// #define DEBUG_LED_PATTERN_REGISTRY

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

    // Build strip start offsets for segment mapping
    std::vector<uint32_t> stripStartOffsets;
    stripStartOffsets.reserve(config.stripConfigs.size());
    uint32_t stripStart = 0;
    for (auto& stripCfg : config.stripConfigs)
    {
        stripStartOffsets.push_back(stripStart);
        stripStart += stripCfg.numPixels;
    }
    auto getStripIdxForPixel = [&](uint32_t pixelIdx) -> int32_t
    {
        for (uint32_t idx = 0; idx < config.stripConfigs.size(); idx++)
        {
            uint32_t start = stripStartOffsets[idx];
            uint32_t end = start + config.stripConfigs[idx].numPixels;
            if ((pixelIdx >= start) && (pixelIdx < end))
                return (int32_t)idx;
        }
        return -1;
    };

    // Check if any segments are specified, if not create a single segment from the entire pixel array
    if (config.segmentConfigs.size() == 0)
    {
        LEDSegmentConfig segCfg;
        segCfg.startOffset = 0;
        segCfg.numPixels = config.totalPixels;
        segCfg.name = "All";
        segCfg.pixelBrightnessFactor = config.globalBrightnessFactor;
        if (!config.stripConfigs.empty() && config.stripConfigs[0].colourOrderSpecified)
        {
            segCfg.colourOrder = config.stripConfigs[0].colourOrder;
            segCfg.bytesPerPixel = LEDPixel::getBytesPerPixel(segCfg.colourOrder);
        }
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
        for (auto& segCfg : config.segmentConfigs)
        {
            int32_t stripIdx = getStripIdxForPixel(segCfg.startOffset);
            uint32_t segEndOffset = segCfg.startOffset + (segCfg.numPixels > 0 ? segCfg.numPixels - 1 : 0);
            int32_t stripIdxEnd = getStripIdxForPixel(segEndOffset);
            if (stripIdx < 0)
            {
                LOG_W(MODULE_PREFIX, "segment %s start %d not in strip range", segCfg.name.c_str(), (int)segCfg.startOffset);
                continue;
            }
            if ((stripIdxEnd >= 0) && (stripIdxEnd != stripIdx))
            {
                LOG_W(MODULE_PREFIX, "segment %s spans strips %d..%d", segCfg.name.c_str(), (int)stripIdx, (int)stripIdxEnd);
            }
            if (config.stripConfigs[stripIdx].colourOrderSpecified)
            {
                if (segCfg.colourOrderSpecified && (segCfg.colourOrder != config.stripConfigs[stripIdx].colourOrder))
                {
                    LOG_W(MODULE_PREFIX, "segment %s colorOrder overridden by strip", segCfg.name.c_str());
                }
                segCfg.colourOrder = config.stripConfigs[stripIdx].colourOrder;
                segCfg.bytesPerPixel = LEDPixel::getBytesPerPixel(segCfg.colourOrder);
            }
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

#ifdef DEBUG_LED_PATTERN_REGISTRY
    LOG_I(MODULE_PREFIX, "addPattern %s total %d", patternName.c_str(), (int)_ledPatterns.size());
#endif
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
    outStr.reserve(_pixels.size() * 15);
#ifdef DEBUG_LED_PIXEL_NONZERO
    uint32_t nonZeroCount = 0;
    uint32_t firstNonZeroIdx = UINT32_MAX;
    LEDPixel firstNonZeroPixel;
#endif
    for (uint32_t idx = 0; idx < _pixels.size(); idx++)
    {
        const auto& pix = _pixels[idx];
        outStr += String(pix.c1) + "," + String(pix.c2) + "," + String(pix.c3);
        outStr += "," + String(pix.c4) + "," + String(pix.c5);
        outStr += " | ";
#ifdef DEBUG_LED_PIXEL_NONZERO
        if (pix.c1 || pix.c2 || pix.c3 || pix.c4 || pix.c5)
        {
            nonZeroCount++;
            if (firstNonZeroIdx == UINT32_MAX)
            {
                firstNonZeroIdx = idx;
                firstNonZeroPixel = pix;
            }
        }
#endif
    }
    LOG_I(MODULE_PREFIX, "show %d pixels %s", (int)_pixels.size(), outStr.c_str());
#ifdef DEBUG_LED_PIXEL_NONZERO
    if (nonZeroCount == 0)
    {
        LOG_I(MODULE_PREFIX, "show nonZero 0");
    }
    else
    {
        LOG_I(MODULE_PREFIX, "show nonZero %d firstIdx %d first %d,%d,%d,%d,%d", (int)nonZeroCount,
                (int)firstNonZeroIdx, firstNonZeroPixel.c1, firstNonZeroPixel.c2, firstNonZeroPixel.c3,
                firstNonZeroPixel.c4, firstNonZeroPixel.c5);
    }
#endif
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
