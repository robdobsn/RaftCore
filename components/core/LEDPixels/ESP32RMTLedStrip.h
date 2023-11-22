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

private:

    // RMT channel
    rmt_channel_handle_t _rmtChannelHandle = nullptr;

    // Setup ok
    bool _isSetup = false;

    // // LED strip encoder
    // ESP32RMTLedStripEncoder _ledStripEncoder;
    rmt_encoder_handle_t _ledStripEncoderHandle = nullptr;

    // Pixel working buffer
    std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> _pixelBuffer;

    // Helpers
    void releaseResources();
    void setPixelColor(uint32_t ledIdx, Raft::RGBValue rgbValue);
    esp_err_t showFromBuffer();    
};
