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
#include "LEDPixelConfig.h"
#include "LEDSegment.h"
#include "ESP32RMTLedStrip.h"
#include "LEDPatternBase.h"
#include "esp_idf_version.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)

class RaftJsonIF;
class BusRequestResult;
class NamedValueProvider;

typedef std::function<void(uint32_t segmentIdx, bool postShow)> LEDPixelsShowCB;

class LEDPixels
{
public:
    LEDPixels();
    virtual ~LEDPixels();

    /// @brief Setup
    /// @param config Configuration JSON
    bool setup(const RaftJsonIF& config);

    /// @brief Setup
    /// @param config Configuration object
    bool setup(LEDPixelConfig& config);

    /// @brief Loop
    void loop();

    /// @brief Add a pattern to the factory
    /// @param patternName Name of pattern
    /// @param createFn Create function
    void addPattern(const String& patternName, LEDPatternCreateFn createFn);

    /// @brief Get pattern names
    /// @param patternNames Vector of pattern names
    void getPatternNames(std::vector<String>& patternNames) const;

    /// @brief Get segment index from name
    /// @param segmentName Name of segment
    /// @return Index of segment or -1 if not found
    int32_t getSegmentIdx(const String& segmentName) const;

    /// @brief Set a mapping function to map from a pixel index to a physical LED index
    /// @param segmentIdx Index of segment
    /// @param pixelMappingFn Mapping function
    void setPixelMappingFn(uint32_t segmentIdx, LEDPixelMappingFn pixelMappingFn)
    {
        if (segmentIdx >= _segments.size())
            return;
        _segments[segmentIdx].setPixelMappingFn(pixelMappingFn);
    }

    /// @brief Set a named-value provider for pattern parameterisation
    /// @param segmentIdx Index of segment
    /// @param pNamedValueProvider Pointer to the named value provider
    void setNamedValueProvider(uint32_t segmentIdx, NamedValueProvider* pNamedValueProvider)
    {
        if (segmentIdx >= _segments.size())
            return;
        _segments[segmentIdx].setNamedValueProvider(pNamedValueProvider);
    }

    /// @brief Set pattern in a segment
    /// @param segmentIdx Index of segment
    /// @param patternName Name of pattern
    /// @param pParamsJson Parameters for pattern
    void setPattern(uint32_t segmentIdx, const String& patternName, const char* pParamsJson=nullptr)
    {
        if (segmentIdx >= _segments.size())
            return;
        _segments[segmentIdx].setPattern(patternName, pParamsJson);
    }

    /// @brief Stop all patterns
    /// @param clearPixels Clear pixels after stopping
    void stopPatterns(bool clearPixels)
    {
        for (uint32_t segmentIdx = 0; segmentIdx < _segments.size(); segmentIdx++)
            _segments[segmentIdx].stopPattern(clearPixels);
    }

    /// @brief Stop pattern in a segment
    /// @param segmentIdx Index of segment
    /// @param clearPixels Clear pixels after stopping
    void stopPattern(uint32_t segmentIdx, bool clearPixels)
    {
        if (segmentIdx >= _segments.size())
            return;
        _segments[segmentIdx].stopPattern(clearPixels);
    }

    /// @brief Set RGB value for a pixel
    /// @param ledIdx in the segment
    /// @param r red
    /// @param g green
    /// @param b blue
    /// @param applyBrightness scale values by brightness factor if true
    void setRGB(uint32_t segmentIdx, uint32_t ledIdx, uint32_t r, uint32_t g, uint32_t b, bool applyBrightness=true)
    {
        if (segmentIdx >= _segments.size())
            return;
        _segments[segmentIdx].setRGB(ledIdx, r, g, b, applyBrightness);
    }

    /// @brief Set RGB value for a pixel
    /// @param ledIdx in the segment
    /// @param c RGB value
    /// @param applyBrightness scale values by brightness factor if true
    void setRGB(uint32_t segmentIdx, uint32_t ledIdx, uint32_t c, bool applyBrightness=true)
    {
        if (segmentIdx >= _segments.size())
            return;
        _segments[segmentIdx].setRGB(ledIdx, c, applyBrightness);
    }

    /// @brief Set RGB value for a pixel
    /// @param ledIdx in the segment
    /// @param pixRGB pixel to copy
    void setRGB(uint32_t segmentIdx, uint32_t ledIdx, const LEDPixel& pixRGB)
    {
        if (segmentIdx >= _segments.size())
            return;
        _segments[segmentIdx].setRGB(ledIdx, pixRGB);
    }

    /// @brief Set HSV value for a pixel
    /// @param ledIdx in the segment
    /// @param h hue (0..359)
    /// @param s saturation (0..100)
    /// @param v value (0..100)
    void setHSV(uint32_t segmentIdx, uint32_t ledIdx, uint32_t h, uint32_t s, uint32_t v)
    {
        if (segmentIdx >= _segments.size())
            return;
        _segments[segmentIdx].setHSV(ledIdx, h, s, v);
    }

    /// @brief Clear all pixels
    /// @param showAfterClear Show after clear
    void clear(bool showAfterClear=false);

    /// @brief Clear all pixels in a segment
    /// @param segmentIdx Index of segment
    /// @param showAfterClear Show after clear
    void clear(uint32_t segmentIdx, bool showAfterClear=false)
    {
        if (segmentIdx >= _segments.size())
            return;
        _segments[segmentIdx].clear();

        // Show
        if (showAfterClear)
            show();
    }

    /// @brief Get number of pixels in total
    /// @return Number of pixels in total
    uint32_t getNumPixels() const
    {
        return _pixels.size();
    }

    /// @brief Get number of pixels in a segment
    /// @param segmentIdx Index of segment
    /// @return Number of pixels in the segment
    uint32_t getNumPixels(uint32_t segmentIdx) const
    {
        if (segmentIdx >= _segments.size())
            return 0;
        return _segments[segmentIdx].getNumPixels();
    }

    /// @brief Show pixels
    /// @return true if successful
    bool show();

    /// @brief Check if ready to show
    /// @return true if ready to show
    bool canShow()
    {
        return true;
    }

    /// @brief Wait until show complete
    void waitUntilShowComplete();

    /// @brief Set show callback
    /// @param showCB Show callback
    void setShowCB(LEDPixelsShowCB showCB)
    {
        _showCB = showCB;
    }

private:

    // Pixels
    std::vector<LEDPixel> _pixels;

    // Segments
    std::vector<LEDSegment> _segments;

    // LED strip drivers
    std::vector<ESP32RMTLedStrip> _ledStripDrivers;

    // LED patterns
    std::vector<LEDPatternBase::LEDPatternListItem> _ledPatterns;

    // Show callback
    LEDPixelsShowCB _showCB = nullptr;
    
    // Debug
    static constexpr const char* MODULE_PREFIX = "LEDPix";
};

#endif