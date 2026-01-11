// SimpleRMTLeds.h
// Simple RMT-based LED strip driver for WS2811/WS2812 LEDs
// Based on working FastLED RMT implementation
// Rob Dobson 2024

#pragma once

#include <stdint.h>
#include <vector>
#include "driver/rmt_tx.h"
#include "esp_err.h"

class SimpleRMTLeds
{
public:
    SimpleRMTLeds();
    ~SimpleRMTLeds();

    // Initialize the LED strip
    // pin: GPIO pin number
    // numPixels: number of LEDs in the strip
    // rmtResolutionHz: RMT peripheral resolution in Hz
    // T0H_ticks, T0L_ticks, T1H_ticks, T1L_ticks: WS2812 timing in RMT ticks
    // reset_ticks: Reset pulse duration in RMT ticks
    // msbFirst: Send MSB first (true for WS2812)
    // Returns true on success
    bool init(int pin, uint32_t numPixels,
              uint32_t rmtResolutionHz = 10000000,
              uint16_t T0H_ticks = 4,
              uint16_t T0L_ticks = 8,
              uint16_t T1H_ticks = 8,
              uint16_t T1L_ticks = 4,
              uint16_t reset_ticks = 1000,
              bool msbFirst = true);

    // Set a single pixel color (RGB)
    void setPixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b);

    // Clear all pixels
    void clear();

    // Update the strip with current pixel data
    bool show();

    // Get number of pixels
    uint32_t getNumPixels() const { return _numPixels; }

private:
    // RMT channel handle
    rmt_channel_handle_t _rmtChannel;
    rmt_encoder_handle_t _encoder;
    
    // LED data
    std::vector<uint8_t> _pixelData;  // RGB data (3 bytes per pixel)
    uint32_t _numPixels;
    int _pin;
    bool _initialized;

    // Timing configuration
    uint32_t _rmtResolutionHz;
    uint16_t _T0H_ticks;
    uint16_t _T0L_ticks;
    uint16_t _T1H_ticks;
    uint16_t _T1L_ticks;
    uint16_t _reset_ticks;
    bool _msbFirst;

    // Helper to create encoder with current configuration
    esp_err_t createEncoder();
};