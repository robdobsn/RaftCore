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
        .flags = {
            .invert_out = false,                            // Invert output
            .with_dma = false,                              // No DMA
            .io_loop_back = false,                          // No loop
            .io_od_mode = false,                            // Not open drain
        },
        .intr_priority = 0,                                  // Interrupt priority
    };

    // Create RMT TX channel
    esp_err_t err = rmt_new_tx_channel(&rmtChannelConfig, &_rmtChannelHandle);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "setup FAILED rmt_new_tx_channel error %d", err);
        return false;
    }

    // Setup LED strip encoder
    // _ledStripEncoder.setup(hwConfig);

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

    // Enable RMT TX channel
    err = rmt_enable(_rmtChannelHandle);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "setup FAILED rmt_enable error %d", err);
        return false;
    }

    // Setup ok
    _isSetup = true;

#ifdef DEBUG_ESP32RMTLEDSTRIP_SETUP
    // Debug
    LOG_I(MODULE_PREFIX, "setup ok config %s numPixels %d rmtChannelHandle %p resolutionHz %d gpioNum %d encoderHandle %p", 
                hwConfig.debugStr().c_str(), _numPixels, _rmtChannelHandle, hwConfig.rmtResolutionHz, 
                hwConfig.ledDataPin, _ledStripEncoderHandle);
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
        }
    };
    esp_err_t err = rmt_transmit(_rmtChannelHandle, _ledStripEncoderHandle, _pixelBuffer.data(), numBytesToCopy, &tx_config);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "rmt_transmit failed: %d", err);
        _isSetup = false;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Wait until show complete
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ESP32RMTLedStrip::waitUntilShowComplete()
{
    // Can't wait if not setup
    if (!_isSetup)
        return;

    // Wait for RMT to be done
    rmt_tx_wait_all_done(_rmtChannelHandle, (WAIT_RMT_BASE_US + WAIT_RMT_PER_PIX_US * _pixelBuffer.size() + 1000)/1000);
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

