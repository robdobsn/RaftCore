/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// NetworkSystem 
// Handles WiFi / Ethernet and IP
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_idf_version.h"
#include "nvs_flash.h"
#include "RaftArduino.h"
#include "WiFiScanner.h"
#include "WiFiDiagnostics.h"
#include "NetworkSettings.h"
#include "sdkconfig.h"
#include "RaftThreading.h"
#include <atomic>

// Check if ethernet is enabled
#if defined(CONFIG_ETH_ENABLED) && CONFIG_ETH_ENABLED
#define ETHERNET_IS_ENABLED
#endif

#ifdef ETHERNET_IS_ENABLED
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "esp_eth_driver.h"
#endif
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 3, 0)
// #pragma message "Using V4.3+ WiFi methods"
#define ESP_IDF_WIFI_STA_MODE_FLAG WIFI_IF_STA
#else
#define ESP_IDF_WIFI_STA_MODE_FLAG ESP_IF_WIFI_STA
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0)
#include "esp_netif.h"
#else
#include "tcpip_adapter.h"
#endif

class NetworkSystem
{
public:
    NetworkSystem();

    // Setup 
    bool setup(const NetworkSettings& networkSettings);

    // WiFi station link diagnostics and runtime link options
    WiFiDiagnostics& getWiFiDiagnostics()
    {
        return _wifiDiagnostics;
    }

    // Service
    void loop();

    // Connected indicators
    bool isWifiStaConnectedWithIP() const;
    bool isEthConnectedWithIP() const;
    bool isIPConnected() const;

    // Get JSON info
    String getSettingsJSON(bool includeBraces) const;
    String getConnStateJSON(bool includeBraces, bool staInfo, bool apInfo, bool ethInfo, bool useBeforePauseValue) const;

    // Connection info
    String getWiFiIPV4AddrStr() const
    {
        return getConnInfoStr(_wifiIPV4Addr);
    }

#ifdef ETHERNET_IS_ENABLED
    String getEthIPV4AddrStr() const
    {
        return getConnInfoStr(_ethIPV4Addr);
    }
#endif

    // Hostname
    String getHostname() const
    {
        return _hostname;
    }
    void setHostname(const char* hostname);

    // SSID
    String getSSID() const
    {
        return getConnInfoStr(_wifiStaSSID);
    }

    // Get SSID we're trying to connect to
    String getSSIDConnectingTo() const
    {
        return _wifiStaSSIDConnectingTo;
    }

    // Configuration
    // skipIfUnchanged: nothing is done (no disconnect and reconnect) if the stored config already has these
    // credentials - for credentials applied from config on each WiFi start
    bool configWifiSTA(const String& ssid, const String& pw, bool skipIfUnchanged = false);
    #if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    bool configWifiAP(const String& apSsid, const String& apPassword);
    #endif

    // Clear credentials
    esp_err_t clearCredentials();

    // Start and stop WiFi on demand (WiFi is started at setup unless NetworkSettings::wifiStartOnDemand)
    // fullScan makes this start forget the fast reconnect AP and scan all channels (of the configured band)
    // for the strongest AP - e.g. after a restart or when connections keep failing
    bool wifiStart(bool fullScan = false);
    void wifiStop();
    bool isWifiStarted() const
    {
        return _wifiRequested;
    }

    // WiFi STA static IP, used from the next WiFi start (see NetworkSettings::wifiStaticIP): an empty ip uses DHCP,
    // an empty netmask 255.255.255.0 and an empty dns the gateway
    void setWifiStaticIP(const String& ip, const String& gateway, const String& netmask, const String& dns);

    // A network connection is expected, so its absence means something is wrong (e.g. for SysManager's
    // reboot if disconnected) - always, as before, except while WiFi is started on demand and is stopped
    bool isNetworkWanted() const
    {
        if (_networkSettings.enableEthernet)
            return true;
        if (_networkSettings.wifiStartOnDemand)
            return _wifiRequested;
        return true;
    }

    // Connection statistics for the latest WiFi start: time from start to IP address (ms, 0 if no IP yet),
    // whether the fast reconnect AP was used and the number of times since boot that it failed
    uint32_t getWifiConnectMs() const
    {
        return _wifiConnectMs;
    }
    bool wasFastReconnectUsed() const
    {
        return _fastReconnectUsedThisStart;
    }
    uint32_t getFastReconnectFallbacks() const
    {
        return _fastReconnectFallbacks;
    }

    // Pause WiFi operation - used to help with BLE vs WiFi contention in ESP32
    // (they share the same radio)
    void pauseWiFi(bool pause);
    bool isPaused() const
    {
        return _isPaused;
    }

    // Hostname
    String hostnameMakeValid(const String& hostname);

    // Scan WiFi
    bool wifiScan(bool start, String& jsonResult);

    // Get RSSI
    int getRSSI(bool& isValid) const
    {
        int rssi = _wifiRSSI;
        isValid = rssi != 0;
        return rssi;
    }

    // Set log level
    void setLogLevel(esp_log_level_t logLevel);

private:
    // Is setup
    bool _isSetup = false;

    // Settings
    NetworkSettings _networkSettings;

    // Flags
    // Note that _isPaused is set on the main task and read in event handlers (sys_evt task)
    std::atomic<bool> _isPaused{false};

    // Connection info strings which are written in event handlers (sys_evt task) and read on the main task
    // These MUST only be accessed using getConnInfoStr() / setConnInfoStr() which use _connInfoMutex
    // (_wifiStaSSID, _wifiIPV4Addr, _ethIPV4Addr, _ethMACAddress)
    mutable RaftMutex _connInfoMutex;
    String getConnInfoStr(const String& connInfoStr) const;
    void setConnInfoStr(String& connInfoStr, const String& newValue);

    // WiFi connection details
    String _wifiStaSSID;
    String _wifiIPV4Addr;
    String _hostname;
    bool _wifiStaConnWithIPBeforePause = false;

    // WiFi STA SSID we're trying to connect to
    String _wifiStaSSIDConnectingTo;

    // WiFi AP
    String _wifiAPSSID;
    std::atomic<uint8_t> _wifiAPClientCount{0};

    // RSSI
    std::atomic<int8_t> _wifiRSSI{0};
    uint32_t _wifiRSSILastMs = 0;

    // RSSI check interval
    static const uint32_t WIFI_RSSI_CHECK_MS = 2000;

    // Connect retries
    // (these are accessed from both event handlers (sys_evt task) and the main task)
    std::atomic<int> _numWifiConnectRetries{0};
    uint32_t _lastReconnAttemptMs = 0;
    std::atomic<bool> _pendingWiFiDisconnectWarn{false};
    std::atomic<int> _pendingWiFiDisconnectWarnRetries{0};
    std::atomic<int> _lastWiFiDisconnectReason{0};

    // WiFi event handler instances
    esp_event_handler_instance_t _wifiEventHandlerInstance = nullptr;
    esp_event_handler_instance_t _ipGotEventHandlerInstance = nullptr;
    esp_event_handler_instance_t _ipLostEventHandlerInstance = nullptr;

    // Suppress auto-reconnect while a new STA config is being applied
    std::atomic<bool> _suppressWifiAutoReconnect{false};

    // Auto-reconnect is paused while a scan runs when the STA isn't connected (a connection attempt
    // in progress prevents scanning) - main task only
    bool _reconnectPausedForScan = false;
    uint32_t _reconnectPausedForScanMs = 0;
    static const uint32_t SCAN_RECONNECT_PAUSE_MAX_MS = 15000;
    static const uint32_t SCAN_START_BUSY_RETRY_MS = 2000;
    void resumeReconnectAfterScan();

    // Retry max, -1 means try forever
    static const int WIFI_CONNECT_MAX_RETRY = -1;

    // WiFi wanted (started at setup or by wifiStart(), stopped by wifiStop()) - WiFi may still be stopped
    // while paused
    std::atomic<bool> _wifiRequested{false};
    bool _fullScanThisStart = false;
    void applyWifiStartOptions();

    // Connection statistics (written on the WiFi event task)
    std::atomic<uint32_t> _wifiStartMs{0};
    std::atomic<uint32_t> _wifiConnectMs{0};
    std::atomic<bool> _fastReconnectUsedThisStart{false};
    std::atomic<uint32_t> _fastReconnectFallbacks{0};

    // Fast reconnect (see NetworkSettings::wifiSTAFastReconnect) - true while the STA config (in RAM) is set to
    // connect to the remembered AP
    std::atomic<bool> _fastReconnectHintInUse{false};
    void applyStaFastReconnectHint(const wifi_config_t& staConfig);
    void storeStaFastReconnectHint(const wifi_event_sta_connected_t* pEvent);
    void clearStaFastReconnectHint();
    void applyStaFullScan(const wifi_config_t& staConfig);

    // WiFi band (see NetworkSettings::wifiBand)
    void applyWifiBandMode();

    // WiFi STA static IP (see NetworkSettings::wifiStaticIP)
    bool _wifiStaticIPInUse = false;
    bool applyStaStaticIP();

    // Ethernet
#ifdef ETHERNET_IS_ENABLED
    esp_eth_handle_t _ethernetHandle = nullptr;
#endif
    String _ethIPV4Addr;
    String _ethMACAddress;

    // FreeRTOS event group to signal when we are connected
    // Code here is based on https://github.com/espressif/esp-idf/blob/master/examples/wifi/getting_started/station/main/station_example_main.c
    EventGroupHandle_t _networkRTOSEventGroup = nullptr;

    // The event group allows multiple bits for each event
    static const uint32_t WIFI_STA_CONNECTED_BIT = BIT0;
    static const uint32_t WIFI_STA_IP_CONNECTED_BIT = BIT1;
    static const uint32_t WIFI_STA_FAIL_BIT = BIT2;
    static const uint32_t ETH_CONNECTED_BIT = BIT3;
    static const uint32_t ETH_IP_CONNECTED_BIT = BIT4;

    // WiFi Scanner
    WiFiScanner _wifiScanner;

    // WiFi diagnostics
    WiFiDiagnostics _wifiDiagnostics;

    // ESP netif objects
    esp_netif_t* _pWifiStaNetIf = nullptr;
    esp_netif_t* _pWifiApNetIf = nullptr;

    // Time sync last time
    bool _timeSyncInitialDone = false;
    uint32_t _timeSyncLastMs = 0;
    static const uint32_t TIME_SYNC_INTERVAL_MS = 10*60*60*1000;
    // Flag set from the SNTP sync_cb (lwIP tcpip thread) and consumed from
    // NetworkSystem::loop() to dispatch RaftSystemTime::notifyChanged("sntp")
    // on the main task.
    static volatile bool _sntpSyncPendingNotify;

    // Deferred mDNS setup (done in loop rather than event handler)
    // The flag is set in event handlers (sys_evt task) and the time MUST be written before the flag
    std::atomic<bool> _mdnsSetupPending{false};
    std::atomic<uint32_t> _mdnsSetupPendingMs{0};
    bool _mdnsIsSetup = false;
    static const uint32_t MDNS_SETUP_DELAY_MS = 500;

    // Helpers
    bool startWifi();
    void stopWifi();

    // STA AP selection from settings (see NetworkSettings::wifiSTAConnectBySignal)
    wifi_scan_method_t getWifiSTAScanMethod() const
    {
        return _networkSettings.wifiSTAConnectBySignal ? WIFI_ALL_CHANNEL_SCAN : WIFI_FAST_SCAN;
    }
    wifi_sort_method_t getWifiSTASortMethod() const
    {
        // Sorting only applies to an all-channel scan; BY_SIGNAL is also the
        // ESP-IDF zero default, so fast-scan behaviour is unchanged
        return WIFI_CONNECT_AP_BY_SIGNAL;
    }
    static void networkEventHandler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* pEventData);
    void wifiEventHandler(void* arg, int32_t event_id, void* pEventData);
#ifdef ETHERNET_IS_ENABLED
    bool startEthernet();
    void ethEventHandler(void* arg, int32_t event_id, void* pEventData);
#endif
    void ipEventHandler(void* arg, int32_t event_id, void* pEventData);
    void handleWiFiStaDisconnectEvent();
    void warnOnWiFiDisconnectIfEthNotConnected();
    void setupMDNS();
    void requestMDNSSetup();
    void setupNetBIOS(const String& netbiosName, bool init);

    // Debug
    static constexpr const char* MODULE_PREFIX = "NetSys";
};

// Access to single instance
extern NetworkSystem networkSystem;
