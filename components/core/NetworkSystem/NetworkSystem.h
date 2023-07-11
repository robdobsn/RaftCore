/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// NetworkSystem 
// Handles WiFi / Ethernet and IP
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "NetworkSettings.h"
#include "WiFiScanner.h"
#include <ArduinoOrAlt.h>
#include <NetCoreIF.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include <esp_eth_driver.h>
#endif

#include <esp_idf_version.h>
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 3, 0)
// #pragma message "Using V4.3+ WiFi methods"
#define ESP_IDF_WIFI_STA_MODE_FLAG WIFI_IF_STA
#else
#define ESP_IDF_WIFI_STA_MODE_FLAG ESP_IF_WIFI_STA
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0)
#include "esp_netif.h"
#else
#include <tcpip_adapter.h>
#endif

class NetworkSystem : public NetCoreIF
{
public:
    NetworkSystem();

    // Setup 
    bool setup(const NetworkSettings& networkSettings);

    // Service
    void service();

    // Connected indicators
    bool isWifiStaConnectedWithIP();
    bool isWifiAPConnectedWithIP();
    bool isEthConnectedWithIP();
    bool isIPConnected();

    // Get JSON info
    String getSettingsJSON(bool includeBraces);
    String getConnStateJSON(bool includeBraces, bool staInfo, bool apInfo, bool ethInfo, bool useBeforePauseValue);

    // Connection info
    String getWiFiIPV4AddrStr()
    {
        return _wifiIPV4Addr;
    }
    String getEthIPV4AddrStr()
    {
        return _ethIPV4Addr;
    }

    virtual String getHostname() override final
    {
        return _hostname;
    }

    virtual void setHostname(const char* hostname) override final;

    String getSSID()
    {
        return _wifiStaSSID;
    }

    // Configuration
    bool configWifiSTA(const String& ssid, const String& pw);
    bool configWifiAP(const String& apSsid, const String& apPassword);

    // Clear credentials
    esp_err_t clearCredentials();

    // Pause WiFi operation - used to help with BLE vs WiFi contention in ESP32
    // (they share the same radio)
    void pauseWiFi(bool pause);
    bool isPaused()
    {
        return _isPaused;
    }

    // Hostname
    String hostnameMakeValid(const String& hostname);

    // Scan WiFi
    bool wifiScan(bool start, String& jsonResult);

    // Get RSSI
    int getRSSI(bool& isValid)
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
    esp_eth_handle_t _ethernetHandle = nullptr;
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

    // Helpers
    bool startWifi();
    void stopWifi();
    bool startEthernet();
    static void networkEventHandler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* pEventData);
    void wifiEventHandler(void* arg, int32_t event_id, void* pEventData);
    void ethEventHandler(void* arg, int32_t event_id, void* pEventData);
    void ipEventHandler(void* arg, int32_t event_id, void* pEventData);
    void handleWiFiStaDisconnectEvent();
    void warnOnWiFiDisconnectIfEthNotConnected();
};

// Access to single instance
extern NetworkSystem networkSystem;
