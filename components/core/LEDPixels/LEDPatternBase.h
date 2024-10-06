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

class LEDPixelIF;
class NamedValueProvider;
class LEDPatternBase;

// Create function for LED pattern factory
typedef LEDPatternBase* (*LEDPatternCreateFn)(NamedValueProvider* pNamedValueProvider, LEDPixelIF& pixels);

// Base class for LED patterns
class LEDPatternBase
{
public:
    LEDPatternBase(NamedValueProvider* pNamedValueProvider, LEDPixelIF& pixels) :
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

    // LED pattern list item
    struct LEDPatternListItem
    {
        String name;
        LEDPatternCreateFn createFn;
    };

protected:

    // Refresh rate
    uint32_t _refreshRateMs = 30;

    // Hardware state provider
    NamedValueProvider* _pNamedValueProvider = nullptr;

    // Pixel interface
    LEDPixelIF& _pixels;
};
