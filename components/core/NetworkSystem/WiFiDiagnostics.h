/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// WiFiDiagnostics
// WiFi station link diagnostics, recovery and runtime link options
//
// Recovery: the gateway is pinged and if it stops responding while the station is still associated (e.g. an AP
// which has stopped passing data for this station) another AP with the same SSID is joined if one is found (the
// failing AP is avoided for a while) or the station reconnects. If the gateway has never responded since startup
// a longer threshold is used and only a few attempts are made (so a gateway which doesn't respond to pings doesn't
// cause repeated reconnection).
//
// Counts WiFi/IP events, optionally pings the gateway from the device (which shows whether the device can
// still reach the network when other hosts cannot reach it), periodically logs a one-line link summary and
// allows link options (power save, max TX power, PHY protocol, bandwidth) to be changed at runtime.
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <atomic>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "RaftArduino.h"
#include "RaftJsonIF.h"
#include "WiFiScanner.h"

class WiFiDiagnostics
{
public:
    WiFiDiagnostics();
    ~WiFiDiagnostics();

    // Event notifications (called from the event loop task)
    void onWiFiEvent(int32_t eventId, void* pEventData);
    void onGotIP();
    void onLostIP();

    // Service (called from the main task)
    void loop(esp_netif_t* pStaNetIf, bool staConnectedWithIP, WiFiScanner& wifiScanner);

    // Apply options which must be (re)applied after each connection (called when an IP address is obtained)
    void applyConnectionOptions();

    // Set options from JSON (any subset of the fields below), returns false with errStr set on error
    //   "ps": "none" | "min" | "max"       - WiFi power save mode (default none)
    //   "txPwr": dBm (2..20, 0 = default)  - maximum TX power
    //   "proto": "bgn" | "bg" | "b"         - PHY protocols (bg disables 802.11n, including AMPDU)
    //   "bw": 20 | 40                       - bandwidth
    //   "pingMs": ms (0 = off)              - interval of pings from the device to the gateway (default 5000)
    //   "pingRecover": N (0 = off)          - recover after N consecutive gateway ping failures (default 4)
    //   "logS": s (0 = off)                 - interval of the one-line link summary log
    //   "bssid": "xx:xx:xx:xx:xx:xx" | "any" - connect only to the AP with this BSSID (e.g. one mesh node), not persisted
    //   "reconnect": 1                      - disconnect and reconnect now
    //   "simGwFail": 0 | 1 | 2              - (testing) treat gateway ping responses as failures to exercise recovery
    //                                         (1 = after the first response on a connection, 2 = all responses)
    bool setOptions(const RaftJsonIF& options, String& errStr);

    // Get diagnostics as JSON (without braces when includeBraces is false)
    String getJSON(bool includeBraces = true) const;

private:
    // Event counters (written in the event loop task)
    std::atomic<uint32_t> _connects{0};
    std::atomic<uint32_t> _disconnects{0};
    std::atomic<uint32_t> _lastDisconnectReason{0};
    std::atomic<uint32_t> _beaconTimeouts{0};
    std::atomic<uint32_t> _rssiLowEvents{0};
    std::atomic<uint32_t> _gotIPs{0};
    std::atomic<uint32_t> _lostIPs{0};
    std::atomic<uint32_t> _connectedAtMs{0};

    // Recoveries made because the gateway could not be reached
    std::atomic<uint32_t> _pingRecoveries{0};

    // Options
    wifi_ps_type_t _psMode = WIFI_PS_NONE;
    int _txPwrDbm = 0;
    uint8_t _protocolBitmap = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N;
    wifi_bandwidth_t _bandwidth = WIFI_BW20;
    bool _protocolOrBwSet = false;
    static const uint32_t DEFAULT_PING_INTERVAL_MS = 5000;
    static const uint32_t DEFAULT_PING_RECOVER_COUNT = 4;
    uint32_t _pingIntervalMs = DEFAULT_PING_INTERVAL_MS;
    uint32_t _pingRecoverCount = DEFAULT_PING_RECOVER_COUNT;
    uint32_t _logIntervalS = 0;
    uint32_t _logLastMs = 0;

    // Gateway ping (callbacks are made on the ping task)
    void* _pingHandle = nullptr;
    bool _pingActive = false;
    uint32_t _pingStartAttemptMs = 0;
    static const uint32_t PING_START_RETRY_MS = 10000;
    uint32_t _pingActiveIntervalMs = 0;
    String _pingTarget;
    std::atomic<uint32_t> _pingSent{0};
    std::atomic<uint32_t> _pingRecv{0};
    std::atomic<uint32_t> _pingLastRttMs{0};
    std::atomic<uint32_t> _pingMaxRttMs{0};
    std::atomic<uint32_t> _pingConsecFail{0};
    std::atomic<uint32_t> _pingMaxConsecFail{0};
    std::atomic<uint32_t> _pingLastOkMs{0};
    std::atomic<bool> _pingOkThisSession{false};
    std::atomic<bool> _gatewayEverResponded{false};
    uint32_t _unknownGatewayRecoveries = 0;
    static const uint32_t UNKNOWN_GATEWAY_THRESHOLD_MULT = 3;
    static const uint32_t UNKNOWN_GATEWAY_MAX_RECOVERIES = 3;
    std::atomic<uint32_t> _simGatewayFail{0};

    // Recovery
    enum class RecoveryState { IDLE, SCANNING };
    RecoveryState _recoveryState = RecoveryState::IDLE;
    uint32_t _recoveryScanId = 0;
    uint32_t _recoveryStartMs = 0;
    uint32_t _lastRecoveryMs = 0;
    bool _recoveryDone = false;
    uint8_t _failingBssid[6] = {};
    String _failingSsid;
    std::atomic<uint32_t> _apSwitches{0};
    String _lastRecoveryStr;
    static const uint32_t RECOVERY_MIN_INTERVAL_MS = 60000;
    static const uint32_t RECOVERY_SCAN_TIMEOUT_MS = 10000;
    static const int8_t RECOVERY_MIN_ALT_RSSI = -90;

    // APs avoided after a recovery
    struct AvoidedAP
    {
        uint8_t bssid[6] = {};
        uint32_t untilMs = 0;
        bool valid = false;
    };
    static const uint32_t MAX_AVOIDED_APS = 4;
    static const uint32_t AVOID_AP_MS = 600000;
    AvoidedAP _avoidedAPs[MAX_AVOIDED_APS];
    void avoidAP(const uint8_t* pBssid);
    bool isAPAvoided(const uint8_t* pBssid) const;
    uint32_t numAvoidedAPs() const;

    // BSSID lock (set by recovery or manually) - never stored in flash
    std::atomic<bool> _autoLockActive{false};
    std::atomic<bool> _autoLockClearPending{false};
    std::atomic<uint32_t> _autoLockSetMs{0};
    bool _manualLockActive = false;
    uint8_t _lockedBssid[6] = {};
    static const uint32_t AUTO_LOCK_SETTLE_MS = 10000;
    bool setBssidLock(const uint8_t* pBssid, bool reconnect);
    void recoveryStart(WiFiScanner& wifiScanner);
    void recoveryComplete(WiFiScanner& wifiScanner, bool scanOk);
    void startPing(esp_netif_t* pStaNetIf);
    void stopPing();
    static void pingSuccessCB(void* hdl, void* args);
    static void pingTimeoutCB(void* hdl, void* args);

    // Helpers
    void logSummary() const;
    void forceReconnect();
    static const char* psModeStr(wifi_ps_type_t ps);
    static bool parseBssid(const String& bssidStr, uint8_t* pBssid);
    static String bssidToStr(const uint8_t* pBssid);
    static String protocolStr(uint8_t bitmap);
};
