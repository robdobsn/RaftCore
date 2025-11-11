/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// PlatformUtils
//
// Rob Dobson 2020-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "PlatformUtils.h"

#ifdef ESP_PLATFORM

#ifndef ESP8266

#include <limits.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_app_desc.h"
#include "esp_random.h"
#ifdef CONFIG_ESP32_SPIRAM_SUPPORT
#ifdef __cplusplus
extern "C" {
#endif
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
#include "esp32/spiram.h"
#else
#include "esp_psram.h"
#endif
#ifdef __cplusplus
}
#endif
#endif
#include "RaftUtils.h"

#ifndef ARDUINO

void enableCore0WDT(){
    TaskHandle_t idle_0 = 
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
        xTaskGetIdleTaskHandleForCore(0);
#else
        xTaskGetIdleTaskHandleForCPU(0);
#endif
    if(idle_0 == NULL || esp_task_wdt_add(idle_0) != ESP_OK){
        esp_log_write(ESP_LOG_ERROR, "", "Failed to add Core 0 IDLE task to WDT");
    }
}

void disableCore0WDT(){
    TaskHandle_t idle_0 = 
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
        xTaskGetIdleTaskHandleForCore(0);
#else
        xTaskGetIdleTaskHandleForCPU(0);
#endif
    if(idle_0 == NULL || esp_task_wdt_delete(idle_0) != ESP_OK){
        esp_log_write(ESP_LOG_ERROR, "", "Failed to remove Core 0 IDLE task from WDT");
    }
}

void enableCore1WDT(){
    TaskHandle_t idle_1 = 
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
        xTaskGetIdleTaskHandleForCore(1);
#else
        xTaskGetIdleTaskHandleForCPU(1);
#endif
    if(idle_1 == NULL || esp_task_wdt_add(idle_1) != ESP_OK){
        esp_log_write(ESP_LOG_ERROR, "", "Failed to add Core 1 IDLE task to WDT");
    }
}

void disableCore1WDT(){
    TaskHandle_t idle_1 = 
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
        xTaskGetIdleTaskHandleForCore(1);
#else
        xTaskGetIdleTaskHandleForCPU(1);
#endif
    if(idle_1 == NULL || esp_task_wdt_delete(idle_1) != ESP_OK){
        esp_log_write(ESP_LOG_ERROR, "", "Failed to remove Core 1 IDLE task from WDT");
    }
}

#endif

static String __systemMACCachedBT;
static String __systemMACCachedETH;
static String __systemMACCachedSTA;
static String __systemMACCachedSep;
String getSystemMACAddressStr(esp_mac_type_t macType, const char* pSeparator)
{
    if (pSeparator && (__systemMACCachedSep.equals(pSeparator)))
    {
    // Check if already got
        if ((macType == ESP_MAC_BT) && (__systemMACCachedBT.length() > 0))
            return __systemMACCachedBT;
        else if ((macType == ESP_MAC_ETH) && (__systemMACCachedETH.length() > 0))
            return __systemMACCachedETH;
        else if ((macType == ESP_MAC_WIFI_STA) && (__systemMACCachedSTA.length() > 0))
            return __systemMACCachedSTA;
    }

    // Use the public (MAC) address of BLE
    uint8_t addr[6] = {0,0,0,0,0,0};
    int rc = esp_read_mac(addr, macType);
    if (rc != ESP_OK)
        return "";
    String macStr = Raft::formatMACAddr(addr, pSeparator);
    if (macType == ESP_MAC_BT)
        __systemMACCachedBT = macStr;
    else if (macType == ESP_MAC_ETH)
        __systemMACCachedETH = macStr;
    else if (macType == ESP_MAC_WIFI_STA)
        __systemMACCachedSTA = macStr;
    if (pSeparator)
        __systemMACCachedSep = pSeparator;
    return macStr;
}

// Get the app version string
String platform_getAppVersion()
{
    const esp_app_desc_t *pAppDesc = esp_app_get_description();
    if (pAppDesc->version[0] == 0)
        return "0.0.0";
    else if (pAppDesc->version[0] == 'v')
        return String(pAppDesc->version+1);
    return String(pAppDesc->version);
}

// Get compile time and date
String platform_getCompileTime(bool includeDate)
{
    const esp_app_desc_t *pAppDesc = esp_app_get_description();
    return (includeDate ? " " + String(pAppDesc->date) : "") + String(pAppDesc->time);
}

/// @brief Convert ESP-IDF/LWIP error code to string
/// @param err Error code (err_t from lwip/err.h)
/// @return String description of the error
const char* espIdfErrToStr(int err)
{
    // LWIP error codes - from lwip/err.h
    // ERR_OK = 0, ERR_MEM = -1, ERR_BUF = -2, etc.
    switch(err)
    {
        case 0: return "OK";              // ERR_OK
        case -1: return "Out of Mem";     // ERR_MEM
        case -2: return "Buffer error";   // ERR_BUF
        case -3: return "Timeout";        // ERR_TIMEOUT
        case -4: return "Routing problem"; // ERR_RTE
        case -5: return "Op in progress"; // ERR_INPROGRESS
        case -6: return "Illegal value";  // ERR_VAL
        case -7: return "Op would block"; // ERR_WOULDBLOCK
        case -8: return "Addr in Use";    // ERR_USE
        case -9: return "Already connecting"; // ERR_ALREADY
        case -10: return "Already connected"; // ERR_ISCONN
        case -11: return "Write error";   // ERR_CONN
        case -12: return "NETIF error";   // ERR_IF
        case -13: return "Conn aborted";  // ERR_ABRT
        case -14: return "Conn reset";    // ERR_RST
        case -15: return "Conn closed";   // ERR_CLSD
        case -16: return "Illegal arg";   // ERR_ARG
    }
    return "UNKNOWN";
}

#endif // ESP8266

#else

#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

// char* utoa(unsigned value, char* result, int base) {
//     // check that the base if valid
//     if (base < 2 || base > 36) { *result = '\0'; return result; }

//     char* ptr = result, *ptr1 = result, tmp_char;
//     unsigned tmp_value;

//     do {
//         tmp_value = value;
//         value /= base;
//         *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[tmp_value % base];
//     } while ( value );

//     // Apply negative sign
//     *ptr-- = '\0';
//     while(ptr1 < ptr) {
//         tmp_char = *ptr;
//         *ptr--= *ptr1;
//         *ptr1++ = tmp_char;
//     }
//     return result;
// }

/**
 * C++ version 0.4 char* style "itoa":
 * Written by Lukás Chmela
 * Released under GPLv3.

    */
char* itoa(int value, char* result, int base) {
    // check that the base if valid
    if (base < 2 || base > 36) { *result = '\0'; return result; }

    char* ptr = result, *ptr1 = result, tmp_char;
    int tmp_value;

    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
    } while ( value );

    // Apply negative sign
    if (tmp_value < 0) *ptr++ = '-';
    *ptr-- = '\0';
    while(ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr--= *ptr1;
        *ptr1++ = tmp_char;
    }
    return result;
}

char* utoa(unsigned int value, char* result, int base) {
    // check that the base if valid
    if (base < 2 || base > 36) { *result = '\0'; return result; }

    char* ptr = result, *ptr1 = result, tmp_char;
    unsigned int tmp_value;

    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
    } while ( value );

    // Apply negative sign
    if (tmp_value < 0) *ptr++ = '-';
    *ptr-- = '\0';
    while(ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr--= *ptr1;
        *ptr1++ = tmp_char;
    }
    return result;
}

char* ltoa(long value, char* result, int base) {
    // check that the base if valid
    if (base < 2 || base > 36) { *result = '\0'; return result; }

    char* ptr = result, *ptr1 = result, tmp_char;
    long tmp_value;

    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
    } while ( value );

    // Apply negative sign
    if (tmp_value < 0) *ptr++ = '-';
    *ptr-- = '\0';
    while(ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr--= *ptr1;
        *ptr1++ = tmp_char;
    }
    return result;
}

char* ultoa(unsigned long value, char* result, int base) {
    // check that the base if valid
    if (base < 2 || base > 36) { *result = '\0'; return result; }

    char* ptr = result, *ptr1 = result, tmp_char;
    unsigned long tmp_value;

    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
    } while ( value );

    // Apply negative sign
    if (tmp_value < 0) *ptr++ = '-';
    *ptr-- = '\0';
    while(ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr--= *ptr1;
        *ptr1++ = tmp_char;
    }
    return result;
}

char* lltoa(long long value, char* result, int base) {
    // check that the base if valid
    if (base < 2 || base > 36) { *result = '\0'; return result; }

    char* ptr = result, *ptr1 = result, tmp_char;
    long long tmp_value;

    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
    } while ( value );

    // Apply negative sign
    if (tmp_value < 0) *ptr++ = '-';
    *ptr-- = '\0';
    while(ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr--= *ptr1;
        *ptr1++ = tmp_char;
    }
    return result;
}

char* ulltoa(unsigned long long value, char* result, int base) {
    // check that the base if valid
    if (base < 2 || base > 36) { *result = '\0'; return result; }

    char* ptr = result, *ptr1 = result, tmp_char;
    unsigned long long tmp_value;

    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
    } while ( value );

    // Apply negative sign
    if (tmp_value < 0) *ptr++ = '-';
    *ptr-- = '\0';
    while(ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr--= *ptr1;
        *ptr1++ = tmp_char;
    }
    return result;
}

char* dtostrf(double value, int width, unsigned int precision, char* result) {
    char* ptr = result;
    char* ptr1 = result;
    char tmp_char;
    int tmp_value;
    double tmp_float;

    // Check for negative number
    if (value < 0.0) {
        *ptr++ = '-';
        value = -value;
    }

    // Calculate magnitude
    tmp_float = value;
    while ((tmp_float >= 10.0) && (width > 1)) {
        tmp_float /= 10.0;
        width--;
    }

    // Calculate leading zeros
    while ((width > 1) && (precision > 0)) {
        tmp_float *= 10.0;
        width--;
        precision--;
    }

    // Round value
    tmp_float += 0.5;

    // Copy reversed digits to result
    tmp_value = (int)tmp_float;
    tmp_float -= tmp_value;
    if (tmp_float > 0.5) {
        tmp_value++;
        if (tmp_value >= 10) {
            tmp_value = 0;
            *ptr++ = '1';
        }
    }
    if (tmp_value == 0) *ptr++ = '0';
    else {
        while (tmp_value > 0) {
            *ptr++ = (char)('0' + (tmp_value % 10));
            tmp_value /= 10;
        }
    }

    // Copy decimal point
    if (precision > 0) *ptr++ = '.';

    // Copy digits from tmp_float
    while (precision > 0) {
        tmp_float *= 10.0;
        tmp_value = (int)tmp_float;
        *ptr++ = (char)('0' + tmp_value);
        tmp_float -= tmp_value;
        precision--;
    }

    // Add tailing zeros
    while ((width > 1) && (ptr > ptr1) && (*(ptr-1) == '0')) {
        ptr--;
        width--;
    }

    // Add tailing space
    while ((width > 1) && (ptr > ptr1)) {
        *ptr++ = ' ';
        width--;
    }

    // Add null termination
    *ptr = '\0';

    // Reverse string
    ptr--;
    while(ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr--= *ptr1;
        *ptr1++ = tmp_char;
    }

    return result;
}

/*	$OpenBSD: strlcat.c,v 1.13 2005/08/08 08:05:37 espie Exp $	*/
/*
 * Copyright (c) 1998 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * Appends src to string dst of size siz (unlike strncat, siz is the
 * full size of dst, not space left).  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz <= strlen(dst)).
 * Returns strlen(src) + MIN(siz, strlen(initial dst)).
 * If retval >= siz, truncation occurred.
 */
size_t
strlcat(char *dst, const char *src, size_t siz)
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;
	size_t dlen;
	/* Find the end of dst and adjust bytes left but don't go past end */
	while (n-- != 0 && *d != '\0')
		d++;
	dlen = d - dst;
	n = siz - dlen;
	if (n == 0)
		return(dlen + strlen(s));
	while (*s != '\0') {
		if (n != 1) {
			*d++ = *s;
			n--;
		}
		s++;
	}
	*d = '\0';
	return(dlen + (s - src));	/* count does not include NUL */
}

/*	$OpenBSD: strlcpy.c,v 1.11 2006/05/05 15:27:38 millert Exp $	*/
/*
 * Robust version of strncpy(3) that ensures null-termination.
 * This function is not in the C standard but is a common extension.
 */
/*
    * OpenBSD implementation of strlcpy
    */
size_t
strlcpy(char *dst, const char *src, size_t siz)
{
    char *d = dst;
    const char *s = src;
    size_t n = siz;

    /* Copy as many bytes as will fit */
    if (n != 0 && --n != 0) {
        do {
            if ((*d++ = *s++) == 0)
                break;
        } while (--n != 0);
    }

    /* Not enough room in dst, add NUL and traverse rest of src */
    if (n == 0) {
        if (siz != 0)
            *d = '\0';      /* NUL-terminate dst */
        while (*s++)
            ;
    }

    return(s - src - 1);    /* count does not include NUL */
}

// Get the app version string
String platform_getAppVersion()
{
    return "0.0.0";
}

// Get compile time and date
String platform_getCompileTime(bool includeDate)
{
    if (includeDate)
        return __DATE__ " " __TIME__;
    return __TIME__;
}

#endif // ESP_PLATFORM

// Get size of SPIRAM (returns 0 if not available)
uint32_t utilsGetSPIRAMSize()
{
#ifdef ESP_PLATFORM
#ifdef CONFIG_ESP32_SPIRAM_SUPPORT
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
    switch (esp_spiram_get_chip_size())
    {
        case ESP_SPIRAM_SIZE_16MBITS:
            return 2 * 1024 * 1024;
        case ESP_SPIRAM_SIZE_32MBITS:
        case ESP_SPIRAM_SIZE_64MBITS:
            // Note that only 4MBytes is usable
            return 4 * 1024 * 1024;
        default:
            return 0;
    }
#else
    return esp_psram_get_size();
#endif
#else
    return 0;
#endif
#else
    return UINT32_MAX;
#endif // ESP_PLATFORM
}

// Get a platform-independent random number
uint32_t platform_random()
{
#ifdef ESP_PLATFORM
    return esp_random();
#else
    // Use standard library random for non-ESP platforms
    return (uint32_t)rand();
#endif
}
