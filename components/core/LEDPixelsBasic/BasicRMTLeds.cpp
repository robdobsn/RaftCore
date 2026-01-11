// BasicRMTLeds.cpp
// Basic RMT-based LED strip driver using simple encoder
// Based on ESP-IDF simple encoder example
// Rob Dobson 2025

#include "BasicRMTLeds.h"
#include "Logger.h"
#include "RaftUtils.h"
#include "esp_check.h"
#include "driver/rmt_encoder.h"

static const char* MODULE_PREFIX = "BasicRMTLeds";

// #define DEBUG_BASIC_RMT_SHOW
// #define DEBUG_BASIC_RMT_INIT

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor and destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BasicRMTLeds::BasicRMTLeds()
    : _rmtChannel(nullptr)
    , _encoder(nullptr)
    , _numPixels(0)
    , _pin(-1)
    , _initialized(false)
    , _rmtResolutionHz(10000000)
    , _T0H_ticks(4)
    , _T0L_ticks(8)
    , _T1H_ticks(8)
    , _T1L_ticks(4)
    , _reset_ticks(1000)
    , _msbFirst(true)
    , _memBlockSymbols(64)
    , _transQueueDepth(1)
    , _minChunkSize(64)
{
    RaftAtomicBool_init(_txInProgress, false);
}

BasicRMTLeds::~BasicRMTLeds()
{
    if (_encoder) {
        rmt_del_encoder(_encoder);
        _encoder = nullptr;
    }
    if (_rmtChannel) {
        rmt_disable(_rmtChannel);
        rmt_del_channel(_rmtChannel);
        _rmtChannel = nullptr;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialize
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BasicRMTLeds::init(int pin, uint32_t numPixels,
                         uint32_t rmtResolutionHz,
                         uint16_t T0H_ticks,
                         uint16_t T0L_ticks,
                         uint16_t T1H_ticks,
                         uint16_t T1L_ticks,
                         uint16_t reset_ticks,
                         bool msbFirst,
                         uint16_t memBlockSymbols,
                         uint16_t transQueueDepth,
                         uint16_t minChunkSize)
{
    if (_initialized) {
        LOG_W(MODULE_PREFIX, "Already initialized");
        return false;
    }

    // Store configuration
    _pin = pin;
    _numPixels = numPixels;
    _rmtResolutionHz = rmtResolutionHz;
    _T0H_ticks = T0H_ticks;
    _T0L_ticks = T0L_ticks;
    _T1H_ticks = T1H_ticks;
    _T1L_ticks = T1L_ticks;
    _reset_ticks = reset_ticks;
    _msbFirst = msbFirst;
    _memBlockSymbols = memBlockSymbols;
    _transQueueDepth = transQueueDepth;
    _minChunkSize = minChunkSize;

    // Initialize pixel data buffer
    _pixelData.resize(numPixels * 3, 0);  // 3 bytes per pixel (RGB)

    // Setup timing symbols for encoder
    _ws2812_zero.level0 = 1;
    _ws2812_zero.duration0 = _T0H_ticks;
    _ws2812_zero.level1 = 0;
    _ws2812_zero.duration1 = _T0L_ticks;

    _ws2812_one.level0 = 1;
    _ws2812_one.duration0 = _T1H_ticks;
    _ws2812_one.level1 = 0;
    _ws2812_one.duration1 = _T1L_ticks;

    _ws2812_reset.level0 = 0;
    _ws2812_reset.duration0 = _reset_ticks / 2;
    _ws2812_reset.level1 = 0;
    _ws2812_reset.duration1 = _reset_ticks / 2;

    // Setup encoder context for callback
    _encoderContext.ws2812_zero = &_ws2812_zero;
    _encoderContext.ws2812_one = &_ws2812_one;
    _encoderContext.ws2812_reset = &_ws2812_reset;

    // Configure RMT TX channel
    rmt_tx_channel_config_t tx_chan_config = {};
    tx_chan_config.clk_src = RMT_CLK_SRC_DEFAULT;
    tx_chan_config.gpio_num = (gpio_num_t)pin;
    tx_chan_config.mem_block_symbols = _memBlockSymbols;
    tx_chan_config.resolution_hz = _rmtResolutionHz;
    tx_chan_config.trans_queue_depth = _transQueueDepth;
    tx_chan_config.flags.invert_out = false;
    tx_chan_config.flags.with_dma = false;

    esp_err_t ret = rmt_new_tx_channel(&tx_chan_config, &_rmtChannel);
    if (ret != ESP_OK) {
        LOG_E(MODULE_PREFIX, "Failed to create RMT TX channel: %s", esp_err_to_name(ret));
        return false;
    }

    // Create simple encoder
    ret = createEncoder();
    if (ret != ESP_OK) {
        LOG_E(MODULE_PREFIX, "Failed to create encoder: %s", esp_err_to_name(ret));
        rmt_del_channel(_rmtChannel);
        _rmtChannel = nullptr;
        return false;
    }

    // Setup TX completion callback
    rmt_tx_event_callbacks_t cbs = {
        .on_trans_done = rmtTxCompleteCBStatic,
    };
    ret = rmt_tx_register_event_callbacks(_rmtChannel, &cbs, this);
    if (ret != ESP_OK) {
        LOG_E(MODULE_PREFIX, "Failed to register callbacks: %s", esp_err_to_name(ret));
        rmt_del_encoder(_encoder);
        rmt_del_channel(_rmtChannel);
        _encoder = nullptr;
        _rmtChannel = nullptr;
        return false;
    }

    // Enable RMT TX channel
    ret = rmt_enable(_rmtChannel);
    if (ret != ESP_OK) {
        LOG_E(MODULE_PREFIX, "Failed to enable RMT: %s", esp_err_to_name(ret));
        rmt_del_encoder(_encoder);
        rmt_del_channel(_rmtChannel);
        _encoder = nullptr;
        _rmtChannel = nullptr;
        return false;
    }

    _initialized = true;

#ifdef DEBUG_BASIC_RMT_INIT
    LOG_I(MODULE_PREFIX, "init OK pin %d numPix %d memBlk %d transQ %d minChunk %d",
          pin, numPixels, _memBlockSymbols, _transQueueDepth, _minChunkSize);
#endif

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create simple encoder
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

esp_err_t BasicRMTLeds::createEncoder()
{
    rmt_simple_encoder_config_t simple_encoder_cfg = {
        .callback = encoder_callback,
        .arg = &_encoderContext,
        .min_chunk_size = _minChunkSize
    };
    return rmt_new_simple_encoder(&simple_encoder_cfg, &_encoder);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Encoder callback - called from ISR context to encode pixel data into RMT symbols
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

size_t IRAM_ATTR BasicRMTLeds::encoder_callback(const void *data, size_t data_size,
                                                size_t symbols_written, size_t symbols_free,
                                                rmt_symbol_word_t *symbols, bool *done, void *arg)
{
    // Get encoder context with timing symbols
    EncoderContext* ctx = (EncoderContext*)arg;

    // We need a minimum of 8 symbol spaces to encode a byte. We only
    // need one to encode a reset, but it's simpler to demand 8 spaces free.
    if (symbols_free < 8)
    {
        return 0;
    }

    // Calculate where in the data we are from the symbol position
    size_t data_pos = symbols_written / 8;
    const uint8_t *data_bytes = (const uint8_t *)data;

    if (data_pos < data_size)
    {
        // Encode a byte
        size_t symbol_pos = 0;
        for (int bitmask = 0x80; bitmask != 0; bitmask >>= 1)
        {
            if (data_bytes[data_pos] & bitmask)
            {
                symbols[symbol_pos++] = *(ctx->ws2812_one);
            }
            else
            {
                symbols[symbol_pos++] = *(ctx->ws2812_zero);
            }
        }
        // We wrote 8 symbols for this byte
        return symbol_pos;
    }
    else
    {
        // All bytes are encoded, send reset code
        symbols[0] = *(ctx->ws2812_reset);
        *done = true;  // Indicate end of transmission
        return 1;      // We wrote one symbol
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set pixel
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BasicRMTLeds::setPixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index >= _numPixels)
        return;

    _pixelData[index * 3 + 0] = r;
    _pixelData[index * 3 + 1] = g;
    _pixelData[index * 3 + 2] = b;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clear all pixels
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BasicRMTLeds::clear()
{
    std::fill(_pixelData.begin(), _pixelData.end(), 0);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Show pixels
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BasicRMTLeds::show()
{
    if (!_initialized)
        return false;

    // Check if transmission is already in progress
    if (RaftAtomicBool_get(_txInProgress))
    {
        // Skip this update - transmission already in progress
        return false;
    }

    // Transmit the data
    static const rmt_transmit_config_t tx_config = {
        .loop_count = 0,  // No transfer loop
        .flags = {
            .eot_level = 0  // Level 0 at end of transmission
        }
    };

    RaftAtomicBool_set(_txInProgress, true);
    esp_err_t err = rmt_transmit(_rmtChannel, _encoder, _pixelData.data(), _pixelData.size(), &tx_config);
    
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "rmt_transmit failed: %s", esp_err_to_name(err));
        RaftAtomicBool_set(_txInProgress, false);
        return false;
    }

#ifdef DEBUG_BASIC_RMT_SHOW
    LOG_I(MODULE_PREFIX, "show OK numBytes %d", _pixelData.size());
#endif

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Wait until show complete
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BasicRMTLeds::waitUntilShowComplete()
{
    if (!_initialized)
        return;

    // Max time to wait (base time + per-pixel time)
    static const uint32_t WAIT_RMT_BASE_US = 100;
    static const uint32_t WAIT_RMT_PER_PIX_US = 5;
    uint64_t maxWaitUs = WAIT_RMT_BASE_US + WAIT_RMT_PER_PIX_US * _numPixels;
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

bool IRAM_ATTR BasicRMTLeds::rmtTxCompleteCBStatic(rmt_channel_handle_t tx_chan, 
                                                   const rmt_tx_done_event_data_t *edata, 
                                                   void *user_ctx)
{
    BasicRMTLeds* pThis = (BasicRMTLeds*)user_ctx;
    if (!pThis)
        return false;
    
    return pThis->rmtTxCompleteCB(tx_chan, edata);
}

bool IRAM_ATTR BasicRMTLeds::rmtTxCompleteCB(rmt_channel_handle_t tx_chan, 
                                             const rmt_tx_done_event_data_t *edata)
{
    // Clear the transmission in progress flag
    RaftAtomicBool_set(_txInProgress, false);
    
    // False indicates no higher-priority task was woken
    return false;
}
