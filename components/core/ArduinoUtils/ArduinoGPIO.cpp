// ArduinoGPIO
// Rob Dobson 2012-2021

#ifndef ARDUINO

#include <stdint.h>
#include <ArduinoGPIO.h>
#include <ArduinoTime.h>
#include <esp_attr.h>
#include <driver/gpio.h>
#include "driver/adc.h"
#include "esp_log.h"

#ifdef CONFIG_ESP32_SPIRAM_SUPPORT
#ifdef __cplusplus
extern "C" {
#endif
#include "esp32/spiram.h"
#ifdef __cplusplus
}
#endif
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

#ifdef CONFIG_ESP32_SPIRAM_SUPPORT
#ifdef IDF_TARGET STREQUAL "esp32" 
    // Check the PIN is not used by SPIRAM
    if (esp_spiram_is_initialized())
    {
        if (pin == 16 || pin == 17 || pin == 7 || pin == 10)
        {
            return;
        }
    }
#endif
#endif

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
}

extern void IRAM_ATTR pinMode(int pin, uint8_t mode) __attribute__ ((weak, alias("__pinMode")));
extern void IRAM_ATTR digitalWrite(uint8_t pin, uint8_t val) __attribute__ ((weak, alias("__digitalWrite")));
extern int IRAM_ATTR digitalRead(uint8_t pin) __attribute__ ((weak, alias("__digitalRead")));
extern uint16_t IRAM_ATTR analogRead(uint8_t pin) __attribute__ ((weak, alias("__analogRead")));

#endif // ARDUINO
