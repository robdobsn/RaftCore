/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// WiFiDiagnostics
// WiFi station link diagnostics and runtime link options
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
    void loop(esp_netif_t* pStaNetIf, bool staConnectedWithIP);

    // Apply options which must be (re)applied after each connection (called when an IP address is obtained)
    void applyConnectionOptions();

    // Set options from JSON (any subset of the fields below), returns false with errStr set on error
    //   "ps": "none" | "min" | "max"       - WiFi power save mode (default none)
    //   "txPwr": dBm (2..20, 0 = default)  - maximum TX power
    //   "proto": "bgn" | "bg" | "b"         - PHY protocols (bg disables 802.11n, including AMPDU)
    //   "bw": 20 | 40                       - bandwidth
    //   "pingMs": ms (0 = off)              - interval of pings from the device to the gateway
    //   "pingRecover": N (0 = off)          - reconnect after N consecutive gateway ping failures
    //   "logS": s (0 = off)                 - interval of the one-line link summary log
    //   "reconnect": 1                      - disconnect and reconnect now
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
    uint32_t _pingIntervalMs = 0;
    uint32_t _pingRecoverCount = 0;
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
    void startPing(esp_netif_t* pStaNetIf);
    void stopPing();
    static void pingSuccessCB(void* hdl, void* args);
    static void pingTimeoutCB(void* hdl, void* args);

    // Helpers
    void logSummary() const;
    void forceReconnect();
    static const char* psModeStr(wifi_ps_type_t ps);
    static String protocolStr(uint8_t bitmap);
};
