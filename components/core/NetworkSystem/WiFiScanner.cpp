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
#include "RaftJson.h"

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
// Start scan (main task)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool WiFiScanner::scanStart()
{
    // A scan is already running - report its status rather than restarting it
    if (_scanState == ScanState::SCANNING)
        return true;

    // New scan
    _scanId++;
    _scanStartMs = millis();
    _scanEndMs = _scanStartMs;
    _scanErrStr = "";
    _completionPending = false;

    // State is set before starting as the scan-done event (on the sys_evt task) may occur before
    // esp_wifi_scan_start returns
    _scanState = ScanState::SCANNING;
    esp_err_t err = esp_wifi_scan_start(NULL, false);
    if (err != ESP_OK)
    {
        _scanState = ScanState::FAILED;
        _scanErrStr = (err == ESP_ERR_WIFI_STATE) ? "busy (STA connecting)" : esp_err_to_name(err);
        LOG_W(MODULE_PREFIX, "scanStart failed %s (%d)", esp_err_to_name(err), err);
        return false;
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scan complete - called by WiFi event handler (sys_evt task)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void WiFiScanner::scanComplete(bool success, uint16_t numFound)
{
    _completionSuccess.store(success, std::memory_order_relaxed);
    _completionNumFound.store(numFound, std::memory_order_relaxed);
    _completionPending.store(true, std::memory_order_release);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Abandon scan (main task)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void WiFiScanner::scanAbandon()
{
    _completionPending = false;
    if (_scanState != ScanState::SCANNING)
        return;
    _scanState = ScanState::FAILED;
    _scanEndMs = millis();
    _scanErrStr = "abandoned";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loop (main task) - handle scan completion
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void WiFiScanner::loop()
{
    if (!_completionPending.exchange(false, std::memory_order_acquire))
        return;

    // Ignore completions that are not for a scan we started (e.g. after an abandon)
    if (_scanState != ScanState::SCANNING)
        return;
    _scanEndMs = millis();
    if (!_completionSuccess.load(std::memory_order_relaxed))
    {
        _scanState = ScanState::FAILED;
        _scanErrStr = "aborted";
        return;
    }
    collectResults(_completionNumFound.load(std::memory_order_relaxed));
    _scanState = ScanState::DONE;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Collect results from the driver into the cache and compare with the previous scan
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void WiFiScanner::collectResults(uint16_t numFound)
{
    // Get records - esp_wifi_scan_get_ap_records returns the number copied in num and frees the
    // driver's list (so it must only be called once per scan)
    uint16_t num = MAX_SCAN_LIST_SIZE;
    wifi_ap_record_t *pRecords = (wifi_ap_record_t *)malloc(num * sizeof(wifi_ap_record_t));
    if (pRecords == NULL)
    {
        LOG_E(MODULE_PREFIX, "collectResults malloc failed");
        esp_wifi_clear_ap_list();
        num = 0;
    }
    else if (esp_wifi_scan_get_ap_records(&num, pRecords) != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "collectResults esp_wifi_scan_get_ap_records failed");
        num = 0;
    }

    // Build the new list, marking BSSIDs not seen in the previous scan
    WiFiScanResultList newResults;
    newResults.reserve(num);
    uint16_t numNew = 0;
    for (uint32_t i = 0; (i < num) && (i < MAX_SCAN_LIST_SIZE); i++)
    {
        const wifi_ap_record_t *pRecord = &pRecords[i];
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
        result.isNew = true;
        for (const WiFiScanResult& prev : _results)
        {
            if (prev.bssid == result.bssid)
            {
                result.isNew = false;
                break;
            }
        }
        if (result.isNew)
            numNew++;
        newResults.push_back(result);
    }
    free(pRecords);

    // Count previous BSSIDs no longer present
    uint16_t numLost = 0;
    for (const WiFiScanResult& prev : _results)
    {
        bool found = false;
        for (const WiFiScanResult& cur : newResults)
        {
            if (cur.bssid == prev.bssid)
            {
                found = true;
                break;
            }
        }
        if (!found)
            numLost++;
    }

    // On the first scan everything is new but there is nothing to compare with
    _numNew = _hasPreviousResults ? numNew : 0;
    _numLost = _hasPreviousResults ? numLost : 0;
    if (!_hasPreviousResults)
    {
        for (WiFiScanResult& result : newResults)
            result.isNew = false;
    }
    _hasPreviousResults = true;
    _numFound = numFound;
    _results.swap(newResults);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get status JSON (main task)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String WiFiScanner::getStatusJSON()
{
    // Pick up a completion that happened since the last loop()
    loop();

    uint32_t nowMs = millis();
    String json = R"("scan":{"state":")" + String(getScanStateStr(_scanState)) + R"(","id":)" + String(_scanId);
    if (_scanState == ScanState::SCANNING)
    {
        json += R"(,"elapsedMs":)" + String(nowMs - _scanStartMs);
    }
    else if (_scanState != ScanState::IDLE)
    {
        json += R"(,"durMs":)" + String(_scanEndMs - _scanStartMs);
        json += R"(,"ageMs":)" + String(nowMs - _scanEndMs);
    }
    if (_scanState == ScanState::FAILED)
        json += R"(,"err":")" + _scanErrStr + R"(")";
    if (_hasPreviousResults)
    {
        json += R"(,"count":)" + String((uint32_t)_results.size());
        json += R"(,"found":)" + String(_numFound);
        json += R"(,"new":)" + String(_numNew);
        json += R"(,"lost":)" + String(_numLost);
    }
    json += "}";
    return json;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get status and results JSON (main task)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String WiFiScanner::getResultsJSON()
{
    String json = getStatusJSON() + R"(,"wifi":[)";
    for (uint32_t i = 0; i < _results.size(); i++)
    {
        const WiFiScanResult& result = _results[i];
        if (i > 0)
            json += ",";
        String entry = "{";
        RaftJson::appendStringField(entry, "ssid", result.ssid.c_str());
        RaftJson::appendRawField(entry, "rssi", String(result.rssi).c_str());
        RaftJson::appendRawField(entry, "ch1", String(result.primaryChannel).c_str());
        RaftJson::appendRawField(entry, "ch2", String(result.secondaryChannel).c_str());
        RaftJson::appendStringField(entry, "auth", getAuthModeString(result.authMode).c_str());
        RaftJson::appendStringField(entry, "bssid", result.bssid.c_str());
        RaftJson::appendStringField(entry, "pair", getCipherString(result.pairwiseCipher).c_str());
        RaftJson::appendStringField(entry, "group", getCipherString(result.groupCipher).c_str());
        RaftJson::appendRawField(entry, "new", result.isNew ? "1" : "0");
        entry += "}";
        json += entry;
    }
    json += "]";
    return json;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scan state string
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const char* WiFiScanner::getScanStateStr(ScanState state)
{
    switch (state)
    {
        case ScanState::IDLE: return "idle";
        case ScanState::SCANNING: return "scanning";
        case ScanState::DONE: return "done";
        case ScanState::FAILED: return "failed";
    }
    return "unknown";
}

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

