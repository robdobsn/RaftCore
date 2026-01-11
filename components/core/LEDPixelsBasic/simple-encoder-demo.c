/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "esp_timer.h"

#define RMT_LED_STRIP_RESOLUTION_HZ 10000000 // 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define RMT_LED_STRIP_GPIO_NUM 32

#define NUMBER_OF_LEDS 1000

// #define EXAMPLE_FRAME_DURATION_MS 50
// #define EXAMPLE_ANGLE_INC_FRAME 0.02
// #define EXAMPLE_ANGLE_INC_LED 0.3

static const char *TAG = "example";

static uint8_t led_strip_pixels[NUMBER_OF_LEDS * 3];

static const rmt_symbol_word_t ws2812_zero = {
    .level0 = 1,
    .duration0 = 0.3 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000, // T0H=0.3us
    .level1 = 0,
    .duration1 = 0.9 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000, // T0L=0.9us
};

static const rmt_symbol_word_t ws2812_one = {
    .level0 = 1,
    .duration0 = 0.9 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000, // T1H=0.9us
    .level1 = 0,
    .duration1 = 0.3 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000, // T1L=0.3us
};

// reset defaults to 50uS
static const rmt_symbol_word_t ws2812_reset = {
    .level0 = 0,
    .duration0 = RMT_LED_STRIP_RESOLUTION_HZ / 1000000 * 50 / 2,
    .level1 = 0,
    .duration1 = RMT_LED_STRIP_RESOLUTION_HZ / 1000000 * 50 / 2,
};

unsigned long millis()
{
    return esp_timer_get_time() / 1000ULL;
}

bool isTimeout(uint64_t curTime, uint64_t lastTime, uint64_t maxDuration)
{
    if (curTime >= lastTime)
    {
        return (curTime > lastTime + maxDuration);
    }
    return (ULONG_LONG_MAX - (lastTime - curTime) > maxDuration);
}

// State machine states
enum State
{
    STATE_INITIAL_SYNC, // Initial sync flashes at start of sequence
    STATE_LED_ON,       // Individual LED lit
    STATE_INTER_SYNC    // Sync flash between LED groups
};

// Default configuration values
static const uint32_t DEFAULT_INITIAL_SYNC_FLASHES = 3;
static const uint32_t DEFAULT_SYNC_FLASH_TIME_MS = 200;
static const uint32_t DEFAULT_LED_ON_TIME_MS = 200;
static const uint32_t DEFAULT_LEDS_BETWEEN_SYNCS = 10;
static const uint32_t DEFAULT_SYNC_BRIGHTNESS = 40;
static const uint32_t DEFAULT_LED_BRIGHTNESS = 255;

// Configuration
uint32_t _initialSyncFlashes = DEFAULT_INITIAL_SYNC_FLASHES;
uint32_t _syncFlashTimeMs = DEFAULT_SYNC_FLASH_TIME_MS;
uint32_t _ledOnTimeMs = DEFAULT_LED_ON_TIME_MS;
uint32_t _ledsBetweenSyncs = DEFAULT_LEDS_BETWEEN_SYNCS;
uint32_t _syncBrightness = DEFAULT_SYNC_BRIGHTNESS;
uint32_t _ledBrightness = DEFAULT_LED_BRIGHTNESS;

// State
enum State _state = STATE_INITIAL_SYNC;
uint32_t _curLedIdx = 0;
uint32_t _syncPhase = 0;        // Counts on/off phases during sync
uint32_t _ledsLitSinceSync = 0; // Count LEDs lit since last sync
uint32_t _lastUpdateMs = 0;
uint32_t _actualEndIdx = NUMBER_OF_LEDS-1;

void clearAllLeds()
{
    for (uint32_t i = 0; i <= _actualEndIdx; i++)
    {
        led_strip_pixels[i * 3] = 0;
        led_strip_pixels[i * 3 + 1] = 0;
        led_strip_pixels[i * 3 + 2] = 0;
    }
}

void showCurrentLed()
{
    clearAllLeds();
    if (_curLedIdx <= _actualEndIdx)
    {
        led_strip_pixels[_curLedIdx * 3] = _ledBrightness;
        led_strip_pixels[_curLedIdx * 3 + 1] = _ledBrightness;
        led_strip_pixels[_curLedIdx * 3 + 2] = _ledBrightness;
    }
}


void setAllLeds(uint32_t r, uint32_t g, uint32_t b)
{
    for (uint32_t i = 0; i <= _actualEndIdx; i++)
    {
        led_strip_pixels[i * 3] = r;
        led_strip_pixels[i * 3 + 1] = g;
        led_strip_pixels[i * 3 + 2] = b;
    }
}

bool handleInitialSync(uint32_t now)
{
    uint32_t totalSyncPhases = _initialSyncFlashes * 2; // Each flash has on and off phase

    if (!isTimeout(now, _lastUpdateMs, _syncFlashTimeMs))
        return false;

    _lastUpdateMs = now;

    if (_syncPhase < totalSyncPhases)
    {
        if (_syncPhase % 2 == 0)
        {
            // Flash all LEDs on
            setAllLeds(_syncBrightness, _syncBrightness, _syncBrightness);
#ifdef DEBUG_LEDPATTERN_AUTOID
            if (_syncPhase == 0)
                ESP_LOGI(MODULE_PREFIX, "Initial sync start");
#endif
        }
        else
        {
            // All LEDs off
            clearAllLeds();
        }
        _syncPhase++;
    }
    else
    {
        // Done with initial sync, move to first LED
        _syncPhase = 0;
        _ledsLitSinceSync = 0;
        _state = STATE_LED_ON;
        showCurrentLed();
    }
    return true;
}

bool handleLedOn(uint32_t now)
{
    if (!isTimeout(now, _lastUpdateMs, _ledOnTimeMs))
        return false;

    _lastUpdateMs = now;

    // Move to next LED
    _curLedIdx++;
    _ledsLitSinceSync++;

    // Check if we've completed all LEDs
    if (_curLedIdx > _actualEndIdx)
    {
#ifdef DEBUG_LEDPATTERN_AUTOID
        ESP_LOGI(MODULE_PREFIX, "Sequence complete, restarting");
#endif
        // Restart the entire sequence
        _curLedIdx = 0;
        _syncPhase = 0;
        _ledsLitSinceSync = 0;
        _state = STATE_INITIAL_SYNC;
        clearAllLeds();
        return true;
    }

    // Check if it's time for an inter-sync flash
    if (_ledsLitSinceSync >= _ledsBetweenSyncs)
    {
        _syncPhase = 0;
        _ledsLitSinceSync = 0;
        _state = STATE_INTER_SYNC;
        // Start first phase of inter-sync immediately
        setAllLeds(_syncBrightness, _syncBrightness, _syncBrightness);
        _syncPhase++;
#ifdef DEBUG_LEDPATTERN_AUTOID
        ESP_LOGI(MODULE_PREFIX, "Inter-sync at LED %d", _curLedIdx);
#endif
    }
    else
    {
        // Show next LED
        showCurrentLed();
    }
    return true;
}

bool handleInterSync(uint32_t now)
{
    if (!isTimeout(now, _lastUpdateMs, _syncFlashTimeMs))
        return false;

    _lastUpdateMs = now;

    // Inter-sync is just one flash (on then off = 2 phases)
    if (_syncPhase < 2)
    {
        if (_syncPhase % 2 == 0)
        {
            // Flash all LEDs on (already done when entering this state)
            setAllLeds(_syncBrightness, _syncBrightness, _syncBrightness);
        }
        else
        {
            // All LEDs off
            clearAllLeds();
        }
        _syncPhase++;
    }
    else
    {
        // Done with inter-sync, continue with LEDs
        _state = STATE_LED_ON;
        showCurrentLed();
    }
    return true;
}

static size_t encoder_callback(const void *data, size_t data_size,
                               size_t symbols_written, size_t symbols_free,
                               rmt_symbol_word_t *symbols, bool *done, void *arg)
{
    // We need a minimum of 8 symbol spaces to encode a byte. We only
    // need one to encode a reset, but it's simpler to simply demand that
    // there are 8 symbol spaces free to write anything.
    if (symbols_free < 8)
    {
        return 0;
    }

    // We can calculate where in the data we are from the symbol pos.
    // Alternatively, we could use some counter referenced by the arg
    // parameter to keep track of this.
    size_t data_pos = symbols_written / 8;
    uint8_t *data_bytes = (uint8_t *)data;
    if (data_pos < data_size)
    {
        // Encode a byte
        size_t symbol_pos = 0;
        for (int bitmask = 0x80; bitmask != 0; bitmask >>= 1)
        {
            if (data_bytes[data_pos] & bitmask)
            {
                symbols[symbol_pos++] = ws2812_one;
            }
            else
            {
                symbols[symbol_pos++] = ws2812_zero;
            }
        }
        // We're done; we should have written 8 symbols.
        return symbol_pos;
    }
    else
    {
        // All bytes already are encoded.
        // Encode the reset, and we're done.
        symbols[0] = ws2812_reset;
        *done = 1; // Indicate end of the transaction.
        return 1;  // we only wrote one symbol
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Create RMT TX channel");
    rmt_channel_handle_t led_chan = NULL;
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT, // select source clock
        .gpio_num = RMT_LED_STRIP_GPIO_NUM,
        .mem_block_symbols = 64, // increase the block size can make the LED less flickering
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .trans_queue_depth = 1, // only one transaction at a time (queueing without data buffering causes corruption)
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));

    ESP_LOGI(TAG, "Create simple callback-based encoder");
    rmt_encoder_handle_t simple_encoder = NULL;
    const rmt_simple_encoder_config_t simple_encoder_cfg = {
        .callback = encoder_callback
        // Note we don't set min_chunk_size here as the default of 64 is good enough.
    };
    ESP_ERROR_CHECK(rmt_new_simple_encoder(&simple_encoder_cfg, &simple_encoder));

    ESP_LOGI(TAG, "Enable RMT TX channel");
    ESP_ERROR_CHECK(rmt_enable(led_chan));

    ESP_LOGI(TAG, "Start LED rainbow chase");
    rmt_transmit_config_t tx_config = {
        .loop_count = 0, // no transfer loop
    };
    // uint32_t ledIdx = 0;
    // const uint32_t colour_on = 0x282828;
    // const uint32_t color_off = 0;
    while (1)
    {

        uint32_t now = millis();
        bool isUpdated = false;

        switch (_state)
        {
        case STATE_INITIAL_SYNC:
            isUpdated = handleInitialSync(now);
            break;

        case STATE_LED_ON:
            isUpdated = handleLedOn(now);
            break;

        case STATE_INTER_SYNC:
            isUpdated = handleInterSync(now);
            break;
        }

        if (!isUpdated)
        {
            // No state change, skip sending data
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        // for (int led = 0; led < NUMBER_OF_LEDS; led++)
        // {
        //     // Build RGB pixels.
        //     led_strip_pixels[led * 3 + 0] = ledIdx == led ? (colour_on >> 16) & 0xFF : color_off;
        //     led_strip_pixels[led * 3 + 1] = ledIdx == led ? (colour_on >> 8) & 0xFF : color_off;
        //     led_strip_pixels[led * 3 + 2] = ledIdx == led ? (colour_on >> 0) & 0xFF : color_off;
        // }
        // Flush RGB values to LEDs
        ESP_ERROR_CHECK(rmt_transmit(led_chan, simple_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
        ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
        vTaskDelay(pdMS_TO_TICKS(EXAMPLE_FRAME_DURATION_MS));

        // // Increase
        // ledIdx++;
        // if (ledIdx >= NUMBER_OF_LEDS)
        // {
        //     ledIdx = 0;
        // }
    }
}
