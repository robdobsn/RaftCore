// ArduinoTime
// Rob Dobson 2012-2021

#ifndef ARDUINO

#include <stdint.h>

#ifdef ESP_PLATFORM
#include "esp_timer.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define FUNCTION_DECORATOR_IRAM
#else
#include <time.h>
#define FUNCTION_DECORATOR_IRAM
#endif // ESP_PLATFORM

#include "ArduinoTime.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Low-level time functions for Arduino compat
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint64_t FUNCTION_DECORATOR_IRAM micros()
{
#ifdef ESP_PLATFORM
    return (uint64_t) (esp_timer_get_time());
#else
    // Get time on a linux platform
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL;
#endif
}

unsigned long FUNCTION_DECORATOR_IRAM millis()
{
#ifdef ESP_PLATFORM
    return (unsigned long) (esp_timer_get_time() / 1000ULL);
#else
    // Get time on a linux platform
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long) ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL;
#endif
}

void FUNCTION_DECORATOR_IRAM delay(uint32_t ms)
{
#ifdef ESP_PLATFORM
    vTaskDelay(ms / portTICK_PERIOD_MS);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
#endif
}

void FUNCTION_DECORATOR_IRAM delayMicroseconds(uint64_t us)
{
#ifdef ESP_PLATFORM
    uint64_t m = micros();
    if(us){
        int64_t e = (m + us);
        if(m > e){ //overflow
            while(micros() > e){
                __asm__ volatile ("nop");
            }
        }
        while(micros() < e){
            __asm__ volatile ("nop");
        }
    }
#else
    struct timespec ts;
    ts.tv_sec = us / 1000000;
    ts.tv_nsec = (us % 1000000) * 1000;
    nanosleep(&ts, NULL);
#endif
}

#endif // ARDUINO
