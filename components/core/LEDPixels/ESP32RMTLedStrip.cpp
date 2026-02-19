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
#include "driver/gpio.h"

// #define DEBUG_ESP32RMTLEDSTRIP_SETUP
// #define DEBUG_ESP32RMTLEDSTRIP_SEND
// #define DEBUG_ESP32RMTLEDSTRIP_DEINIT_AFTER_TX
// #define DEBUG_ESP32RMTLEDSTRIP_INIT_RMT
// #define DEBUG_ESP32RMTLEDSTRIP_POWER_CTRL

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor and destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ESP32RMTLedStrip::ESP32RMTLedStrip()
{
    RaftMutex_init(_stateMutex);
    RaftAtomicBool_init(_txInProgress, false);
}

ESP32RMTLedStrip::~ESP32RMTLedStrip()
{
    deinitRMTPeripheral();

    // Destroy mutex
    RaftMutex_destroy(_stateMutex);

    // Clear power pin if used
    if (_ledStripConfig.powerPin >= 0)
    {
        if (_ledStripConfig.powerPinGpioHold)
            gpio_hold_dis((gpio_num_t)_ledStripConfig.powerPin);
        pinMode(_ledStripConfig.powerPin, INPUT);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ESP32RMTLedStrip::setup(const LEDStripConfig& config, uint32_t pixelIndexStartOffset)
{
    // If already setup then release resources first
    if (_isSetup)
    {
        deinitRMTPeripheral();
        _isSetup = false;
    }

    // Store the config
    _ledStripConfig = config;
    _pixelIdxStartOffset = pixelIndexStartOffset;

    // Setup power control
    if (_ledStripConfig.powerPin >= 0)
    {
        if (_ledStripConfig.powerPinGpioHold)
            gpio_hold_dis((gpio_num_t)_ledStripConfig.powerPin);
        pinMode(_ledStripConfig.powerPin, OUTPUT);
        powerControl(false);
    }

    // Setup the RMT channel
    _rmtChannelConfig = {
        .gpio_num = (gpio_num_t)config.ledDataPin,          // LED strip data pin
        .clk_src = RMT_CLK_SRC_DEFAULT,                     // Default clock
        .resolution_hz = config.rmtResolutionHz,
        .mem_block_symbols = config.memBlockSymbols,        // Increase to reduce flickering
        .trans_queue_depth = config.transQueueDepth,        // Generally we only want one transaction at a time 
                                                            // (queueing without data buffering could cause corruption)
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
        .intr_priority = 0,                                  // Interrupt priority
        .flags = {
            .invert_out = false,                            // Invert output
            .with_dma = false,                              // No DMA
            .io_loop_back = false,                          // No loop
            .io_od_mode = false,                            // Not open drain
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
            .allow_pd = config.allowPowerDown,              // Allow power down (save context to RAM)
#endif
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
            .init_level = false,                            // Initial level 
#endif
        },
#else
        .intr_priority = 0,                                 // Interrupt priority
        .flags = {
            .invert_out = false,                            // Invert output
            .with_dma = false,                              // No DMA
            .io_loop_back = false,                          // No loop
            .io_od_mode = false,                            // Not open drain
        },
#endif
    };

    _ledStripEncoderConfig = {
        .resolution = config.rmtResolutionHz,
        .T0H_ticks = config.T0H_ticks,
        .T0L_ticks = config.T0L_ticks,
        .T1H_ticks = config.T1H_ticks,
        .T1L_ticks = config.T1L_ticks,
        .reset_ticks = config.reset_ticks, 
        .msbFirst = config.msbFirst,
    };

    // Now setup
    _isSetup = true;

#ifdef DEBUG_ESP32RMTLEDSTRIP_SETUP
    // Debug
    LOG_I(MODULE_PREFIX, "setup ok %s", config.debugStr().c_str());
#endif

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Loop
void ESP32RMTLedStrip::loop()
{
    // Check for de-init
    if (_isSetup && _isInit && _ledStripConfig.stopAfterTx && !RaftAtomicBool_get(_txInProgress) && Raft::isTimeout(millis(), _lastTxTimeMs, STOP_AFTER_TX_TIME_MS))
    {
#ifdef DEBUG_ESP32RMTLEDSTRIP_DEINIT_AFTER_TX
        LOG_I(MODULE_PREFIX, "loop deinitRMTPeripheral");
#endif
        deinitRMTPeripheral();
    }

    // Check for power down conditions
    if (_isSetup && _isPowerOn)
    {
        if (_powerOffAfterTxAsAllBlank)
        {
            _powerOffAfterTxAsAllBlank = false;
#ifdef DEBUG_ESP32RMTLEDSTRIP_POWER_CTRL
        LOG_I(MODULE_PREFIX, "loop power off as all blank");
#endif
            powerControl(false);
        }
        else if ((_ledStripConfig.powerOffAfterMs > 0) && Raft::isTimeout(millis(), _lastTxTimeMs, _ledStripConfig.powerOffAfterMs))
        {
#ifdef DEBUG_ESP32RMTLEDSTRIP_POWER_CTRL
            LOG_I(MODULE_PREFIX, "loop power off after %dms", _ledStripConfig.powerOffAfterMs);
#endif
            powerControl(false);
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Initialization of RMT TX peripheral
bool ESP32RMTLedStrip::initRMTPeripheral()
{
    // Check if busy (with mutex protection)
    RaftMutex_lock(_stateMutex, RAFT_MUTEX_WAIT_FOREVER);
    bool isBusy = _isInit || RaftAtomicBool_get(_txInProgress);
    RaftMutex_unlock(_stateMutex);
    
    if (isBusy)
    {
        LOG_E(MODULE_PREFIX, "initRMT FAIL reinit|busy");
        return false;
    }

    // Create RMT TX channel
    esp_err_t err = rmt_new_tx_channel(&_rmtChannelConfig, &_rmtChannelHandle);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "initRMT FAIL newCh %d", err);
        return false;
    }

    err = rmt_new_led_strip_encoder(&_ledStripEncoderConfig, &_ledStripEncoderHandle);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "initRMT FAIL newEncod %d", err);
        return false;
    }

    // Setup callback on completion
    _lastTxTimeMs = millis();
    RaftAtomicBool_set(_txInProgress, false);
    rmt_tx_event_callbacks_t cbs = {
        .on_trans_done = rmtTxCompleteCBStatic,
    };
    err = rmt_tx_register_event_callbacks(_rmtChannelHandle, &cbs, this);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "initRMT FAIL regCB %d", err);
        return false;
    }

    // Enable RMT TX channel
    err = rmt_enable(_rmtChannelHandle);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "initRMT FAIL rmtEn %d", err);
        return false;
    }

    // Debug
#ifdef DEBUG_ESP32RMTLEDSTRIP_INIT_RMT
    LOG_I(MODULE_PREFIX, "initRMTPeripheral OK");
#endif

    // Init ok
    _isInit = true;
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// De-init RMT peripheral
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ESP32RMTLedStrip::deinitRMTPeripheral()
{
    RaftMutex_lock(_stateMutex, RAFT_MUTEX_WAIT_FOREVER);
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
    RaftMutex_unlock(_stateMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Show pixels
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ESP32RMTLedStrip::showPixels(std::vector<LEDPixel>& pixels)
{
    // Can't show pixels if not setup
    if (!_isSetup)
        return false;
    
    // Get number of pixels to copy
    if (pixels.size() < _pixelIdxStartOffset)
        return false;
    uint32_t numPixelsToCopy = _ledStripConfig.numPixels;
    if (numPixelsToCopy > pixels.size() - _pixelIdxStartOffset)
        numPixelsToCopy = pixels.size() - _pixelIdxStartOffset;

    // Check if transmission is already in progress
    // If so, skip this update to prevent buffer corruption
    if (RaftAtomicBool_get(_txInProgress))
    {
        // Skip this update - transmission still in progress
        return false;
    }

    // Copy the buffer
    uint32_t numBytesToCopy = numPixelsToCopy * sizeof(LEDPixel);
    if (_pixelBuffer.size() != numBytesToCopy)
        _pixelBuffer.resize(numBytesToCopy);
    memcpy(_pixelBuffer.data(), pixels.data() + _pixelIdxStartOffset, numBytesToCopy);

    // Check for power off if all power controlled pixels are blank
    if (_ledStripConfig.powerOffIfPowerControlledAllBlank && (_pixelBuffer.size() > 0))
    {
        _powerOffAfterTxAsAllBlank = true;
        for (uint32_t i = _ledStripConfig.powerOffBlankExcludeFirstN; i < _pixelBuffer.size(); i++)
        {
            if (_pixelBuffer[i] != 0)
            {
                _powerOffAfterTxAsAllBlank = false;
                break;
            }
        }
    }

    // Init if not already done
    if (!_isInit)
        initRMTPeripheral();

    // Power on the strip if required
    if (_ledStripConfig.powerPin >= 0 && !_isPowerOn)
    {
        powerControl(true);
    }

    // Transmit the data
    static const rmt_transmit_config_t tx_config = {
        .loop_count = 0,        // no repetition
        .flags = {
            .eot_level = 0      // level = 0 at end
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
            ,
            .queue_nonblocking = true  // fail fast if queue full rather than blocking
#endif
        }
    };
    RaftAtomicBool_set(_txInProgress, true);
    _lastTxTimeMs = millis();
    esp_err_t err = rmt_transmit(_rmtChannelHandle, _ledStripEncoderHandle, _pixelBuffer.data(), numBytesToCopy, &tx_config);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "rmt_transmit failed: %d", err);
        RaftAtomicBool_set(_txInProgress, false);
        deinitRMTPeripheral();
    }

    // if tx ok ... Check for blocking show
    else if (_ledStripConfig.blockingShow)
    {
        // Block until complete
        waitUntilShowComplete();

        // Not sure why a delay here is necessary but it seems to be
        delay(_ledStripConfig.delayBeforeDeinitMs);

        // Check for power off if all power controlled pixels are blank
        if (_powerOffAfterTxAsAllBlank)
        {
            _powerOffAfterTxAsAllBlank = false;
#ifdef DEBUG_ESP32RMTLEDSTRIP_POWER_CTRL
        LOG_I(MODULE_PREFIX, "loop power off as all blank");
#endif
            powerControl(false);
        }

        // Check if peripheral should be deinitialized
        if (_ledStripConfig.stopAfterTx)
        {
#ifdef DEBUG_ESP32RMTLEDSTRIP_DEINIT_AFTER_TX
            LOG_I(MODULE_PREFIX, "showPixels deinitRMTPeripheral");
#endif
            deinitRMTPeripheral();
        }
    }

#ifdef DEBUG_ESP32RMTLEDSTRIP_SEND
    bool allZeroes = true;
    for (uint32_t i = 0; i < _pixelBuffer.size(); i++)
    {
        if (_pixelBuffer[i] != 0)
        {
            allZeroes = false;
            break;
        }
    }
    String outStr;
    static const int MAX_PIXELS_TO_SHOW = 10;
    for (uint32_t i = 0; i < _pixelBuffer.size(); i+=3)
    {
        outStr += String(_pixelBuffer[i]) + "," + String(_pixelBuffer[i+1]) + "," + String(_pixelBuffer[i+2]) + " | ";
        if (i >= MAX_PIXELS_TO_SHOW * 3)
        {
            outStr += "...";
            break;
        }
    }
    LOG_I(MODULE_PREFIX, "showPixels offset %d numPix %d numBytes %d rslt %s allBlank %s RMTHdl %p blocking %s powerOffAllBlank %s stopAfterTx %s vals %s", 
            _pixelIdxStartOffset,
            numPixelsToCopy,
            numBytesToCopy,
            err == ESP_OK ? "OK" : "FAILED", 
            allZeroes ? "YES" : "NO",
            _rmtChannelHandle,
            _ledStripConfig.blockingShow ? "Y" : "N",
            _ledStripConfig.powerOffIfPowerControlledAllBlank ? "Y" : "N",
            _ledStripConfig.stopAfterTx ? "Y" : "N",
            outStr.c_str());
#endif
    
    return (err == ESP_OK);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Wait until show complete
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ESP32RMTLedStrip::waitUntilShowComplete()
{
    // Can't wait if not setup
    if (!_isSetup || !_isInit)
        return;

    // We're not going to use rmt_tx_wait_all_done as it errors on timeout

    // Max time to wait
    uint64_t maxWaitUs = (WAIT_RMT_BASE_US + WAIT_RMT_PER_PIX_US * _pixelBuffer.size() + 1000)/1000;
    uint64_t startTimeUs = micros();
    while (RaftAtomicBool_get(_txInProgress) && !Raft::isTimeout(micros(), startTimeUs, maxWaitUs))
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
    // No longer transmitting (atomic update, no mutex needed in ISR)
    RaftAtomicBool_set(_txInProgress, false);
    _lastTxTimeMs = millis();

    // False indicates a higher-priority task hasn't been woken up
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// @brief Power control
// @param enable Enable / disable
void ESP32RMTLedStrip::powerControl(bool enable)
{
    // Check if power control is enabled
    if (_ledStripConfig.powerPin < 0)
        return;

    // Update power on flag
    _isPowerOn = enable;

    // Check for gpio hold on power pin
    if (_ledStripConfig.powerPinGpioHold)
    {
        // Disable hold
        gpio_hold_dis( (gpio_num_t)_ledStripConfig.powerPin);
    }

    // Set the power level
    bool powerLevel = enable ? _ledStripConfig.powerOnLevel : !_ledStripConfig.powerOnLevel;
    digitalWrite(_ledStripConfig.powerPin, powerLevel);

    // Check for gpio hold on power pin and either always hold or power enabled
    bool applyHold = _ledStripConfig.powerPinGpioHold && (_ledStripConfig.powerHoldIfInactive || enable);
    if (applyHold)
    {
        // Enable hold if power enabled (this is so that the power pin is held high when going into light sleep)
        gpio_hold_en( (gpio_num_t)_ledStripConfig.powerPin);
    }

#ifdef DEBUG_ESP32RMTLEDSTRIP_POWER_CTRL
    LOG_I(MODULE_PREFIX, "powerControl %s pin %d gpioHold req %s ifInactive %s applied %s levelWritten %d", 
                enable ? "ON" : "OFF", 
                _ledStripConfig.powerPin, 
                _ledStripConfig.powerPinGpioHold ? "Y" : "N",
                _ledStripConfig.powerHoldIfInactive ? "Y" : "N",
                applyHold ? "Y" : "N",
                powerLevel);
#endif
}
