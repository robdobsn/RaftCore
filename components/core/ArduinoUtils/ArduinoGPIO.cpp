// ArduinoGPIO
// Rob Dobson 2012-2021

#ifndef ARDUINO

#include <stdint.h>
#include <ArduinoGPIO.h>
#include <ArduinoTime.h>
#include <esp_attr.h>
#include <driver/gpio.h>

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
    if (pin >= 0)
    {
#ifdef CONFIG_ESP32_SPIRAM_SUPPORT
        // Check the PIN is not used by SPIRAM
        if (esp_spiram_is_initialized())
        {
            if (pin == 16 || pin == 17 || pin == 7 || pin == 10)
            {
                return;
            }
        }
#endif

        // Configure the GPIO
        switch(mode)
        {
            case INPUT:
            case INPUT_PULLUP:
            case INPUT_PULLDOWN:
                gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
                if (mode == INPUT_PULLUP)
                    gpio_set_pull_mode((gpio_num_t)pin, GPIO_PULLUP_ONLY);
                else if (mode == INPUT_PULLDOWN)
                    gpio_set_pull_mode((gpio_num_t)pin, GPIO_PULLDOWN_ONLY);
                else
                    gpio_set_pull_mode((gpio_num_t)pin, GPIO_FLOATING);
#ifdef DEBUG_PINMODE
                LOG_I("pinmode", "pinMode pin %d INPUT", pin);
#endif
                break;
            case OUTPUT:
                gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
#ifdef DEBUG_PINMODE
                LOG_I("pinmode", "pinMode pin %d OUTPUT", pin);
#endif
                break;
            case OUTPUT_OPEN_DRAIN:
                gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT_OD);
#ifdef DEBUG_PINMODE
                LOG_I("pinmode", "pinMode pin %d OUTPUT_OPEN_DRAIN", pin);
#endif
                break;
        }
    }
}

extern "C" void IRAM_ATTR __digitalWrite(uint8_t pin, uint8_t val)
{
    gpio_set_level((gpio_num_t)pin, val);
}

extern "C" int IRAM_ATTR __digitalRead(uint8_t pin)
{
    return gpio_get_level((gpio_num_t)pin);
}

extern void pinMode(int pin, uint8_t mode) __attribute__ ((weak, alias("__pinMode")));
extern void digitalWrite(uint8_t pin, uint8_t val) __attribute__ ((weak, alias("__digitalWrite")));
extern int digitalRead(uint8_t pin) __attribute__ ((weak, alias("__digitalRead")));

#endif // ARDUINO
