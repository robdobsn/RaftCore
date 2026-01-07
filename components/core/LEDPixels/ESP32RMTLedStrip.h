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
#include "RaftThreading.h"
#include "SpiramAwareAllocator.h"
#include "LEDPixel.h"
#include "LEDStripConfig.h"
#include "LEDStripEncoder.h"
#include "esp_idf_version.h"

#include "driver/rmt_tx.h"

class ESP32RMTLedStrip
{
public:
    ESP32RMTLedStrip();
    virtual ~ESP32RMTLedStrip();

    // Delete copy constructor and copy assignment
    ESP32RMTLedStrip(const ESP32RMTLedStrip&) = delete;
    ESP32RMTLedStrip& operator=(const ESP32RMTLedStrip&) = delete;

    // Setup
    bool setup(const LEDStripConfig& ledStripConfig, uint32_t pixelIndexStartOffset);

    // Loop
    void loop();

    // Show pixels
    // Returns false if show is skipped (busy or error), true if transmission started
    bool showPixels(std::vector<LEDPixel>& pixels);

    // Wait for show to complete
    void waitUntilShowComplete();

private:

    // LED strip config
    LEDStripConfig _ledStripConfig;

    // LED strip pixel indexing start offset into the overall pixel buffer
    uint32_t _pixelIdxStartOffset = 0;

    // RMT channel config
    rmt_tx_channel_config_t _rmtChannelConfig;

    // Encoder config
    led_strip_encoder_config_t _ledStripEncoderConfig;

    // RMT channel
    rmt_channel_handle_t _rmtChannelHandle = nullptr;

    // Setup/init/power flags
    bool _isSetup = false;
    bool _isInit = false;
    bool _isPowerOn = false;
    bool _powerOffAfterTxAsAllBlank = false;

    // Tx in progress (atomic for thread safety)
    RaftAtomicBool _txInProgress;

    // LED strip encoder
    rmt_encoder_handle_t _ledStripEncoderHandle = nullptr;

    // Last pixel transmit activity time
    static const uint32_t STOP_AFTER_TX_TIME_MS = 100;
    uint32_t _lastTxTimeMs = 0;

    // Mutex for state synchronization
    RaftMutex _stateMutex;

    // Pixel working buffer
    SpiramAwareUint8Vector _pixelBuffer;

    // Wait for RMT complete
    static const uint32_t WAIT_RMT_BASE_US = 100;
    static const uint32_t WAIT_RMT_PER_PIX_US = 5;

    // Helpers
    bool initRMTPeripheral();
    void deinitRMTPeripheral();
    static bool IRAM_ATTR rmtTxCompleteCBStatic(rmt_channel_handle_t tx_chan, const rmt_tx_done_event_data_t *edata, void *user_ctx);
    bool IRAM_ATTR rmtTxCompleteCB(rmt_channel_handle_t tx_chan, const rmt_tx_done_event_data_t *edata);
    void powerControl(bool enablePower);

    // Debug
    static constexpr const char* MODULE_PREFIX = "RMTLedSt";
};
