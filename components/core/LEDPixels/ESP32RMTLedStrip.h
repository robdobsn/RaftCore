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

#define LEDSTRIP_USE_LEGACY_RMT ((ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)) && !defined(IDF_USE_LEGACY_RMT))

#ifdef LEDSTRIP_USE_LEGACY_RMT
#include <driver/rmt.h>
#else
#include "driver/rmt_tx.h"
#endif

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

#ifdef LEDSTRIP_USE_LEGACY_RMT
    // Bits per led cmd
    static const int WS2812_BITS_PER_LED_CMD = 24;

    // RMT channel
    rmt_channel_t _rmtChannel = RMT_CHANNEL_0;

    // Raw data working buffer
    std::vector<rmt_item32_t> _rawBuffer;

    // Driver installed
    bool _driverInstalled = false;

    // Helpers
    void setWaveBufferAtPos(uint32_t ledIdx, uint32_t ledValue);
    void fillWaveBuffer();
    bool writeAsync();

#else
    // RMT channel
    rmt_channel_handle_t _rmtChannelHandle = nullptr;

    // LED strip encoder
    rmt_encoder_handle_t _ledStripEncoderHandle = nullptr;
#endif

    // Pixel buffer
    uint32_t _numPixels = 0;
    std::vector<uint8_t> _pixelBuffer;
    void clear();

    // Setup ok
    bool _isSetup = false;

    // Helpers
    void releaseResources();
    void setPixelColor(uint32_t ledIdx, Raft::RGBValue rgbValue);
    esp_err_t showFromBuffer();
};
