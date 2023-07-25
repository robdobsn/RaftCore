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
#include "LEDPixel.h"
#include "driver/rmt_tx.h"
// #include "ESP32RMTLedStripEncoder.h"
#include "LEDStripConfig.h"
#include "LEDStripEncoder.h"

class ESP32RMTLedStrip
{
public:
    ESP32RMTLedStrip();
    virtual ~ESP32RMTLedStrip();

    // Setup
    bool setup(const LEDStripConfig& ledStripConfig);

    // Show pixels
    void showPixels(std::vector<LEDPixel>& pixels);

private:

    // RMT channel
    rmt_channel_handle_t _rmtChannelHandle = nullptr;

    // Setup ok
    bool _isSetup = false;

    // // LED strip encoder
    // ESP32RMTLedStripEncoder _ledStripEncoder;
    rmt_encoder_handle_t _ledStripEncoderHandle = nullptr;

    // Pixel working buffer
    std::vector<uint8_t> _pixelBuffer;

};
