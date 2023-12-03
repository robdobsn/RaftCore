/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ESP32RMTLedStrip.h
//
// Rob Dobson 2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <vector>
#include <SpiramAwareAllocator.h>
#include "LEDPixel.h"
#include "LEDStripConfig.h"
#include "LEDStripEncoder.h"
#include "driver/rmt_tx.h"

class ESP32RMTLedStrip
{
public:
    ESP32RMTLedStrip();
    virtual ~ESP32RMTLedStrip();

    // Setup
    bool setup(const LEDStripConfig& ledStripConfig);

    // Show pixels
    void showPixels(std::vector<LEDPixel>& pixels);

    // Wait for show to complete
    void waitUntilShowComplete();

private:

    // RMT channel
    rmt_channel_handle_t _rmtChannelHandle = nullptr;

    // Setup ok
    bool _isSetup = false;

    // // LED strip encoder
    rmt_encoder_handle_t _ledStripEncoderHandle = nullptr;

    // Pixel working buffer
    std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> _pixelBuffer;

    // Wait for RMT complete
    static const uint32_t WAIT_RMT_BASE_US = 100;
    static const uint32_t WAIT_RMT_PER_PIX_US = 5;

    // Helpers
    void releaseResources();
};
