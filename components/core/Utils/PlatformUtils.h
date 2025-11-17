/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// PlatformUtils
//
// Rob Dobson 2020-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>

#ifdef ESP_PLATFORM

#ifndef ESP8266

#include <cstdint>
#include "esp_system.h"
#include "esp_mac.h"
#include "RaftArduino.h"

#ifndef ARDUINO

// enable/disable WDT for the IDLE task on Core 0 (SYSTEM)
void enableCore0WDT();
void disableCore0WDT();

// enable/disable WDT for the IDLE task on Core 1 (Arduino)
void enableCore1WDT();
void disableCore1WDT();

#endif

// https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/system.html
// WIFI STA is base MAC address
// WIFI SoftAP is base + 1
// BT is base +2
// Ethernet is base +3
String getSystemMACAddressStr(esp_mac_type_t macType, const char* pSeparator);

/// @brief Convert ESP-IDF/LWIP error code to string
/// @param err Error code (err_t from lwip/err.h)
/// @return String description of the error
const char* espIdfErrToStr(int err);

#endif // ESP8266

#else

#include <string.h>
#include "ArduinoWString.h"

#ifdef __cplusplus
extern "C" {
#endif

    char* itoa(int value, char* result, int base);
    char* utoa(unsigned int value, char* result, int base);
    char* ltoa(long value, char* result, int base);
    char* ultoa(unsigned long value, char* result, int base);
    char* dtostrf(double value, int width, unsigned int precision, char* result);
    char* ftoa(float value, int width, unsigned int precision, char* result);
    char* lltoa(long long value, char* result, int base);
    char* ulltoa(unsigned long long value, char* result, int base);
    size_t strlcat(char *dst, const char *src, size_t siz);
    size_t strlcpy(char *dst, const char *src, size_t siz);

#ifdef __cplusplus
}
#endif

#endif // ESP_PLATFORM

// Get size of SPIRAM (returns 0 if not available) or UINT32_MAX if on a non-ESP platform
uint32_t utilsGetSPIRAMSize();

// Get a platform-independent random number
uint32_t platform_random();

// Get the app version string
String platform_getAppVersion();

// Get compile time and date
String platform_getCompileTime(bool includeDate);