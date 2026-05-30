/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// LED Pattern Status
//
// Simple, self-contained "system alive" status indicator. Slowly breathes a
// configurable colour so the status segment shows the device is running without
// depending on any external named-value provider (battery / BLE / USB etc.).
//
// Params (all optional, supplied as JSON):
//   rateMs  - full breathe cycle time in ms (default 2000)
//   transPts- number of fade steps per half-cycle (default 20)
//   rgb     - breathe peak colour as #RRGGBB (default #004000, dim green)
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// #define DEBUG_LED_PATTERN_STATUS_SETUP

#include "RaftCore.h"
#include "LEDPatternBase.h"
#include "LEDPixHSV.h"

class LEDPatternStatus : public LEDPatternBase
{
public:
    LEDPatternStatus(NamedValueProvider* pNamedValueProvider, LEDPixelIF& pixels) :
        LEDPatternBase(pNamedValueProvider, pixels)
    {
    }
    virtual ~LEDPatternStatus()
    {
    }

    // Create function for factory
    static LEDPatternBase* create(NamedValueProvider* pNamedValueProvider, LEDPixelIF& pixels)
    {
        return new LEDPatternStatus(pNamedValueProvider, pixels);
    }

    // Setup
    virtual void setup(const char* pParamsJson = nullptr) override final
    {
        if (pParamsJson)
        {
            RaftJson paramsJson(pParamsJson, false);
            _breatheRateMs = paramsJson.getLong("rateMs", BREATHE_RATE_MS_DEFAULT);
            _numTransitionPoints = paramsJson.getLong("transPts", TRANSITION_POINTS_DEFAULT);
            String rgbStr = paramsJson.getString("rgb", "#004000");
            _hsvPeak = LEDPixHSV::fromRGB(Raft::getRGBFromHex(rgbStr).toUint());
        }

        // Number of transition points must be at least 1
        if (_numTransitionPoints < 1)
            _numTransitionPoints = 1;

        // Compute refresh rate from half-cycle time and number of steps
        _refreshRateMs = (_breatheRateMs / 2) / _numTransitionPoints;
        if (_refreshRateMs < MIN_REFRESH_RATE_MS)
            _refreshRateMs = MIN_REFRESH_RATE_MS;

#ifdef DEBUG_LED_PATTERN_STATUS_SETUP
        LOG_I(MODULE_PREFIX, "setup breatheRate %dms refreshRate %dms transPts %d peak #%06x",
                    _breatheRateMs, _refreshRateMs, _numTransitionPoints, _hsvPeak.toRGB());
#endif
    }

    // Loop
    virtual void loop() override final
    {
        // Check update time
        if (!Raft::isTimeout(millis(), _lastLoopMs, _refreshRateMs))
            return;
        _lastLoopMs = millis();

        // Fade factor 0..1 across the half-cycle
        double fadeFactor = ((double)_curTransitionCount) / _numTransitionPoints;

        // Interpolate value (brightness) between off and peak colour
        LEDPixHSV hsv = _hsvPeak;
        hsv.v = (uint32_t)(_hsvPeak.v * fadeFactor);

        // Apply to all pixels in the segment
        uint32_t numPix = _pixels.getNumPixels();
        for (uint32_t pixIdx = 0; pixIdx < numPix; pixIdx++)
            _pixels.setHSV(pixIdx, hsv.h, hsv.s, hsv.v);
        _pixels.show();

        // Bounce the transition count between 0 and numTransitionPoints
        _curTransitionCount += _countDirection;
        if (_curTransitionCount >= (int32_t)_numTransitionPoints)
        {
            _curTransitionCount = _numTransitionPoints;
            _countDirection = -1;
        }
        else if (_curTransitionCount <= 0)
        {
            _curTransitionCount = 0;
            _countDirection = 1;
        }
    }

private:
    // Consts
    static const uint32_t BREATHE_RATE_MS_DEFAULT = 2000;
    static const uint32_t TRANSITION_POINTS_DEFAULT = 20;
    static const uint32_t MIN_REFRESH_RATE_MS = 20;

    // Setup
    uint32_t _breatheRateMs = BREATHE_RATE_MS_DEFAULT;
    uint32_t _numTransitionPoints = TRANSITION_POINTS_DEFAULT;
    LEDPixHSV _hsvPeak = LEDPixHSV::fromRGB(0x004000);

    // State
    uint32_t _lastLoopMs = 0;
    int32_t _curTransitionCount = 0;
    int32_t _countDirection = 1;

    // Debug
    static constexpr const char *MODULE_PREFIX = "LEDPatStatus";
};
