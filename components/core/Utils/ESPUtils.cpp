/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ESPUtils
//
// Rob Dobson 2020-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef ESP8266

#include <limits.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "sdkconfig.h"
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
#include "ESPUtils.h"
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

// Get size of SPIRAM (returns 0 if not available)
uint32_t utilsGetSPIRAMSize()
{
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
}

#endif