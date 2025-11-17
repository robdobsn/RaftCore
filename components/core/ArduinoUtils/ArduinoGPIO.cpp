// ArduinoGPIO
// Rob Dobson 2012-2021

#ifdef ESP_PLATFORM

#ifndef ARDUINO

#include <stdint.h>
#include "sdkconfig.h"
#include "ArduinoGPIO.h"
#include "ArduinoTime.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_idf_version.h"

#if ((ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)) || defined(IMPLEMENT_USE_LEGACY_ANALOG_APIS))
#define ARDUINO_GPIO_USE_LEGACY_ANALOG_APIS
#endif

#ifdef ARDUINO_GPIO_USE_LEGACY_ANALOG_APIS
#include "driver/adc.h"
#else
#include "esp_adc/adc_oneshot.h"
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
#define RAFT_ADC_ATTEN_DB ADC_ATTEN_DB_12
#else
#define RAFT_ADC_ATTEN_DB ADC_ATTEN_DB_11
#endif

// #define DEBUG_PINMODE
#ifdef DEBUG_PINMODE
#include "Logger.h"
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Low-level GPIO functions
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" void IRAM_ATTR __pinMode(int pin, uint8_t mode)
{
    // Check pin valid
    if (pin < 0)
        return;

#ifndef DEBUG_PINMODE
    esp_log_level_set("gpio", ESP_LOG_NONE);
#endif

    // Base config
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << pin);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;

    // Configure the GPIO
    switch(mode)
    {
        case INPUT:
        case INPUT_PULLUP:
        case INPUT_PULLDOWN:
            io_conf.mode = GPIO_MODE_INPUT;
            if (mode == INPUT_PULLUP)
                io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
            else if (mode == INPUT_PULLDOWN)
                io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
#ifdef DEBUG_PINMODE
            LOG_I("pinmode", "pinMode pin %d INPUT%s", 
                    pin, (mode == INPUT_PULLUP) ? " PULLUP" :
                        (mode == INPUT_PULLDOWN) ? " PULLDOWN" : "");
#endif
            break;
        case OUTPUT:
#ifdef DEBUG_PINMODE
            LOG_I("pinmode", "pinMode pin %d OUTPUT", pin);
#endif
            break;
        case OUTPUT_OPEN_DRAIN:
            io_conf.mode = GPIO_MODE_OUTPUT_OD;
#ifdef DEBUG_PINMODE
            LOG_I("pinmode", "pinMode pin %d OUTPUT_OPEN_DRAIN", pin);
#endif
            break;
        case INPUT_OUTPUT_OPEN_DRAIN:
            io_conf.mode = GPIO_MODE_INPUT_OUTPUT_OD;
#ifdef DEBUG_PINMODE
            LOG_I("pinmode", "pinMode pin %d INPUT_OUTPUT_OPEN_DRAIN", pin);
#endif
            break;
        case INPUT_OUTPUT:
            io_conf.mode = GPIO_MODE_INPUT_OUTPUT;
#ifdef DEBUG_PINMODE
            LOG_I("pinmode", "pinMode pin %d INPUT_OUTPUT", pin);
#endif
            break;
        default:
            return;
    }

    // Perform the config
    gpio_config(&io_conf);
}

extern "C" void IRAM_ATTR __digitalWrite(uint8_t pin, uint8_t val)
{
    gpio_set_level((gpio_num_t)pin, val);
}

extern "C" int IRAM_ATTR __digitalRead(uint8_t pin)
{
    return gpio_get_level((gpio_num_t)pin);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Arduino-like functions
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" uint16_t IRAM_ATTR __analogRead(uint8_t pin)
{

#ifdef ARDUINO_GPIO_USE_LEGACY_ANALOG_APIS
    // Convert pin to adc channel
    // Only handles channel 1 (channel 2 generally available when WiFi used)
    adc1_channel_t adc1Chan = ADC1_CHANNEL_MAX;
    switch(pin)
    {
        case 36: adc1Chan = ADC1_CHANNEL_0; break;
        case 37: adc1Chan = ADC1_CHANNEL_1; break;
        case 38: adc1Chan = ADC1_CHANNEL_2; break;
        case 39: adc1Chan = ADC1_CHANNEL_3; break;
        case 32: adc1Chan = ADC1_CHANNEL_4; break;
        case 33: adc1Chan = ADC1_CHANNEL_5; break;
        case 34: adc1Chan = ADC1_CHANNEL_6; break;
        case 35: adc1Chan = ADC1_CHANNEL_7; break;
        default: 
            // Invalid channel - return 0 reading
            adc1Chan = ADC1_CHANNEL_MAX;
    }
    adc2_channel_t adc2Chan = ADC2_CHANNEL_MAX;
    switch (pin)
    {
        case 4: adc2Chan = ADC2_CHANNEL_0; break;
        case 0: adc2Chan = ADC2_CHANNEL_1; break;
        case 2: adc2Chan = ADC2_CHANNEL_2; break;
        case 15: adc2Chan = ADC2_CHANNEL_3; break;
        case 13: adc2Chan = ADC2_CHANNEL_4; break;
        case 12: adc2Chan = ADC2_CHANNEL_5; break;
        case 14: adc2Chan = ADC2_CHANNEL_6; break;
        case 27: adc2Chan = ADC2_CHANNEL_7; break;
        case 25: adc2Chan = ADC2_CHANNEL_8; break;
        case 26: adc2Chan = ADC2_CHANNEL_9; break;
        default: 
            // Invalid channel - return 0 reading
            adc2Chan = ADC2_CHANNEL_MAX;
    }
    if (adc1Chan != ADC1_CHANNEL_MAX)
    {
        // Configure width
        adc1_config_width(ADC_WIDTH_BIT_12);

        // Set attenuation (to allow voltages 0 .. 2.5V approx)
        adc1_config_channel_atten(adc1Chan, RAFT_ADC_ATTEN_DB);

        // Get adc reading
        return adc1_get_raw(adc1Chan);
    }
    else if (adc2Chan != ADC2_CHANNEL_MAX)
    {
        // Configure width
        adc2_config_channel_atten(adc2Chan, RAFT_ADC_ATTEN_DB);

        // Get adc reading
        int rawValue = 0;
        esp_err_t err = adc2_get_raw(adc2Chan, ADC_WIDTH_BIT_12, &rawValue);
        if (err == ESP_OK)
            return rawValue;
        return 0;
    }
    else
    {
        return 0;
    }
#else
    // Convert pin to adc channel
    adc_unit_t adcUnit = ADC_UNIT_1;
    adc_channel_t adcChannel = ADC_CHANNEL_0;
    if (adc_oneshot_io_to_channel(pin, &adcUnit, &adcChannel) != ESP_OK)
        return 0;

    // Configure one-shot ADC
    adc_oneshot_unit_handle_t adcHandle;
    adc_oneshot_unit_init_cfg_t initConfig = {
        .unit_id = adcUnit,
        .clk_src = (adc_oneshot_clk_src_t)0, // use default clock
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&initConfig, &adcHandle) != ESP_OK)
        return 0;

    // Configure channel
    adc_oneshot_chan_cfg_t chanConfig = {
        .atten = RAFT_ADC_ATTEN_DB,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_oneshot_config_channel(adcHandle, adcChannel, &chanConfig) != ESP_OK)
        return 0;

    // Get adc reading
    int adcVal = 0;
    if (adc_oneshot_read(adcHandle, adcChannel, &adcVal) != ESP_OK)
        return 0;

    // Recycle the one-shot ADC
    adc_oneshot_del_unit(adcHandle);

    // Return value
    return adcVal;
#endif

}

extern void pinMode(int pin, uint8_t mode) __attribute__ ((weak, alias("__pinMode")));
extern void digitalWrite(uint8_t pin, uint8_t val) __attribute__ ((weak, alias("__digitalWrite")));
extern int digitalRead(uint8_t pin) __attribute__ ((weak, alias("__digitalRead")));
extern uint16_t analogRead(uint8_t pin) __attribute__ ((weak, alias("__analogRead")));

#endif // ARDUINO

#endif // ESP_PLATFORM

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Linux stub implementations
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(__linux__) && !defined(ESP_PLATFORM)

#include "ArduinoGPIO.h"
#include "Logger.h"

#define WARN_ON_GPIO_MODE_STUBS
// #define WARN_ON_GPIO_READ_STUBS
// #define WARN_ON_GPIO_WRITE_STUBS
// #define WARN_ON_GPIO_ANALOG_READ_STUBS

static const char* MODULE_PREFIX = "ArduinoGPIO";

// Stub implementations for Linux - these do nothing but log warnings
extern "C" void pinMode(int pin, uint8_t mode)
{
#ifdef WARN_ON_GPIO_MODE_STUBS
    LOG_W(MODULE_PREFIX, "pinMode(%d, %d) - stub implementation for Linux", pin, mode);
#endif
}

extern "C" void digitalWrite(uint8_t pin, uint8_t val)
{
#ifdef WARN_ON_GPIO_WRITE_STUBS
    LOG_W(MODULE_PREFIX, "digitalWrite(%d, %d) - stub implementation for Linux", pin, val);
#endif
}

extern "C" int digitalRead(uint8_t pin)
{
#ifdef WARN_ON_GPIO_READ_STUBS
    LOG_W(MODULE_PREFIX, "digitalRead(%d) - stub implementation for Linux", pin);
#endif
    return 0;
}

extern "C" uint16_t analogRead(uint8_t pin)
{
#ifdef WARN_ON_GPIO_ANALOG_READ_STUBS
    LOG_W(MODULE_PREFIX, "analogRead(%d) - stub implementation for Linux", pin);
#endif
    return 0;
}

#endif // __linux__ && !ESP_PLATFORM
