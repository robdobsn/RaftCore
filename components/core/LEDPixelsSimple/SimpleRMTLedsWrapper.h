// SimpleRMTLedsWrapper.h
// Wrapper to make SimpleRMTLeds compatible with RaftCore LEDPixels ESP32RMTLedStrip interface
// Rob Dobson 2024

#pragma once

#include <vector>
#include <stdint.h>
#include "SimpleRMTLeds.h"
#include "LEDPixel.h"
#include "LEDStripConfig.h"

class SimpleRMTLedsWrapper
{
public:
    SimpleRMTLedsWrapper();
    ~SimpleRMTLedsWrapper();

    // Setup
    bool setup(const LEDStripConfig& ledStripConfig, uint32_t pixelIndexStartOffset);

    // Loop
    void loop();

    // Show pixels
    // Returns false if show is skipped (busy or error), true if transmission started
    bool showPixels(std::vector<LEDPixel>& pixels);

    // Wait for show to complete
    void waitUntilShowComplete();

private:
    // SimpleRMTLeds driver instance
    SimpleRMTLeds _simpleRMT;
    
    // Setup flag
    bool _isSetup = false;
    
    // LED strip pixel indexing start offset into the overall pixel buffer
    uint32_t _pixelIdxStartOffset = 0;
    
    // Number of pixels in this strip
    uint32_t _numPixels = 0;
};