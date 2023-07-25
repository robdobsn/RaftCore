/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ESP32RMTLedStrip.h
//
// Rob Dobson 2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <ESP32RMTLedStrip.h>
#include <Logger.h>
#include <RaftUtils.h>

// #define DEBUG_ESP32RMTLEDSTRIP_DETAIL

static const char* MODULE_PREFIX = "ESP32RMTLedStrip";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor and destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ESP32RMTLedStrip::ESP32RMTLedStrip()
{
}

ESP32RMTLedStrip::~ESP32RMTLedStrip()
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
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ESP32RMTLedStrip::setup(const LEDStripConfig& ledStripConfig)
{
    // Can't setup twice
    if (_isSetup)
    {
        LOG_E(MODULE_PREFIX, "setup already called");
        return true;
    }

    // Setup the RMT channel
    rmt_tx_channel_config_t rmtChannelConfig = {
        .gpio_num = (gpio_num_t)ledStripConfig.ledDataPin,              // LED strip data pin
        .clk_src = RMT_CLK_SRC_DEFAULT,                     // Default clock
        .resolution_hz = ledStripConfig.rmtResolutionHz,
        .mem_block_symbols = 64,                            // Increase to reduce flickering
        .trans_queue_depth = 4,                             // Number of transactions that can be pending in the background
        .flags = {
            .invert_out = false,                            // Invert output
            .with_dma = false,                              // No DMA
            .io_loop_back = false,                          // No loop
            .io_od_mode = false,                            // Not open drain
        }
    };

    // Create RMT TX channel
    esp_err_t err = rmt_new_tx_channel(&rmtChannelConfig, &_rmtChannelHandle);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "setup FAILED rmt_new_tx_channel error %d", err);
        return false;
    }

    // Setup LED strip encoder
    // _ledStripEncoder.setup(ledStripConfig);
    led_strip_encoder_config_t encoder_config = {
        .resolution = ledStripConfig.rmtResolutionHz,
        .bit0Duration0Us = ledStripConfig.bit0Duration0Us,
        .bit0Duration1Us = ledStripConfig.bit0Duration1Us,
        .bit1Duration0Us = ledStripConfig.bit1Duration0Us,
        .bit1Duration1Us = ledStripConfig.bit1Duration1Us,
        .resetDurationUs = ledStripConfig.resetDurationUs,
        .msbFirst = ledStripConfig.msbFirst,
    };
    err = rmt_new_led_strip_encoder(&encoder_config, &_ledStripEncoderHandle);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "setup FAILED rmt_new_led_strip_encoder error %d", err);
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

    // Log
    LOG_I(MODULE_PREFIX, "setup ok config %s rmtChannelHandle %p resolutionHz %d gpioNum %d encoderHandle %p", 
                ledStripConfig.toStr().c_str(), _rmtChannelHandle, ledStripConfig.rmtResolutionHz, 
                ledStripConfig.ledDataPin, _ledStripEncoderHandle);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Show pixels
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ESP32RMTLedStrip::showPixels(std::vector<LEDPixel>& pixels)
{
    // Can't show pixels if not setup
    if (!_isSetup)
        return;

    // Copy the buffer
    uint32_t numBytesToCopy = pixels.size() * sizeof(LEDPixel);
    if (_pixelBuffer.size() != numBytesToCopy)
        _pixelBuffer.resize(numBytesToCopy);
    memcpy(_pixelBuffer.data(), pixels.data(), numBytesToCopy);

    // Transmit the data
    static const rmt_transmit_config_t tx_config = {
        .loop_count = 0,        // no repetition
        .flags = {
            .eot_level = 0      // level = 0 at end
        }
    };
    esp_err_t err = rmt_transmit(_rmtChannelHandle, _ledStripEncoderHandle, _pixelBuffer.data(), numBytesToCopy, &tx_config);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "rmt_transmit failed: %d", err);
        _isSetup = false;
    }
#ifdef DEBUG_ESP32RMTLEDSTRIP_DETAIL
    else
    {
        String outStr;
        Raft::getHexStrFromBytes(_pixelBuffer.data(), numBytesToCopy, outStr);
        LOG_I(MODULE_PREFIX, "showPixels %d bytes %s", numBytesToCopy, outStr.c_str());
    }
#endif
}

