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
#include "esp_idf_version.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)

#define DEBUG_ESP32RMTLEDSTRIP_SETUP
// #define DEBUG_ESP32RMTLEDSTRIP_DETAIL

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor and destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ESP32RMTLedStrip::ESP32RMTLedStrip()
{
}

ESP32RMTLedStrip::~ESP32RMTLedStrip()
{
    deinitRMTPeripheral();
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

    // If already setup then release resources first
    if (_isSetup)
    {
        deinitRMTPeripheral();
        _isSetup = false;
    }

    // Hardware config
    const LEDStripHwConfig& hwConfig = ledStripConfig.hwConfigs[ledStripIdx];
    _numPixels = hwConfig.numPixels;

    // Get the offset to the first pixel
    _pixelIdxStartOffset = ledStripConfig.getPixelStartOffset(ledStripIdx);

    // Get stop after transmit flag
    _stopAfterTx = ledStripConfig.stopAfterTx;

    // Setup the RMT channel
    _rmtChannelConfig = {
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
        .intr_priority = 0,                                  // Interrupt priority
        .flags = {
            .invert_out = false,                            // Invert output
            .with_dma = false,                              // No DMA
            .io_loop_back = false,                          // No loop
            .io_od_mode = false,                            // Not open drain
        },
#endif
    };

    _ledStripEncoderConfig = {
        .resolution = hwConfig.rmtResolutionHz,
        .bit0Duration0Us = hwConfig.bit0Duration0Us,
        .bit0Duration1Us = hwConfig.bit0Duration1Us,
        .bit1Duration0Us = hwConfig.bit1Duration0Us,
        .bit1Duration1Us = hwConfig.bit1Duration1Us,
        .resetDurationUs = hwConfig.resetDurationUs,
        .msbFirst = hwConfig.msbFirst,
    };

    // Now setup
    _isSetup = true;

#ifdef DEBUG_ESP32RMTLEDSTRIP_SETUP
    // Debug
    LOG_I(MODULE_PREFIX, "setup ok numPixels %d hw %s", 
                _numPixels, hwConfig.debugStr().c_str());
#endif

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Loop
void ESP32RMTLedStrip::loop()
{
    if (_isSetup && _isInit && _stopAfterTx && !_txInProgress && Raft::isTimeout(millis(), _lastTxTimeMs, STOP_AFTER_TX_TIME_MS))
    {
        // LOG_I(MODULE_PREFIX, "loop deinitRMTPeripheral");
        deinitRMTPeripheral();
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Initialization of RMT TX peripheral
bool ESP32RMTLedStrip::initRMTPeripheral()
{
    // Check if busy
    if (_isInit || _txInProgress)
    {
        LOG_E(MODULE_PREFIX, "initRMTPeripheral FAILED already init or busy");
        return false;
    }

    // Create RMT TX channel
    esp_err_t err = rmt_new_tx_channel(&_rmtChannelConfig, &_rmtChannelHandle);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "initRMTPeripheral FAILED rmt_new_tx_channel error %d", err);
        return false;
    }

    err = rmt_new_led_strip_encoder(&_ledStripEncoderConfig, &_ledStripEncoderHandle);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "initRMTPeripheral FAILED rmt_new_led_strip_encoder error %d", err);
        return false;
    }

    // Setup callback on completion
    _lastTxTimeMs = millis();
    _txInProgress = false;
    rmt_tx_event_callbacks_t cbs = {
        .on_trans_done = rmtTxCompleteCBStatic,
    };
    err = rmt_tx_register_event_callbacks(_rmtChannelHandle, &cbs, this);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "initRMTPeripheral FAILED rmt_tx_register_event_callbacks error %d", err);
        return false;
    }

    // Enable RMT TX channel
    err = rmt_enable(_rmtChannelHandle);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "initRMTPeripheral FAILED rmt_enable error %d", err);
        return false;
    }

    // Init ok
    _isInit = true;
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// De-init RMT peripheral
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ESP32RMTLedStrip::deinitRMTPeripheral()
{
    if (_rmtChannelHandle)
    {
        rmt_disable(_rmtChannelHandle);
        rmt_del_channel(_rmtChannelHandle);
        _rmtChannelHandle = nullptr;
    }
    if (_ledStripEncoderHandle)
    {
        rmt_del_encoder(_ledStripEncoderHandle);
        _ledStripEncoderHandle = nullptr;
    }
    _isInit = false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Show pixels
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ESP32RMTLedStrip::showPixels(std::vector<LEDPixel>& pixels)
{
    // Can't show pixels if not setup
    if (!_isSetup)
        return;

    // Init if not already done
    if (!_isInit)
        initRMTPeripheral();

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
    _txInProgress = true;
    _lastTxTimeMs = millis();
    esp_err_t err = rmt_transmit(_rmtChannelHandle, _ledStripEncoderHandle, _pixelBuffer.data(), numBytesToCopy, &tx_config);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "rmt_transmit failed: %d", err);
        _txInProgress = false;
        deinitRMTPeripheral();
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Wait until show complete
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ESP32RMTLedStrip::waitUntilShowComplete()
{
    // Can't wait if not setup
    if (!_isSetup || !_isInit)
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
    // No longer transmitting
    _lastTxTimeMs = millis();
    _txInProgress = false;

    // False indicates a higher-priority task hasn't been woken up
    return false;
}

#endif // ESP_IDF_VERSION
