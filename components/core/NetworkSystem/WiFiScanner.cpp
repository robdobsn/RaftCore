/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// WiFiScanner 
//
// Rob Dobson 2018-2022
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "Logger.h"
#include "WiFiScanner.h"
#include "RaftUtils.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

WiFiScanner::WiFiScanner()
{
}

WiFiScanner::~WiFiScanner()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start scan
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool WiFiScanner::scanStart()
{
    _scanInProgress = true;
    return esp_wifi_scan_start(NULL, false) == ESP_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start complete - called by WiFi event handler
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void WiFiScanner::scanComplete()
{
    _scanInProgress = false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get results JSON
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool WiFiScanner::getResultsJSON(String& json)
{
    // Check if in progress
    if (_scanInProgress)
    {
        // Return scan in progress
        json = "\"scanInProgress\":1";
        return false;
    }

    // Empty list
    json = "\"wifi\":[]";

    // Get results
    WiFiScanResultList results;
    if (getScanResults(results))
    {
        json = "\"wifi\":[";
        for (int i = 0; i < results.size(); i++)
        {
            if (i > 0)
                json += ",";
            json += "{";
            json += "\"ssid\":\"" + Raft::escapeString(results[i].ssid) + "\",";
            json += "\"rssi\":" + String(results[i].rssi) + ",";
            json += "\"ch1\":" + String(results[i].primaryChannel) + ",";
            json += "\"ch2\":" + String(results[i].secondaryChannel) + ",";
            json += "\"auth\":\"" + getAuthModeString(results[i].authMode) + "\",";
            json += "\"bssid\":\"" + results[i].bssid + "\",";
            json += "\"pair\":\"" + getCipherString(results[i].pairwiseCipher) + "\",";
            json += "\"group\":\"" + getCipherString(results[i].groupCipher) + "\"";
            json += "}";
        }
        json += "]";
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get scan results
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool WiFiScanner::getScanResults(WiFiScanResultList& results)
{
    // Check if in progress
    if (_scanInProgress)
        return false;

    // Get scan results
    uint16_t num = MAX_SCAN_LIST_SIZE;
    wifi_ap_record_t *pRecords = (wifi_ap_record_t *)malloc(num * sizeof(wifi_ap_record_t));
    if (pRecords == NULL)
    {
        LOG_E(MODULE_PREFIX, "getScanResults malloc failed");
        return false;
    }
    if (esp_wifi_scan_get_ap_records(&num, pRecords) != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "getScanResults esp_wifi_scan_get_ap_records failed");
        free(pRecords);
        return false;
    }
    uint16_t wifi_count = 0;
    if (esp_wifi_scan_get_ap_num(&wifi_count) != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "getScanResults esp_wifi_scan_get_ap_num failed");
        free(pRecords);
        return false;
    }
    if (wifi_count == 0)
    {
        LOG_E(MODULE_PREFIX, "getScanResults no records");
        free(pRecords);
        return false;
    }

    // Process results
    for (int i = 0; (i < wifi_count) && (i < MAX_SCAN_LIST_SIZE); i++)
    {
        // Get record
        wifi_ap_record_t *pRecord = &pRecords[i];

        // Create WiFiScanResult
        static const uint32_t SSID_MAX_LEN = 32;
        WiFiScanResult result;
        result.ssid = String(pRecord->ssid, SSID_MAX_LEN);
        result.rssi = pRecord->rssi;
        result.primaryChannel = pRecord->primary;
        result.secondaryChannel = pRecord->second;
        result.bssid = Raft::formatMACAddr(pRecord->bssid, ":");
        result.authMode = pRecord->authmode;
        result.pairwiseCipher = pRecord->pairwise_cipher;
        result.groupCipher = pRecord->group_cipher;
        results.push_back(result);
    }

    // Free memory
    free(pRecords);
    return true;
}

    // uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    // wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
    // uint16_t ap_count = 0;
    // memset(ap_info, 0, sizeof(ap_info));

    // ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
    // ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    // LOG_I(MODULE_PREFIX, "Total APs scanned = %u", ap_count);
    // for (int i = 0; (i < DEFAULT_SCAN_LIST_SIZE) && (i < ap_count); i++) {
    //     LOG_I(MODULE_PREFIX, "SSID \t\t%s", ap_info[i].ssid);
    //     LOG_I(MODULE_PREFIX, "RSSI \t\t%d", ap_info[i].rssi);
    //     print_auth_mode(ap_info[i].authmode);
    //     if (ap_info[i].authmode != WIFI_AUTH_WEP) {
    //         print_cipher_type(ap_info[i].pairwise_cipher, ap_info[i].group_cipher);
    //     }
    //     LOG_I(MODULE_PREFIX, "Channel \t\t%d\n", ap_info[i].primary);
    // }

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Convert auth mode to string
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String WiFiScanner::getAuthModeString(uint16_t authMode)
{
    static const char* authModeStrings[] = {
        "OPEN",
        "WEP",             
        "WPA_PSK",         
        "WPA2_PSK",        
        "WPA_WPA2_PSK",    
        "WPA2_ENTERPRISE", 
        "WPA3_PSK",        
        "WPA2_WPA3_PSK",   
        "WAPI_PSK",        
    };
    if (authMode < sizeof(authModeStrings) / sizeof(authModeStrings[0]))
        return authModeStrings[authMode];
    return "UNKNOWN";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Convert pairwise cipher to string
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String WiFiScanner::getCipherString(uint16_t cipher)
{
    static const char* cipherStrings[] = {
        "NONE",
        "WEP40",
        "WEP104",
        "TKIP",
        "CCMP",
        "TKIP_CCMP",
        "AES_128_CMAC",
        "SMS4",
        "GCMP",
        "GCMP_256",
        "AES_GMAC128",
        "AES_GMAC256",
    };
    if (cipher < sizeof(cipherStrings) / sizeof(cipherStrings[0]))
        return cipherStrings[cipher];
    return "UNKNOWN";
}

