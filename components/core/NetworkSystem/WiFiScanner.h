/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// WiFiScanner
//
// Rob Dobson 2018-2022
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <vector>
#include <atomic>
#include "RaftArduino.h"

class WiFiScanner
{
public:
    WiFiScanner();
    virtual ~WiFiScanner();

    // Scan state
    enum class ScanState
    {
        IDLE,       // No scan started yet
        SCANNING,   // Scan in progress (results are from the previous scan, if any)
        DONE,       // Scan completed - results are current
        FAILED      // Scan could not start or did not complete (see error string)
    };

    // Start a scan (main task). Returns false if the scan could not be started (the reason is
    // reported in the status). A start request while a scan is in progress, or within
    // MIN_RESCAN_INTERVAL_MS of a scan completing, is not an error and doesn't start a new scan.
    bool scanStart();

    // Service (main task) - collects the results of a completed scan promptly, as the driver's
    // list may be replaced by a later (e.g. connection) scan
    void loop();

    // Scan complete - called by WiFi event handler (sys_evt task)
    // @param success true if the scan completed successfully
    // @param numFound number of APs found by the driver (may exceed the number retained)
    void scanComplete(bool success, uint16_t numFound);

    // Abandon scan - called when a scan can no longer complete (e.g. WiFi paused) (main task)
    void scanAbandon();

    // Check if scan is in progress
    bool isScanInProgress() const
    {
        return _scanState == ScanState::SCANNING;
    }

    // Get scan status JSON (without braces) - "scan":{...} (main task)
    String getStatusJSON();

    // Get scan status and cached results JSON (without braces) - "scan":{...},"wifi":[...] (main task)
    String getResultsJSON();

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
        bool isNew;         // BSSID not present in the previous completed scan
    } WiFiScanResult;

    // Define WiFiScanResultList
    typedef std::vector<WiFiScanResult> WiFiScanResultList;

private:
    // State and timing - main task only
    ScanState _scanState = ScanState::IDLE;
    uint32_t _scanId = 0;
    uint32_t _scanStartMs = 0;
    uint32_t _scanEndMs = 0;
    String _scanErrStr;

    // Completion reported by the WiFi event handler (sys_evt task) and consumed in loop()
    std::atomic<bool> _completionPending{false};
    std::atomic<bool> _completionSuccess{false};
    std::atomic<uint16_t> _completionNumFound{0};

    // Results of the last completed scan and change counts relative to the scan before it
    WiFiScanResultList _results;
    uint16_t _numFound = 0;
    uint16_t _numNew = 0;
    uint16_t _numLost = 0;
    bool _hasPreviousResults = false;

    // Max scan result size
    static const uint32_t MAX_SCAN_LIST_SIZE = 30;

    // A start request within this time of a scan completing doesn't start a new scan
    static const uint32_t MIN_RESCAN_INTERVAL_MS = 1000;

    // Helpers
    void collectResults(uint16_t numFound);
    static const char* getScanStateStr(ScanState state);
    String getCipherString(uint16_t cipher);
    String getAuthModeString(uint16_t authMode);

    // Debug
    static constexpr const char* MODULE_PREFIX = "WiFiScan";
};
