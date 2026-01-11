// BasicRMTLeds.h
// Basic RMT-based LED strip driver using simple encoder
// Based on ESP-IDF simple encoder example
// Rob Dobson 2025

#pragma once

#include <stdint.h>
#include <vector>
#include "driver/rmt_tx.h"
#include "esp_err.h"
#include "RaftThreading.h"

class BasicRMTLeds
{
public:
    BasicRMTLeds();
    ~BasicRMTLeds();

    // Initialize the LED strip
    // pin: GPIO pin number
    // numPixels: number of LEDs in the strip
    // rmtResolutionHz: RMT peripheral resolution in Hz
    // T0H_ticks, T0L_ticks, T1H_ticks, T1L_ticks: WS2812 timing in RMT ticks
    // reset_ticks: Reset pulse duration in RMT ticks
    // msbFirst: Send MSB first (true for WS2812)
    // memBlockSymbols: RMT memory block size
    // transQueueDepth: Transaction queue depth
    // minChunkSize: Minimum chunk size for encoder callback
    // Returns true on success
    bool init(int pin, uint32_t numPixels,
              uint32_t rmtResolutionHz = 10000000,
              uint16_t T0H_ticks = 4,
              uint16_t T0L_ticks = 8,
              uint16_t T1H_ticks = 8,
              uint16_t T1L_ticks = 4,
              uint16_t reset_ticks = 1000,
              bool msbFirst = true,
              uint16_t memBlockSymbols = 64,
              uint16_t transQueueDepth = 1,
              uint16_t minChunkSize = 64);

    // Set a single pixel color (RGB)
    void setPixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b);

    // Clear all pixels
    void clear();

    // Update the strip with current pixel data
    bool show();

    // Wait for show to complete
    void waitUntilShowComplete();

    // Get number of pixels
    uint32_t getNumPixels() const { return _numPixels; }

    // Check if transmission is in progress
    bool isBusy() const { return RaftAtomicBool_get(_txInProgress); }

private:
    // RMT channel and encoder handles
    rmt_channel_handle_t _rmtChannel;
    rmt_encoder_handle_t _encoder;
    
    // LED data
    std::vector<uint8_t> _pixelData;  // RGB data (3 bytes per pixel)
    uint32_t _numPixels;
    int _pin;
    bool _initialized;

    // Timing configuration stored for encoder callback
    uint32_t _rmtResolutionHz;
    uint16_t _T0H_ticks;
    uint16_t _T0L_ticks;
    uint16_t _T1H_ticks;
    uint16_t _T1L_ticks;
    uint16_t _reset_ticks;
    bool _msbFirst;

    // RMT configuration
    uint16_t _memBlockSymbols;
    uint16_t _transQueueDepth;
    uint16_t _minChunkSize;

    // Timing symbols for encoder
    rmt_symbol_word_t _ws2812_zero;
    rmt_symbol_word_t _ws2812_one;
    rmt_symbol_word_t _ws2812_reset;

    // Transmission tracking
    RaftAtomicBool _txInProgress;

    // Helper to create simple encoder
    esp_err_t createEncoder();

    // Encoder callback (must be static for C API)
    static size_t IRAM_ATTR encoder_callback(const void *data, size_t data_size,
                                             size_t symbols_written, size_t symbols_free,
                                             rmt_symbol_word_t *symbols, bool *done, void *arg);

    // Callback context structure
    struct EncoderContext {
        const rmt_symbol_word_t* ws2812_zero;
        const rmt_symbol_word_t* ws2812_one;
        const rmt_symbol_word_t* ws2812_reset;
    };
    EncoderContext _encoderContext;

    // TX completion callback
    static bool IRAM_ATTR rmtTxCompleteCBStatic(rmt_channel_handle_t tx_chan, 
                                                const rmt_tx_done_event_data_t *edata, 
                                                void *user_ctx);
    bool IRAM_ATTR rmtTxCompleteCB(rmt_channel_handle_t tx_chan, 
                                   const rmt_tx_done_event_data_t *edata);
};
