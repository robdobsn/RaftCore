/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ESP32RMTLedStrip.h
//
// Rob Dobson 2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include <stdint.h>
#include "SpiramAwareAllocator.h"
#include "LEDPixel.h"
#include "LEDStripConfig.h"
#include "LEDStripEncoder.h"
#include "esp_idf_version.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)

#include "driver/rmt_tx.h"

class ESP32RMTLedStrip
{
public:
    ESP32RMTLedStrip();
    virtual ~ESP32RMTLedStrip();

    // Setup
    bool setup(uint32_t ledStripIdx, const LEDStripConfig& ledStripConfig);

    // Show pixels
    void showPixels(std::vector<LEDPixel>& pixels);

    // Wait for show to complete
    void waitUntilShowComplete();

private:

    // RMT channel
    rmt_channel_handle_t _rmtChannelHandle = nullptr;

    // Setup ok
    bool _isSetup = false;

    // Tx in progress
    volatile bool _txInProgress = false;

    // LED strip pixel indexing start offset
    uint32_t _pixelIdxStartOffset = 0;
    
    // LED strip encoder
    rmt_encoder_handle_t _ledStripEncoderHandle = nullptr;

    // Num pixels in strip - this is recorded here rather than using _pixelBuffer.size()
    // because the buffer is lazily allocated and is multiplied by size of one pixel
    uint32_t _numPixels = 0;

    // Pixel working buffer
    std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> _pixelBuffer;

    // Wait for RMT complete
    static const uint32_t WAIT_RMT_BASE_US = 100;
    static const uint32_t WAIT_RMT_PER_PIX_US = 5;

    // Helpers
    void releaseResources();
    static bool rmtTxCompleteCBStatic(rmt_channel_handle_t tx_chan, const rmt_tx_done_event_data_t *edata, void *user_ctx);
    bool rmtTxCompleteCB(rmt_channel_handle_t tx_chan, const rmt_tx_done_event_data_t *edata);

    // Debug
    static constexpr const char* MODULE_PREFIX = "RMTLedSt";
};

#endif // ESP_IDF_VERSION