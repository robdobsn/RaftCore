/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// LED Pattern Base Class
//
// Rob Dobson 2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RaftArduino.h>
#include <stdint.h>
#include <NamedValueProvider.h>
#include <LEDPixel.h>
#include <ESP32RMTLedStrip.h>

// Base class for LED patterns
class LEDPatternBase
{
public:
    LEDPatternBase(NamedValueProvider* pNamedValueProvider, std::vector<LEDPixel>& pixels, ESP32RMTLedStrip& ledStrip) :
        _pNamedValueProvider(pNamedValueProvider), _pixels(pixels), _ledStrip(ledStrip)
    {
    }
    virtual ~LEDPatternBase()
    {
    }

    // Setup
    virtual void setup() = 0;

    // Service
    virtual void service() = 0;

protected:

    // Refresh rate
    uint32_t _refreshRateMs = 30;

    // Hardware state provider
    NamedValueProvider* _pNamedValueProvider = nullptr;

    // Pixels
    std::vector<LEDPixel>& _pixels;

    // LED strip
    ESP32RMTLedStrip& _ledStrip;
};

// Build function for factory
typedef LEDPatternBase* (*LEDPatternBuildFunc)(NamedValueProvider* pNamedValueProvider, std::vector<LEDPixel>& pixels, ESP32RMTLedStrip& ledStrip);

