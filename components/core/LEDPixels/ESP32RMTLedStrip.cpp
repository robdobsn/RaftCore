/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ESP32RMTLedStrip.h
//
// Rob Dobson 2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "ESP32RMTLedStrip.h"
#include "RaftUtils.h"

#define DEBUG_ESP32RMTLEDSTRIP_SETUP
// #define DEBUG_ESP32RMTLEDSTRIP_DETAIL

static const char* MODULE_PREFIX = "RMTLedStrip";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor and destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ESP32RMTLedStrip::ESP32RMTLedStrip()
{
}

ESP32RMTLedStrip::~ESP32RMTLedStrip()
{
    releaseResources();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ESP32RMTLedStrip::setup(uint32_t ledStripIdx, const LEDStripConfig& ledStripConfig)
{
    // Check index valid
    if (ledStripIdx >= ledStripConfig.hwConfigs.size())
    {
        LOG_E(MODULE_PREFIX, "setup FAILED ledStripIdx %d invalid numHWConfigs %d", ledStripIdx, ledStripConfig.hwConfigs.size());
        return false;
    }

    // Can't setup twice
    if (_isSetup)
    {
        LOG_E(MODULE_PREFIX, "setup already called");
        return true;
    }

    // Hardware config
    const LEDStripHwConfig& hwConfig = ledStripConfig.hwConfigs[ledStripIdx];
    _numPixels = hwConfig.numPixels;

    // Get the offset to the first pixel
    _pixelIdxStartOffset = ledStripConfig.getPixelStartOffset(ledStripIdx);

    // Setup the RMT channel
    rmt_tx_channel_config_t rmtChannelConfig = {
        .gpio_num = (gpio_num_t)hwConfig.ledDataPin,              // LED strip data pin
        .clk_src = RMT_CLK_SRC_DEFAULT,                     // Default clock
        .resolution_hz = hwConfig.rmtResolutionHz,
        .mem_block_symbols = 64,                            // Increase to reduce flickering
        .trans_queue_depth = 4,                             // Number of transactions that can be pending in the background
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
        .intr_priority = 0,                                  // Interrupt priority
        .flags = {
            .invert_out = false,                            // Invert output
            .with_dma = false,                              // No DMA
            .io_loop_back = false,                          // No loop
            .io_od_mode = false,                            // Not open drain
        },
#else
        .flags = {
            .invert_out = false,                            // Invert output
            .with_dma = false,                              // No DMA
            .io_loop_back = false,                          // No loop
            .io_od_mode = false,                            // Not open drain
        },
        .intr_priority = 0,                                  // Interrupt priority
#endif
    };

    // Create RMT TX channel
    esp_err_t err = rmt_new_tx_channel(&rmtChannelConfig, &_rmtChannelHandle);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "setup FAILED rmt_new_tx_channel error %d", err);
        return false;
    }

    led_strip_encoder_config_t encoder_config = {
        .resolution = hwConfig.rmtResolutionHz,
        .bit0Duration0Us = hwConfig.bit0Duration0Us,
        .bit0Duration1Us = hwConfig.bit0Duration1Us,
        .bit1Duration0Us = hwConfig.bit1Duration0Us,
        .bit1Duration1Us = hwConfig.bit1Duration1Us,
        .resetDurationUs = hwConfig.resetDurationUs,
        .msbFirst = hwConfig.msbFirst,
    };
    err = rmt_new_led_strip_encoder(&encoder_config, &_ledStripEncoderHandle);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "setup FAILED rmt_new_led_strip_encoder error %d", err);
        return false;
    }

    // Setup callback on completion
    rmt_tx_event_callbacks_t cbs = {
        .on_trans_done = rmtTxCompleteCBStatic,
    };
    err = rmt_tx_register_event_callbacks(_rmtChannelHandle, &cbs, this);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "setup FAILED rmt_tx_register_event_callbacks error %d", err);
        return false;
    }

    // Enable RMT TX channel
    err = rmt_enable(_rmtChannelHandle);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "setup FAILED rmt_enable error %d", err);
        return false;
    }

    // Setup ok
    _isSetup = true;
    _txInProgress = false;

#ifdef DEBUG_ESP32RMTLEDSTRIP_SETUP
    // Debug
    LOG_I(MODULE_PREFIX, "setup ok numPixels %d rmtChannelHandle %p encoderHandle %p hw %s", 
                _numPixels, _rmtChannelHandle, _ledStripEncoderHandle,
                hwConfig.debugStr().c_str());
#endif

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Show pixels
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ESP32RMTLedStrip::showPixels(std::vector<LEDPixel>& pixels)
{
    // Can't show pixels if not setup
    if (!_isSetup)
        return;

    // Get numnber of pixels to copy
    if (pixels.size() < _pixelIdxStartOffset)
        return;
    uint32_t numPixelsToCopy = pixels.size() - _pixelIdxStartOffset;
    if (numPixelsToCopy > _numPixels)
        numPixelsToCopy = _numPixels;

    // Copy the buffer
    uint32_t numBytesToCopy = numPixelsToCopy * sizeof(LEDPixel);
    if (_pixelBuffer.size() != numBytesToCopy)
        _pixelBuffer.resize(numBytesToCopy);
    memcpy(_pixelBuffer.data(), pixels.data() + (_pixelIdxStartOffset*sizeof(LEDPixel)), numBytesToCopy);

    // Transmit the data
    static const rmt_transmit_config_t tx_config = {
        .loop_count = 0,        // no repetition
        .flags = {
            .eot_level = 0      // level = 0 at end
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
            ,
            .queue_nonblocking = false
#endif
        }
    };
    esp_err_t err = rmt_transmit(_rmtChannelHandle, _ledStripEncoderHandle, _pixelBuffer.data(), numBytesToCopy, &tx_config);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "rmt_transmit failed: %d", err);
        _isSetup = false;
    }
    _txInProgress = true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Wait until show complete
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ESP32RMTLedStrip::waitUntilShowComplete()
{
    // Can't wait if not setup
    if (!_isSetup)
        return;

    // Check if already complete
    if (!_txInProgress)
        return;

    // We're not going to use rmt_tx_wait_all_done as it errors on timeout

    // Max time to wait
    uint64_t maxWaitUs = (WAIT_RMT_BASE_US + WAIT_RMT_PER_PIX_US * _pixelBuffer.size() + 1000)/1000;
    uint64_t startTimeUs = micros();
    while (_txInProgress && !Raft::isTimeout(micros(), startTimeUs, maxWaitUs))
    {
        if (maxWaitUs > 1000)
            delay(1);
        else
            delayMicroseconds(100);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Release resources
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ESP32RMTLedStrip::releaseResources()
{
    if (_rmtChannelHandle)
    {
        rmt_del_channel(_rmtChannelHandle);
        _rmtChannelHandle = nullptr;
    }
    if (_ledStripEncoderHandle)
    {
        rmt_del_encoder(_ledStripEncoderHandle);
        _ledStripEncoderHandle = nullptr;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// RMT TX complete callback
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ESP32RMTLedStrip::rmtTxCompleteCBStatic(rmt_channel_handle_t tx_chan, const rmt_tx_done_event_data_t *edata, void *user_ctx)
{
    // Get this pointer
    ESP32RMTLedStrip* pThis = (ESP32RMTLedStrip*)user_ctx;
    if (!pThis)
    {
        // False indicates a higher-priority task hasn't been woken up
        return false;
    }
    return pThis->rmtTxCompleteCB(tx_chan, edata);
}

bool ESP32RMTLedStrip::rmtTxCompleteCB(rmt_channel_handle_t tx_chan, const rmt_tx_done_event_data_t *edata)
{
    _txInProgress = false;
    // False indicates a higher-priority task hasn't been woken up
    return false;
}
