// ArduinoGPIO
// Rob Dobson 2012-2021

#ifndef ARDUINO

#include "sdkconfig.h"
#include "ArduinoGPIO.h"
#include "ArduinoTime.h"
#include "stdint.h"
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

// #define DEBUG_PINMODE
#ifdef DEBUG_PINMODE
#include <Logger.h>
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
    adc1_channel_t analogChan = ADC1_CHANNEL_0;
    switch(pin)
    {
        case 36: analogChan = ADC1_CHANNEL_0; break;
        case 37: analogChan = ADC1_CHANNEL_1; break;
        case 38: analogChan = ADC1_CHANNEL_2; break;
        case 39: analogChan = ADC1_CHANNEL_3; break;
        case 32: analogChan = ADC1_CHANNEL_4; break;
        case 33: analogChan = ADC1_CHANNEL_5; break;
        case 34: analogChan = ADC1_CHANNEL_6; break;
        case 35: analogChan = ADC1_CHANNEL_7; break;
        default: 
            // Invalid channel - return 0 reading
            return 0;
    }

    // Configure width
    adc1_config_width(ADC_WIDTH_BIT_12);

    // Set attenuation (to allow voltages 0 .. 2.5V approx)
    adc1_config_channel_atten(analogChan, ADC_ATTEN_DB_11);

    // Get adc reading
    return adc1_get_raw(analogChan);
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
        .atten = ADC_ATTEN_DB_11,
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
