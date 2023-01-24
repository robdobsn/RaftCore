/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// WiFiScanner
//
// Rob Dobson 2018-2022
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <vector>
#include <ArduinoOrAlt.h>

class WiFiScanner
{
public:
    WiFiScanner();
    virtual ~WiFiScanner();

    // Scan
    bool scanStart();

    // Define WiFiScanResult
    typedef struct
    {
        String ssid;
        int8_t rssi;
        uint8_t primaryChannel;
        uint8_t secondaryChannel;
        uint8_t authMode;
        String bssid;
        uint8_t pairwiseCipher;
        uint8_t groupCipher;
    } WiFiScanResult;

    // Define WiFiScanResultList
    typedef std::vector<WiFiScanResult> WiFiScanResultList;

    // Get scan results
    bool getScanResults(WiFiScanResultList& results);

    // Get results JSON
    bool getResultsJSON(String& json);

    // Scan complete - called by WiFi event handler
    void scanComplete();

    // Check if scan is in progress
    bool isScanInProgress()
    {
        return _scanInProgress;
    }

private:
    // Scan in progress
    bool _scanInProgress;

    // Max scan result size
    static const uint32_t MAX_SCAN_LIST_SIZE = 30;

    // Helpers
    String getCipherString(uint16_t cipher);
    String getAuthModeString(uint16_t authMode);
};

