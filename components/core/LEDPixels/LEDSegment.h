/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// LEDSegment.h
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include <functional>
#include <stdint.h>
#include "LEDPixel.h"
#include "LEDPixelConfig.h"
#include "LEDPatternBase.h"
#include "LEDPixelIF.h"
#include "esp_idf_version.h"

// #define DEBUG_LED_SEGMENT_PATTERN_DURATION
// #define DEBUG_LED_SEGMENT_PATTERN_START_STOP

class RaftJsonIF;
class BusRequestResult;
class NamedValueProvider;

// Pixel mapping function
typedef std::function<uint32_t(uint32_t)> LEDPixelMappingFn;

class LEDSegment : public LEDPixelIF
{
public:
    LEDSegment()
    {
    }
    virtual ~LEDSegment()
    {
    }

    /// @brief Setup from JSON
    /// @param config Configuration JSON
    /// @return true if successful
    bool setup(const RaftJsonIF& config, std::vector<LEDPixel>* pLedPixels, const std::vector<LEDPatternBase::LEDPatternListItem>* pLedPatterns)
    {
        // LED segment config
        LEDSegmentConfig ledSegmentConfig;
        if (!ledSegmentConfig.setup(config))
        {
            LOG_E(MODULE_PREFIX, "setup failed to get LED segment config");
            return false;
        }

        // Setup
        return setup(ledSegmentConfig, pLedPixels, pLedPatterns);
    }

    /// @brief Setup from config object
    /// @param config Configuration object
    /// @return true if successful
    bool setup(LEDSegmentConfig& config, std::vector<LEDPixel>* pLedPixels, const std::vector<LEDPatternBase::LEDPatternListItem>* pLedPatterns)
    {
        // Store LED pixels and patterns
        _pLedPixels = pLedPixels;
        _pLedPatterns = pLedPatterns;

        // Copy config
        _ledSegmentConfig = config;

        // Set pattern
        if (config.initialPattern.length() > 0)
        {
            setPattern(config.initialPattern, config.initialPatternParamsJson.c_str());
        }
        else
        {
            clear();
        }

        return true;
    }

    /// @brief Loop
    /// @return true if LED show is required
    bool loop()
    {
        // Check if pattern active
        _showRequired = false;
        if (_pCurrentPattern)
        {
            if ((_patternDurationMs > 0) && Raft::isTimeout(millis(), _patternStartMs, _patternDurationMs))
            {
#ifdef DEBUG_LED_SEGMENT_PATTERN_DURATION
                LOG_I(MODULE_PREFIX, "loop pattern %s duration expired", _currentPatternName.c_str());
#endif
                setPattern("");
                return _showRequired;
            }

            // Service pattern
            _pCurrentPattern->loop();
        }
        return _showRequired;
    }

    /// @brief Check if stop requested
    /// @return true if stop requested
    bool isStopRequested()
    {
        return _stopRequested;
    }

    /// @brief Get name of segment
    /// @return Name of segment
    const String& getName() const
    {
        return _ledSegmentConfig.name;
    }

    /// @brief Set a named-value provider for pattern parameterisation
    /// @param pNamedValueProvider Pointer to the named value provider
    void setNamedValueProvider( NamedValueProvider* pNamedValueProvider)
    {
        _pNamedValueProvider = pNamedValueProvider;
    }

    // Set a mapping function to map from a pixel index to a physical LED index
    void setPixelMappingFn(LEDPixelMappingFn pixelMappingFn)
    {
        _pixelMappingFn = pixelMappingFn;
    }

    /// @brief Set pattern
    /// @param patternName Name of pattern
    /// @param pParamsJson Parameters for pattern
    void setPattern(const String& patternName, const char* pParamsJson=nullptr)
    {
        // Save current pattern name
        String curPatternName = _currentPatternName;

        // Stop existing pattern 
        stopPattern(true);

        // Find pattern
        if (_pLedPatterns)
        {
            for (auto& pattern : *_pLedPatterns)
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
#ifdef DEBUG_LED_SEGMENT_PATTERN_START_STOP
                        LOG_I(MODULE_PREFIX, "setPattern %s OK paramsJson %s duration %s", 
                                patternName.c_str(), pParamsJson ? pParamsJson : "NONE", 
                                _patternDurationMs == 0 ? "FOREVER" : (String(_patternDurationMs) + "ms").c_str());
#endif
                    }
                    return;
                }
            }
        }

        // Debug
#ifdef DEBUG_LED_SEGMENT_PATTERN_START_STOP
        LOG_I(MODULE_PREFIX, "setPattern %s", patternName.length() > 0 ? "PATTERN NOT FOUND" : ("cleared " + curPatternName).c_str());
#endif
    }

    /// @brief Stop pattern
    /// @param clearPixels Clear pixels after stopping
    void stopPattern(bool clearPixels)
    {
        // Stop no longer required
        _stopRequested = false;

        // Clear pattern
        if (_pCurrentPattern)
        {
            delete _pCurrentPattern;
            _pCurrentPattern = nullptr;
            _currentPatternName = "";
        }

        // Clear pixels
        if (clearPixels)
        {
            clear();
            _showRequired = true;
        }
    }

    /// @brief Set RGB value for a pixel
    /// @param ledIdx in the segment
    /// @param r red
    /// @param g green
    /// @param b blue
    /// @param applyBrightness scale values by brightness factor if true
    virtual void setRGB(uint32_t ledIdx, uint32_t r, uint32_t g, uint32_t b, bool applyBrightness=true) override final
    {
        // Get LED index
        uint32_t pixelIdx = getLEDIdx(ledIdx);

        // Set pixel
        if (_pLedPixels && (pixelIdx < _pLedPixels->size()))
            (*_pLedPixels)[pixelIdx].fromRGB(r, g, b, _ledSegmentConfig.colourOrder, applyBrightness ? _ledSegmentConfig.pixelBrightnessFactor : 1.0f);

#ifdef DEBUG_LED_PIXEL_VALUES
    LOG_I(MODULE_PREFIX, "setPixelColor %d r %d g %d b %d order %d val %08x", ledIdx, r, g, b, _ledPixelConfig.colourOrder, _pixels[ledIdx].getRaw());
#endif
    }

    /// @brief Set RGB value for a pixel
    /// @param ledIdx in the segment
    /// @param c RGB value
    /// @param applyBrightness scale values by brightness factor if true
    virtual void setRGB(uint32_t ledIdx, uint32_t c, bool applyBrightness=true) override final
    {
        // Get LED index
        uint32_t pixelIdx = getLEDIdx(ledIdx);

        // Set pixel
        if (_pLedPixels && (pixelIdx < _pLedPixels->size()))
        {
            (*_pLedPixels)[pixelIdx].fromRGB(c, _ledSegmentConfig.colourOrder, applyBrightness ? _ledSegmentConfig.pixelBrightnessFactor : 1.0f);
        }
    }

    /// @brief Set RGB value for a pixel
    /// @param ledIdx in the segment
    /// @param pixRGB pixel to copy
    virtual void setRGB(uint32_t ledIdx, const LEDPixel& pixRGB) override final
    {
        // Get LED index
        uint32_t pixelIdx = getLEDIdx(ledIdx);

        // Set pixel
        if (_pLedPixels && (pixelIdx < _pLedPixels->size()))
            (*_pLedPixels)[pixelIdx] = pixRGB;
    }

    /// @brief Set HSV value for a pixel
    /// @param ledIdx in the segment
    /// @param hsv HSV value
    virtual void setHSV(uint32_t ledIdx, LEDPixHSV& hsv) override final
    {
        // Set pixel
        setRGB(ledIdx, hsv.toRGB());
    }

    /// @brief Set HSV value for a pixel
    /// @param ledIdx in the segment
    /// @param h hue (0..359)
    /// @param s saturation (0..100)
    /// @param v value (0..100)
    virtual void setHSV(uint32_t ledIdx, uint32_t h, uint32_t s, uint32_t v) override final
    {
        // Set pixel
        setRGB(ledIdx, LEDPixHSV::toRGB(h, s, v));
    }

    /// @brief Clear all pixels in the segment
    virtual void clear() override final
    {
        // Iterate over pixels
        for (uint32_t ledIdx = 0; ledIdx < _ledSegmentConfig.numPixels; ledIdx++)
        {
            setRGB(ledIdx, 0, 0, 0, false);
        }
        _showRequired = true;
    }

    /// @brief Get number of pixels in the segment
    uint32_t getNumPixels() const override final
    {
        return _ledSegmentConfig.numPixels;
    }

    /// @brief Show pixels
    /// @return true if successful
    virtual bool show() override final
    {
        // Set flag indicating show required
        _showRequired = true;
        return true;
    }

    /// @brief Stop pattern generation - used to stop a pattern from within the pattern
    virtual void stop() override final
    {
        _stopRequested = true;
    }

private:

    // Config
    LEDSegmentConfig _ledSegmentConfig;
    
    // LEDPixels - pointer to object owned by LEDPixels
    std::vector<LEDPixel>* _pLedPixels = nullptr;

    // Interface to named values used in pattern generation
    NamedValueProvider* _pNamedValueProvider = nullptr;

    // Pixel mapping function
    LEDPixelMappingFn _pixelMappingFn = nullptr;

    // LED patterns
    const std::vector<LEDPatternBase::LEDPatternListItem>* _pLedPatterns = nullptr;

    // Current pattern
    LEDPatternBase* _pCurrentPattern = nullptr;
    String _currentPatternName;
    uint32_t _patternStartMs = 0;
    uint32_t _patternDurationMs = 0;

    // Show required
    bool _showRequired = false;

    // Stop requested
    bool _stopRequested = false;

    /// @brief Get index into LED pixels array owned by LEDPixels
    /// @param ledIdx Index into segment
    /// @return Index into LED pixels array
    uint32_t getLEDIdx(uint32_t ledIdx)
    {
        // Check for mapping function & valid index
        if (_pixelMappingFn)
            return _pixelMappingFn(ledIdx);
        return ledIdx + _ledSegmentConfig.startOffset;
    }

    // Debug
    static constexpr const char* MODULE_PREFIX = "LEDSegment";
};
