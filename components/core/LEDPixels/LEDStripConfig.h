/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// LedStripConfig.h - Configuration for LED strip
//
// Rob Dobson 2023-24
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Logger.h"
#include "RaftArduino.h"
#include "RaftUtils.h"
#include "RaftJsonPrefixed.h"
#include "RaftJson.h"
#include "esp_idf_version.h"

class LEDStripConfig
{
public:
    bool setup(const RaftJsonIF& config)
    {
        // Get data pin for LED strip
        ledDataPin = config.getLong("pin", -1);
        if (ledDataPin < 0)
        {
            LOG_W("LEDStripConfig", "setup invalid pixelDataPin");
            return false;
        }

        // Get number of pixels
        numPixels = config.getLong("num", DEFAULT_NUM_PIXELS);
        if (numPixels > MAX_NUM_PIXELS)
            numPixels = MAX_NUM_PIXELS;

        // RMT resolution
        rmtResolutionHz = config.getLong("rmtHz", rmtResolutionHz);

        // LED speed parameters
        bit0_0_ticks = config.getDouble("bit0_0Us", PIX_BIT_0_0_US_DEFAULT) * rmtResolutionHz / 1000000;
        bit0_1_ticks = config.getDouble("bit0_1Us", PIX_BIT_0_1_US_DEFAULT) * rmtResolutionHz / 1000000;
        bit1_0_ticks = config.getDouble("bit1_0Us", PIX_BIT_1_0_US_DEFAULT) * rmtResolutionHz / 1000000;
        bit1_1_ticks = config.getDouble("bit1_1Us", PIX_BIT_1_1_US_DEFAULT) * rmtResolutionHz / 1000000;
        reset_ticks = config.getDouble("resetUs", PIX_RESET_US_DEFAULT) * rmtResolutionHz / 1000000;

        // MSB first
        msbFirst = config.getBool("msbFirst", msbFirst);

        // Stop RMT peripheral after transmit
        stopAfterTx = config.getBool("stopAfterTx", false);

        // Power control
        powerPin = config.getLong("pwrPin", -1);
        powerOnLevel = config.getLong("pwrOnLvl", 1);
        powerPinGpioHold = config.getBool("pwrPinGpioHold", false);
        powerHoldIfInactive = config.getBool("pwrHoldIfInactive", false);
        powerOffIfBlank = config.getLong("offIfBlank", -1);
        powerOffAfterMs = config.getLong("offAfterMs", 0);

        return true;
    }

    String debugStr() const
    {
        String str = "numPix:" + String(numPixels) + " dPin:" + String(ledDataPin) + 
                    " pwrPin:" + String(powerPin) + " pwrOnLvl:" + String(powerOnLevel) +
                    " pwrGpioHold:" + String(powerPinGpioHold) +
                    " stopAftTx:" + String(stopAfterTx) +
                    " offIfBlnk:" + String(powerOffIfBlank) + " offAftMs:" + String(powerOffAfterMs) +
                    " rmtHz:" + String(rmtResolutionHz) +
                    " b0_0_tks:" + String(bit0_0_ticks) + " b0_1_tks:" + String(bit0_1_ticks) +
                    " b1_0_tks:" + String(bit1_0_ticks) + " b1_1_tks:" + String(bit1_1_ticks) +
                    " rst_tks:" + String(reset_ticks) + " msb1st:" + String(msbFirst);
        return str;
    }

    // Number of pixels in the strip
    uint16_t numPixels = 0;

    // Parameters
    int16_t ledDataPin = -1;

    // Power enablement
    int16_t powerPin = -1;
    bool powerOnLevel:1 = true;
    bool powerOffIfBlank:1 = false;
    bool powerPinGpioHold:1 = false;
    bool powerHoldIfInactive:1 = false;

    // Stop after Tx - deinit RMT peripheral after transmit
    bool stopAfterTx:1 = false;

    // Power off after ms
    uint32_t powerOffAfterMs = 0;

    // Pixel comms
    bool msbFirst:1 = true;
    uint32_t rmtResolutionHz = 10000000;
    static constexpr const double PIX_BIT_0_0_US_DEFAULT = 0.3;
    static constexpr const double PIX_BIT_0_1_US_DEFAULT = 0.9;
    static constexpr const double PIX_BIT_1_0_US_DEFAULT = 0.9; 
    static constexpr const double PIX_BIT_1_1_US_DEFAULT = 0.3;
    static constexpr const double PIX_RESET_US_DEFAULT = 50;
    uint16_t bit0_0_ticks = PIX_BIT_0_0_US_DEFAULT * rmtResolutionHz / 1000000;
    uint16_t bit0_1_ticks = PIX_BIT_0_1_US_DEFAULT * rmtResolutionHz / 1000000;
    uint16_t bit1_0_ticks = PIX_BIT_1_0_US_DEFAULT * rmtResolutionHz / 1000000;
    uint16_t bit1_1_ticks = PIX_BIT_1_1_US_DEFAULT * rmtResolutionHz / 1000000;
    uint16_t reset_ticks = PIX_RESET_US_DEFAULT * rmtResolutionHz / 1000000;

    // Default and max num pixels
    static const uint32_t MAX_NUM_PIXELS = 1000;    
    static const uint32_t DEFAULT_NUM_PIXELS = 60;
};
