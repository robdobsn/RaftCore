/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// NetworkSystem 
// Handles WiFi / Ethernet and IP
//
// Rob Dobson 2018-2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "WiFiScanner.h"
#include <ArduinoOrAlt.h>
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

// #define PRIVATE_EVENT_LOOP

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

class NetworkSystem
{
public:
    NetworkSystem();

    // Ethernet types
    enum EthLanChip
    {
        ETH_CHIP_TYPE_NONE,
        ETH_CHIP_TYPE_LAN87XX
    };

    // Ethernet config
    class EthSettings
    {
    public:
        EthSettings(const char* pEthLanChip, int powerPin, int smiMDCPin, int smiMDIOPin, int phyAddr, int phyRstPin)
            :   _ethLanChip(getChipEnum(pEthLanChip)), _powerPin(powerPin), _smiMDCPin(smiMDCPin), 
                _smiMDIOPin(smiMDIOPin), _phyAddr(phyAddr), _phyRstPin(phyRstPin)
        {
        }
        EthSettings(EthLanChip ethLanChip, int powerPin, int smiMDCPin, int smiMDIOPin, int phyAddr, int phyRstPin)
            :   _ethLanChip(ethLanChip), _powerPin(powerPin), _smiMDCPin(smiMDCPin), 
                _smiMDIOPin(smiMDIOPin), _phyAddr(phyAddr), _phyRstPin(phyRstPin)
        {
        }
        EthLanChip _ethLanChip = ETH_CHIP_TYPE_NONE;
        int _powerPin = -1;
        int _smiMDCPin = -1;
        int _smiMDIOPin = -1;
        int _phyAddr = 0;
        int _phyRstPin = -1;
    private:
        EthLanChip getChipEnum(const char* pEthLanChip)
        {
            if (strcmp(pEthLanChip, "LAN87XX") == 0)
                return ETH_CHIP_TYPE_LAN87XX;
            return ETH_CHIP_TYPE_NONE;
        }
    };

    // Setup 
    void setup(bool enableWiFi, bool enableEthernet, const char* hostname, 
                    bool enWifiSTAMode, bool enWiFiAPMode, bool ethWiFiBridge, EthSettings* pEthSettings=nullptr);

    // Service
    void service();

    bool isWiFiEnabled()
    {
        return _isWiFiEnabled;
    }
    bool isEthEnabled()
    {
        return _isEthEnabled;
    }
    // Station mode connected / eth connected
    bool isWiFiStaConnectedWithIP();
    bool isEthConnectedWithIP()
    {
        return _ethConnected;
    }

    // Is IP connected
    bool isIPConnected();

    // Connection info
    String getWiFiIPV4AddrStr()
    {
        return _wifiIPV4Addr;
    }
    String getEthIPV4AddrStr()
    {
        return _ethIPV4Addr;
    }

    String getHostname()
    {
        return _hostname;
    }

    void setHostname(const char* hostname);

    String getSSID()
    {
        return _staSSID;
    }

    bool configureWiFi(const String& ssid, const String& pw, const String& hostname, const String& apSsid, const String& apPassword);

    // Connection codes
    enum ConnStateCode
    {
        CONN_STATE_NONE,
        CONN_STATE_WIFI_BUT_NO_IP,
        CONN_STATE_WIFI_AND_IP
    };
    static String getConnStateCodeStr(ConnStateCode connStateCode);

    // Get conn state code
    ConnStateCode getConnState();

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
        isValid = _rssi != 0;
        return _rssi;
    }

private:

    // Enabled
    bool _isWiFiEnabled = false;
    bool _isEthEnabled = false;
    static bool _isPaused;

    // Wifi modes
    bool _enWifiSTAMode = false;
    bool _enWifiAPMode = false;

    // Started flags
    bool _netifStarted = false;
    bool _wifiSTAStarted = false;
    bool _wifiAPStarted = false;

    // WiFi connection details
    static String _staSSID;
    static String _wifiIPV4Addr;
    String _hostname;
    String _defaultHostname;

    // WiFi AP client count
    static uint8_t _wifiAPClientCount;

    // Ethernet - WiFi bridge
    static bool _ethWiFiBridge;
    static const uint32_t FLOW_CONTROL_QUEUE_TIMEOUT_MS = 100;
    static const uint32_t FLOW_CONTROL_WIFI_SEND_TIMEOUT_MS  = 100;
    static const uint32_t FLOW_CONTROL_QUEUE_LENGTH = 100;
    static QueueHandle_t _ethWiFiBridgeFlowControlQueue;

    // Flow control message
    typedef struct {
        void *packet;
        uint16_t length;
    } flow_control_msg_t;

    // RSSI
    int8_t _rssi = 0;
    uint32_t _rssiLastMs = 0;

    // RSSI check interval
    static const uint32_t RSSI_CHECK_MS = 2000;

    // Connect retries
    static int _numConnectRetries; 

    // Retry max, -1 means try forever
    static const int WIFI_CONNECT_MAX_RETRY = -1;

    // Ethernet
    static bool _ethConnected;
    static esp_eth_handle_t _ethernetHandle;
    static String _ethIPV4Addr;

#ifdef PRIVATE_EVENT_LOOP
    // Event loop for WiFi connection
    esp_event_loop_handle_t _wifiEventLoopHandle;
    static const int CONFIG_WIFI_PRIVATE_EVENT_LOOP_CORE = 1;
#endif

    // FreeRTOS event group to signal when we are connected
    // Code here is based on https://github.com/espressif/esp-idf/blob/master/examples/wifi/getting_started/station/main/station_example_main.c
    static EventGroupHandle_t _wifiRTOSEventGroup;

    // The event group allows multiple bits for each event
    static const uint32_t WIFI_CONNECTED_BIT = BIT0;
    static const uint32_t IP_CONNECTED_BIT = BIT1;
    static const uint32_t WIFI_FAIL_BIT = BIT2;

    // WiFi Scanner
    WiFiScanner _wifiScanner;

    // Helpers
    bool startWifi();
    static void wifiEventHandler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data);
    static void ethEventHandler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data);
    static void ethGotIPEvent(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data);
    
    // WiFi - Ethernet bridge helpers
    static esp_err_t initEthWiFiBridgeFlowControl(void);
    static void ethWiFiFlowCtrlTask(void *args);
    static esp_err_t wifiRxPacketCallback(void *buffer, uint16_t len, void *eb);
    static esp_err_t ethRxPacketCallback(esp_eth_handle_t eth_handle, uint8_t *buffer, uint32_t len, void *priv);

};

extern NetworkSystem networkSystem;
