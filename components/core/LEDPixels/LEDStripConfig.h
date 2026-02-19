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
        double rmtTicksPerUs = rmtResolutionHz / 1000000.0;
        T0H_ticks = config.getDouble("T0H", config.getDouble("bit0_0Us", T0H_US_DEFAULT)) * rmtTicksPerUs;
        T1H_ticks = config.getDouble("T1H", config.getDouble("bit1_0Us", T1H_US_DEFAULT)) * rmtTicksPerUs;
        T0L_ticks = config.getDouble("T0L", config.getDouble("bit0_1Us", T0L_US_DEFAULT)) * rmtTicksPerUs;
        T1L_ticks = config.getDouble("T1L", config.getDouble("bit1_1Us", T1L_US_DEFAULT)) * rmtTicksPerUs;
        reset_ticks = config.getDouble("resetUs", RESET_US_DEFAULT) * rmtTicksPerUs;
        // MSB first
        msbFirst = config.getBool("msbFirst", msbFirst);

        // Stop RMT peripheral after transmit
        stopAfterTx = config.getBool("stopAfterTx", false);
        allowPowerDown = config.getBool("allowPowerDown", false);

        // Blocking show
        blockingShow = config.getBool("blockingShow", false);

        // Power control
        powerPin = config.getLong("pwrPin", -1);
        powerOnLevel = config.getLong("pwrOnLvl", 1);
        powerPinGpioHold = config.getBool("pwrPinGpioHold", false);
        powerHoldIfInactive = config.getBool("pwrHoldIfInactive", false);
        powerOffIfPowerControlledAllBlank = config.getLong("offIfBlank", -1);
        powerOffBlankExcludeFirstN = config.getLong("offBlankExcl1stN", 0);
        powerOffAfterMs = config.getLong("offAfterMs", 0);

        // Delay before deinit
        delayBeforeDeinitMs = config.getLong("beforeDeinitMs", 0);

        // RMT peripheral configuration
        memBlockSymbols = config.getLong("memBlockSymbols", 64);
        transQueueDepth = config.getLong("transQueueDepth", 1);
        minChunkSize = config.getLong("minChunkSize", 64);

        return true;
    }

    String debugStr() const
    {
        String str = "numPix:" + String(numPixels) + " dPin:" + String(ledDataPin) + 
                    " pwrPin:" + String(powerPin) + " pwrOnLvl:" + String(powerOnLevel) +
                    " pwrGpioHold:" + String(powerPinGpioHold) +
                    " stopAftTx:" + String(stopAfterTx) +
                    " blkShow:" + String(blockingShow) +
                    " offIfBlnk:" + String(powerOffIfPowerControlledAllBlank) +
                    " offExc1stN:" + String(powerOffBlankExcludeFirstN) +
                    " offAftMs:" + String(powerOffAfterMs) +
                    " befDeinitMs:" + String(delayBeforeDeinitMs) +
                    " rmtHz:" + String(rmtResolutionHz) +
                    " T0Hticks:" + String(T0H_ticks) + " T0Lticks:" + String(T0L_ticks) +
                    " T1Hticks:" + String(T1H_ticks) + " T1Lticks:" + String(T1L_ticks) +
                    " rst_tks:" + String(reset_ticks) + " msb1st:" + String(msbFirst) +
                    " memBlkSym:" + String(memBlockSymbols) +
                    " transQDepth:" + String(transQueueDepth) +
                    " minChunk:" + String(minChunkSize);
        return str;
    }

    // Number of pixels in the strip
    uint16_t numPixels = 0;

    // Parameters
    int16_t ledDataPin = -1;

    // Power pin
    int16_t powerPin = -1;

    // Flags
    bool powerOnLevel:1 = true;
    bool powerOffIfPowerControlledAllBlank:1 = false;
    bool powerPinGpioHold:1 = false;
    bool powerHoldIfInactive:1 = false;

    // Stop after Tx - deinit RMT peripheral after transmit
    bool stopAfterTx:1 = false;

    // Allow power down flag in RMT peripheral
    bool allowPowerDown:1 = false;

    // Send MSB first
    bool msbFirst:1 = true;

    // Block until show complete
    bool blockingShow:1 = false;

    // Off blank exclude indices before this value - these pixels are always powered
    uint16_t powerOffBlankExcludeFirstN = 0;

    // Power off after ms
    uint32_t powerOffAfterMs = 0;

    // Delay before deinit
    uint16_t delayBeforeDeinitMs = 0;

    // RMT peripheral configuration - for simple encoder implementations
    uint16_t memBlockSymbols = 64;      // RMT memory block size (larger = less flicker)
    uint16_t transQueueDepth = 1;       // Transaction queue depth (only 1 to prevent buffer corruption)
    uint16_t minChunkSize = 64;         // Min chunk size for simple encoder

    // Pixel comms - defaults are from WS2812B datasheet
    // https://cdn-shop.adafruit.com/datasheets/WS2812B.pdf
    static constexpr const double rmtResolutionMHz = 10.0;
    static constexpr const double rmtTicksPerUs = rmtResolutionMHz;
    static constexpr const double T0H_US_DEFAULT = 0.4;
    static constexpr const double T1H_US_DEFAULT = 0.8;
    static constexpr const double T0L_US_DEFAULT = 0.85; 
    static constexpr const double T1L_US_DEFAULT = 0.45;
    static constexpr const double RESET_US_DEFAULT = 100;
    uint32_t rmtResolutionHz = rmtResolutionMHz * 1000000;
    uint16_t T0H_ticks = T0H_US_DEFAULT * rmtTicksPerUs;
    uint16_t T1H_ticks = T1H_US_DEFAULT * rmtTicksPerUs;
    uint16_t T0L_ticks = T0L_US_DEFAULT * rmtTicksPerUs;
    uint16_t T1L_ticks = T1L_US_DEFAULT * rmtTicksPerUs;
    uint16_t reset_ticks = RESET_US_DEFAULT * rmtTicksPerUs;
    // Default and max num pixels
    static const uint32_t MAX_NUM_PIXELS = 2000;
    static const uint32_t DEFAULT_NUM_PIXELS = 60;
};
