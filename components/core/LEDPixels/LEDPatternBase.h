/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// LED Pattern Base Class
//
// Rob Dobson 2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include "RaftArduino.h"

class LEDPixels;
class NamedValueProvider;

// Base class for LED patterns
class LEDPatternBase
{
public:
    LEDPatternBase(NamedValueProvider* pNamedValueProvider, LEDPixels& pixels) :
        _pNamedValueProvider(pNamedValueProvider), _pixels(pixels)
    {
    }
    virtual ~LEDPatternBase()
    {
    }

    // Setup
    virtual void setup(const char* pParamsJson = nullptr) = 0;

    // Service
    virtual void loop() = 0;

protected:

    // Refresh rate
    uint32_t _refreshRateMs = 30;

    // Hardware state provider
    NamedValueProvider* _pNamedValueProvider = nullptr;

    // Pixels
    LEDPixels& _pixels;
};
