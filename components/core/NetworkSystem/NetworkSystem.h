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
#include "NetworkSettings.h"
#include "sdkconfig.h"

// Check if any ethernet phy type is enabled
#if defined(CONFIG_ETH_USE_ESP32_EMAC) || defined(CONFIG_ETH_USE_SPI_ETHERNET)
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
        return _wifiIPV4Addr;
    }

#ifdef ETHERNET_IS_ENABLED
    String getEthIPV4AddrStr() const
    {
        return _ethIPV4Addr;
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
        return _wifiStaSSID;
    }

    // Get SSID we're trying to connect to
    String getSSIDConnectingTo() const
    {
        return _wifiStaSSIDConnectingTo;
    }

    // Configuration
    bool configWifiSTA(const String& ssid, const String& pw);
    bool configWifiAP(const String& apSsid, const String& apPassword);

    // Clear credentials
    esp_err_t clearCredentials();

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
        isValid = _wifiRSSI != 0;
        return _wifiRSSI;
    }

    // Set log level
    void setLogLevel(esp_log_level_t logLevel);

private:
    // Is setup
    bool _isSetup = false;

    // Settings
    NetworkSettings _networkSettings;

    // Flags
    bool _isPaused = false;

    // WiFi connection details
    String _wifiStaSSID;
    String _wifiIPV4Addr;
    String _hostname;
    bool _wifiStaConnWithIPBeforePause = false;

    // WiFi STA SSID we're trying to connect to
    String _wifiStaSSIDConnectingTo;

    // WiFi AP
    String _wifiAPSSID;
    uint8_t _wifiAPClientCount = 0;

    // RSSI
    int8_t _wifiRSSI = 0;
    uint32_t _wifiRSSILastMs = 0;

    // RSSI check interval
    static const uint32_t WIFI_RSSI_CHECK_MS = 2000;

    // Connect retries
    int _numWifiConnectRetries = 0; 
    uint32_t _lastReconnAttemptMs = 0;

    // Retry max, -1 means try forever
    static const int WIFI_CONNECT_MAX_RETRY = -1;

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

    // ESP netif objects
    esp_netif_t* _pWifiStaNetIf = nullptr;
    esp_netif_t* _pWifiApNetIf = nullptr;

    // Time sync last time
    bool _timeSyncInitialDone = false;
    uint32_t _timeSyncLastMs = 0;
    static const uint32_t TIME_SYNC_INTERVAL_MS = 10*60*60*1000;

    // Helpers
    bool startWifi();
    void stopWifi();
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

    // Debug
    static constexpr const char* MODULE_PREFIX = "NetSys";
};

// Access to single instance
extern NetworkSystem networkSystem;
