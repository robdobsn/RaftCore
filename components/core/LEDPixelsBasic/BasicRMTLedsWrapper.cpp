// BasicRMTLedsWrapper.cpp
// Wrapper to make BasicRMTLeds compatible with RaftCore LEDPixels interface
// Rob Dobson 2025

#include "BasicRMTLedsWrapper.h"
#include "Logger.h"

static const char* MODULE_PREFIX = "BasicRMTWrap";

// #define DEBUG_BASIC_RMT_WRAPPER_SETUP
// #define DEBUG_BASIC_RMT_WRAPPER_SHOW

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor and destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BasicRMTLedsWrapper::BasicRMTLedsWrapper()
{
}

BasicRMTLedsWrapper::~BasicRMTLedsWrapper()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BasicRMTLedsWrapper::setup(const LEDStripConfig& ledStripConfig, uint32_t pixelIndexStartOffset)
{
    // Store config
    _pixelIdxStartOffset = pixelIndexStartOffset;
    _numPixels = ledStripConfig.numPixels;
    _blockingShow = ledStripConfig.blockingShow;

    // Initialize the BasicRMTLeds driver
    bool rslt = _basicRMT.init(
        ledStripConfig.ledDataPin,
        ledStripConfig.numPixels,
        ledStripConfig.rmtResolutionHz,
        ledStripConfig.T0H_ticks,
        ledStripConfig.T0L_ticks,
        ledStripConfig.T1H_ticks,
        ledStripConfig.T1L_ticks,
        ledStripConfig.reset_ticks,
        ledStripConfig.msbFirst,
        ledStripConfig.memBlockSymbols,
        ledStripConfig.transQueueDepth,
        ledStripConfig.minChunkSize
    );

    if (rslt)
    {
        _isSetup = true;
#ifdef DEBUG_BASIC_RMT_WRAPPER_SETUP
        LOG_I(MODULE_PREFIX, "setup OK pin %d numPix %d offset %d", 
              ledStripConfig.ledDataPin, ledStripConfig.numPixels, pixelIndexStartOffset);
#endif
    }
    else
    {
        LOG_E(MODULE_PREFIX, "setup FAILED");
    }

    return rslt;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loop
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BasicRMTLedsWrapper::loop()
{
    // Nothing to do in loop for BasicRMTLeds
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Show pixels
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BasicRMTLedsWrapper::showPixels(std::vector<LEDPixel>& pixels)
{
    if (!_isSetup)
        return false;

    // Check we have enough pixels
    if (pixels.size() < _pixelIdxStartOffset + _numPixels)
    {
        LOG_W(MODULE_PREFIX, "showPixels not enough pixels %d < %d", 
              pixels.size(), _pixelIdxStartOffset + _numPixels);
        return false;
    }

    // Check if transmission is already in progress
    // If so, skip this update to prevent buffer corruption
    if (_basicRMT.isBusy())
    {
        // Skip this update - transmission still in progress
        return false;
    }

    // Copy pixels to driver
    for (uint32_t i = 0; i < _numPixels; i++)
    {
        const LEDPixel& pixel = pixels[_pixelIdxStartOffset + i];
        _basicRMT.setPixel(i, pixel.r, pixel.g, pixel.b);
    }

    // Show the pixels
    bool rslt = _basicRMT.show();

#ifdef DEBUG_BASIC_RMT_WRAPPER_SHOW
    LOG_I(MODULE_PREFIX, "showPixels numPix %d offset %d rslt %s blocking %s",
          _numPixels, _pixelIdxStartOffset, rslt ? "OK" : "BUSY", _blockingShow ? "Y" : "N");
#endif

    // Wait if blocking mode
    if (rslt && _blockingShow)
    {
        _basicRMT.waitUntilShowComplete();
    }
    
    return rslt;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Wait for show to complete
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BasicRMTLedsWrapper::waitUntilShowComplete()
{
    if (!_isSetup)
        return;
    
    _basicRMT.waitUntilShowComplete();
}
