/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// NetworkSystem 
// Handles WiFi / Ethernet and IP
//
// Rob Dobson 2018-2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <Logger.h>
#include "NetworkSystem.h"
#include <mdns.h>
#include <RaftUtils.h>
#include <ArduinoOrAlt.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_private/wifi.h"

static const char* MODULE_PREFIX = "NetworkSystem";

// Global object
NetworkSystem networkSystem;

// Statics
int NetworkSystem::_numConnectRetries = 0;
EventGroupHandle_t NetworkSystem::_wifiRTOSEventGroup;
String NetworkSystem::_staSSID;
String NetworkSystem::_wifiIPV4Addr;
bool NetworkSystem::_ethConnected = false;
String NetworkSystem::_ethIPV4Addr;
uint8_t NetworkSystem::_wifiAPClientCount = false;
QueueHandle_t NetworkSystem::_ethWiFiBridgeFlowControlQueue = nullptr;
bool NetworkSystem::_ethWiFiBridge = false;
esp_eth_handle_t NetworkSystem::_ethernetHandle = nullptr;

// Paused
bool NetworkSystem::_isPaused = false;

// #define DEBUG_RSSI_GET_TIME
#define DEBUG_HOSTNAME_SETTING

#ifdef ETHERNET_HARDWARE_OLIMEX
#define ETHERNET_IS_SUPPORTED
#define	ETH_PIN_PHY_POWER	12
#define	ETH_PIN_SMI_MDC		23
#define	ETH_PIN_SMI_MDIO	18
#define ETH_PHY_LAN87XX
#define ETH_PHY_ADDR 0
#define ETH_PHY_RST_GPIO -1
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NetworkSystem::NetworkSystem()
{
#ifdef PRIVATE_EVENT_LOOP
    // RTOS event loop
    _wifiEventLoopHandle = NULL;
#endif

    // Create the default event loop
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        LOG_E(MODULE_PREFIX, "failed to create default event loop");
    }

    // Create an RTOS event group to record events
    _wifiRTOSEventGroup = xEventGroupCreate();

    // Not initially connected
    xEventGroupClearBits(_wifiRTOSEventGroup, WIFI_CONNECTED_BIT);
    xEventGroupClearBits(_wifiRTOSEventGroup, IP_CONNECTED_BIT);
    xEventGroupClearBits(_wifiRTOSEventGroup, WIFI_FAIL_BIT);

    // WiFi log level
    esp_log_level_set("wifi", ESP_LOG_WARN);    
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void NetworkSystem::setup(bool enableWiFi, bool enableEthernet, const char* defaultHostname, 
        bool enWifiSTAMode, bool enWiFiAPMode, bool ethWiFiBridge)
{
    _defaultHostname = defaultHostname;
    // Start netif if not done already
    if ((enableWiFi || enableEthernet) && (!_netifStarted))
    {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0)
        if (esp_netif_init() == ESP_OK)
            _netifStarted = true;
        else
            LOG_E(MODULE_PREFIX, "setup could not start netif");
#else
        tcpip_adapter_init();
        _netifStarted = true;
#endif
    }
    _isEthEnabled = enableEthernet;
    _isWiFiEnabled = enableWiFi;
    _enWifiSTAMode = enWifiSTAMode && enableWiFi;
    _enWifiAPMode = enWiFiAPMode && enableWiFi;

    // Only enable bridge if both WiFi and Ethernet are enabled and WiFi is in AP mode
#ifdef ETHERNET_IS_SUPPORTED
    _ethWiFiBridge = ethWiFiBridge && enableWiFi && enableEthernet && _enWifiAPMode;
#else
    _ethWiFiBridge = false;
#endif

    // Debug
    LOG_I(MODULE_PREFIX, "setup wifiMode %s defaultHostname %s AP %d STA %d", 
                (_enWifiAPMode && _enWifiSTAMode) ? "AP+STA" : (_enWifiAPMode ? "AP" : (_enWifiSTAMode ? "STA" : "OFF")),
                ethWiFiBridge ? " EthWiFiBridge": "", defaultHostname, enWiFiAPMode, enWifiSTAMode);

    // Start WiFi STA if required
    if (_netifStarted && _isWiFiEnabled)
    {
        // Start Wifi
        startWifi();

        // Check STA mode
        if (_enWifiSTAMode)
        {
        // Connect
        if (!isWiFiStaConnectedWithIP())
        {
            wifi_config_t configFromNVS;
            esp_err_t err = esp_wifi_get_config(ESP_IDF_WIFI_STA_MODE_FLAG, &configFromNVS);
            if (err == ESP_OK)
            {
                esp_wifi_set_config(ESP_IDF_WIFI_STA_MODE_FLAG, &configFromNVS);
                configFromNVS.sta.ssid[sizeof(configFromNVS.sta.ssid)-1] = 0;
                LOG_I(MODULE_PREFIX, "setup from NVS ... connect to ssid %s", configFromNVS.sta.ssid);
            }
            else
            {
                LOG_W(MODULE_PREFIX, "setup failed to get config from NVS");
            }
            esp_wifi_connect();
        }
        _isPaused = false;
    }
    }

#ifdef ETHERNET_HARDWARE_OLIMEX
    if (_netifStarted && _isEthEnabled)
    {
        // Create ethernet event loop that running in background
        esp_netif_t *pEthNetif = nullptr;
        _ethernetHandle = nullptr;
        // Create new default instance of esp-netif for Ethernet
        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
        pEthNetif = esp_netif_new(&cfg);

        // Init MAC and PHY configs to default
        eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
        eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

        phy_config.phy_addr = ETH_PHY_ADDR;
        phy_config.reset_gpio_num = ETH_PHY_RST_GPIO;
        gpio_pad_select_gpio((gpio_num_t) ETH_PIN_PHY_POWER);
        gpio_set_direction((gpio_num_t) ETH_PIN_PHY_POWER, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t) ETH_PIN_PHY_POWER, 1);
        vTaskDelay(pdMS_TO_TICKS(10));

        // Create Ethernet driver
        mac_config.smi_mdc_gpio_num = (gpio_num_t) ETH_PIN_SMI_MDC;
        mac_config.smi_mdio_gpio_num = (gpio_num_t) ETH_PIN_SMI_MDIO;
        esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&mac_config);
    #if defined(ETH_PHY_IP101)
        esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);
    #elif defined(ETH_PHY_RTL8201)
        esp_eth_phy_t *phy = esp_eth_phy_new_rtl8201(&phy_config);
    #elif defined(ETH_PHY_LAN87XX)
        esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_config);
    #elif defined(ETH_PHY_DP83848)
        esp_eth_phy_t *phy = esp_eth_phy_new_dp83848(&phy_config);
    #elif defined(ETH_PHY_KSZ8041)
        esp_eth_phy_t *phy = esp_eth_phy_new_ksz8041(&phy_config);
    #elif defined(ETH_PHY_KSZ8081)
        esp_eth_phy_t *phy = esp_eth_phy_new_ksz8081(&phy_config);
    #endif
        esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);

        // Install Ethernet driver
        esp_err_t err = esp_eth_driver_install(&config, &_ethernetHandle);

        // Check driver ok
        if (err != ESP_OK)
        {
            LOG_W(MODULE_PREFIX, "setup failed to install eth driver");
        }
        else if (pEthNetif)
        {
            // Attach Ethernet driver to Ethernet netif
            err = esp_netif_attach(pEthNetif, esp_eth_new_netif_glue(_ethernetHandle));
        }
        else
        {
            LOG_W(MODULE_PREFIX, "setup failed to create netif for ethernet");
            err = ESP_FAIL;
        }

        // Check ok
        if ((err == ESP_OK) && _ethWiFiBridge)
        {
            // Update input path for ethernet packets
            err = esp_eth_update_input_path(_ethernetHandle, ethRxPacketCallback, NULL);

            // Check ok 
            if (err != ESP_OK)
            {
                LOG_W(MODULE_PREFIX, "setup failed to update ethernet input path");
            }
            else
            {
                // Set promiscuous mode
                bool ethPromiscuous = true;
                err = esp_eth_ioctl(_ethernetHandle, ETH_CMD_S_PROMISCUOUS, &ethPromiscuous);
                if (err != ESP_OK)
                {
                    LOG_W(MODULE_PREFIX, "setup failed to set promiscuous mode");
                }
                else
                {
                    LOG_I(MODULE_PREFIX, "setup set input path for WiFi<->Ethernet bridge OK");
                }
            }
        }
        else
        {
            LOG_W(MODULE_PREFIX, "setup failed to attach eth driver to netif");
        }

        // Check attach ok
        if (err != ESP_OK)
        {
            LOG_W(MODULE_PREFIX, "setup failed to attach eth driver to netif");
        }
        else
        {
            // Register user defined event handers
            err = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &NetworkSystem::ethEventHandler, NULL);
            if (err == ESP_OK)
                err = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &NetworkSystem::ethGotIPEvent, NULL);
        }

        // Check event handler ok
        if (err != ESP_OK)
        {
            LOG_W(MODULE_PREFIX, "setup failed to start eth driver");
        }
        if (err == ESP_OK)
        {
            // Start Ethernet driver state machine
            err = esp_eth_start(_ethernetHandle);
        }

        // Check for error
        if (err != ESP_OK)
        {
            LOG_W(MODULE_PREFIX, "setup failed to start eth driver");
        }
    }
#endif

#ifdef ETHERNET_IS_SUPPORTED
    // Check for Ethernet to WiFi bridge
    if (_ethWiFiBridge)
    {
        initEthWiFiBridgeFlowControl();
    }
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void NetworkSystem::service()
{
    // Get RSSI value if connected
    // Don't set RSSI_CHECK_MS too low as getting AP info taked ~2ms
    if (Raft::isTimeout(millis(), _rssiLastMs, RSSI_CHECK_MS))
    {
        _rssiLastMs = millis();
        _rssi = 0;
        if (getConnState() != CONN_STATE_NONE)
        {
#ifdef DEBUG_RSSI_GET_TIME
            uint64_t startUs = micros();
#endif
            wifi_ap_record_t ap;
            esp_err_t rslt = esp_wifi_sta_get_ap_info(&ap);
            _rssi = ap.rssi;
#ifdef DEBUG_RSSI_GET_TIME
            uint64_t endUs = micros();
            LOG_I(MODULE_PREFIX, "service get RSSI %d us", (int)(endUs - startUs));
#endif
            if (rslt != ESP_OK)
            {
                _rssi = 0;
                // Debug
                // LOG_W(MODULE_PREFIX, "service get RSSI failed");
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Status
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool NetworkSystem::isWiFiStaConnectedWithIP()
{
    // Use the clear bits function with nothing to clear as a way to get the current value
    uint connBits = xEventGroupClearBits(_wifiRTOSEventGroup, 0);
    return (connBits & WIFI_CONNECTED_BIT) && (connBits & IP_CONNECTED_BIT);
}

bool NetworkSystem::isTCPIPConnected()
{
    // TODO V4.1 add ethernet support when available in netif
    // Use the clear bits function with nothing to clear as a way to get the current value
    uint connBits = xEventGroupClearBits(_wifiRTOSEventGroup, 0);
    return (connBits & IP_CONNECTED_BIT);
}

// Connection codes
enum ConnStateCode
{
    CONN_STATE_NONE,
    CONN_STATE_WIFI_BUT_NO_IP,
    CONN_STATE_WIFI_AND_IP
};

String NetworkSystem::getConnStateCodeStr(NetworkSystem::ConnStateCode connStateCode)
{
    switch(connStateCode)
    {
        case CONN_STATE_WIFI_BUT_NO_IP: return "WiFiNoIP";
        case CONN_STATE_WIFI_AND_IP: return "WiFiAndIP";
        case CONN_STATE_NONE: 
        default: 
            return "None";
    }
}

// Get conn state code
NetworkSystem::ConnStateCode NetworkSystem::getConnState()
{
    // Use the clear bits function with nothing to clear as a way to get the current value
    uint connBits = xEventGroupClearBits(_wifiRTOSEventGroup, 0);
    if ((connBits & WIFI_CONNECTED_BIT) && (connBits & IP_CONNECTED_BIT))
        return CONN_STATE_WIFI_AND_IP;
    else if (connBits & WIFI_CONNECTED_BIT)
        return CONN_STATE_WIFI_BUT_NO_IP;
    return CONN_STATE_NONE;    
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// WiFi Event handler
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void NetworkSystem::wifiEventHandler(void *arg, esp_event_base_t event_base,
                                      int32_t event_id, void *event_data)
{
    LOG_I(MODULE_PREFIX, "============================= WIFI EVENT base %d id %d", (int)event_base, event_id);
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
            case WIFI_EVENT_STA_START:
    {
        LOG_I(MODULE_PREFIX, "WiFi STA_START ... calling connect");
        esp_wifi_connect();
        LOG_I(MODULE_PREFIX, "WiFi STA_START ... connect returned");
                break;
    }
            case WIFI_EVENT_STA_CONNECTED:
    {
        wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
        uint32_t ssidLen = event->ssid_len;
        if (ssidLen > 32)
            ssidLen = 32;
        char ssidStr[33];
        strlcpy(ssidStr, (const char*)(event->ssid), ssidLen+1);
        _staSSID = ssidStr;
        xEventGroupSetBits(_wifiRTOSEventGroup, WIFI_CONNECTED_BIT);

        // Set hostname using mDNS service
        esp_err_t espErr = mdns_init();
        if (espErr == ESP_OK)
        {
            LOG_I(MODULE_PREFIX, "WiFi MDNS initialization %s", 
                            networkSystem.getHostname().c_str());
            // Set hostname
            mdns_hostname_set(networkSystem.getHostname().c_str());
        }
                break;
    }
            case WIFI_EVENT_STA_DISCONNECTED:
    {
        // Handle pause
        if (!_isPaused)
        {
            if ((WIFI_CONNECT_MAX_RETRY < 0) || (_numConnectRetries < WIFI_CONNECT_MAX_RETRY))
            {
                LOG_W(MODULE_PREFIX, "WiFi disconnected, retry to connect to the AP retries %d", _numConnectRetries);
                if (esp_wifi_disconnect() == ESP_OK)
                {
                    esp_wifi_connect();
                }
                _numConnectRetries++;
            }
            else
            {
                xEventGroupSetBits(_wifiRTOSEventGroup, WIFI_FAIL_BIT);
            }
            LOG_W(MODULE_PREFIX, "WiFi disconnected, connect failed");
            _staSSID = "";
        }
        xEventGroupClearBits(_wifiRTOSEventGroup, WIFI_CONNECTED_BIT);
                break;
    }
            case WIFI_EVENT_SCAN_DONE:
    {
        // LOG_W(MODULE_PREFIX, "scan complete");
        networkSystem._wifiScanner.scanComplete();
                break;
            }
            case WIFI_EVENT_AP_STACONNECTED:
            {
                wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
                LOG_I(MODULE_PREFIX, "WiFi AP client join MAC " MACSTR " aid %d", MAC2STR(event->mac), event->aid);
#ifdef ETHERNET_IS_SUPPORTED
                if (_wifiAPClientCount == 0)
                {
                    // Register receive callback
                    if (_ethWiFiBridge)
                    {
                        esp_wifi_internal_reg_rxcb(WIFI_IF_AP, wifiRxPacketCallback);
                        LOG_I(MODULE_PREFIX, "WiFi AP registered receive callback");
                    }
                }
#endif
                _wifiAPClientCount++;
                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED:
            {
                wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
                LOG_I(MODULE_PREFIX, "WiFi AP client leave MAC " MACSTR " aid %d", MAC2STR(event->mac), event->aid);
                _wifiAPClientCount--;
#ifdef ETHERNET_IS_SUPPORTED                
                if (_wifiAPClientCount == 0)
                {
                    // Unregister receive callback
                    if (_ethWiFiBridge)
                        esp_wifi_internal_reg_rxcb(WIFI_IF_AP, NULL);
                }
#endif
                break;
            }
        }
    }
    else if (event_base == IP_EVENT)
    {
        switch (event_id)
        {
            case IP_EVENT_STA_GOT_IP:
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        LOG_I(MODULE_PREFIX, "WiFi got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        _numConnectRetries = 0;
        char ipAddrStr[32];
        sprintf(ipAddrStr, IPSTR, IP2STR(&event->ip_info.ip));
        _wifiIPV4Addr = ipAddrStr;
        xEventGroupSetBits(_wifiRTOSEventGroup, IP_CONNECTED_BIT);
                break;
    }
            case IP_EVENT_STA_LOST_IP:
    {
        LOG_W(MODULE_PREFIX, "WiFi lost ip");
        if (!_isPaused)
            _wifiIPV4Addr = "";
        xEventGroupClearBits(_wifiRTOSEventGroup, IP_CONNECTED_BIT);
                break;
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ethernet Event handler
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void NetworkSystem::ethEventHandler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t ethHandle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(ethHandle, ETH_CMD_G_MAC_ADDR, mac_addr);
        LOG_I(MODULE_PREFIX, "Ethernet Link up HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        _ethConnected = true;
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        LOG_I(MODULE_PREFIX, "Ethernet Link Down");
        _ethConnected = false;
        break;
    case ETHERNET_EVENT_START:
        LOG_I(MODULE_PREFIX, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        LOG_I(MODULE_PREFIX, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ethernet got IP event handler
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void NetworkSystem::ethGotIPEvent(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    LOG_I(MODULE_PREFIX, "eth got IP event: " IPSTR, IP2STR(&ip_info->ip));
    char ipAddrStr[32];
    sprintf(ipAddrStr, IPSTR, IP2STR(&event->ip_info.ip));
    _ethIPV4Addr = ipAddrStr;
    // ESP_LOGI(MODULE_PREFIX, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    // ESP_LOGI(MODULE_PREFIX, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start WiFi
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool NetworkSystem::startWifi()
{
    // Check if already initialised (can't re-initialise)
    if (_wifiSTAStarted || _wifiAPStarted)
        return true;

#ifdef PRIVATE_EVENT_LOOP
    // Create event loop if not already there
    if (!_wifiEventLoopHandle)
    {
        esp_event_loop_args_t eventLoopArgs = {
            .queue_size = 10,
            .task_name = "WiFiEvLp",
            .task_priority = configMAX_PRIORITIES,
            .task_stack_size = 2048,
            .task_core_id = CONFIG_WIFI_PRIVATE_EVENT_LOOP_CORE,
        };
        esp_err_t err = esp_event_loop_create(&eventLoopArgs, &_wifiEventLoopHandle);
        if (err != ESP_OK)
        {
            LOG_E(MODULE_PREFIX, "startWifi FAILED to start event loop");
            return false;
        }            
    }
#endif

    // Create the default WiFi STA
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0)
    if (_enWifiSTAMode)
    {
    esp_netif_create_default_wifi_sta();
    }
    if (_enWifiAPMode)
    {
        esp_netif_create_default_wifi_ap();
    }
#endif

    // Setup a config to initialise the WiFi resources
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "startWifi failed to init wifi err %d", err);
        return false;
    }

    // Attach event handlers
#ifdef PRIVATE_EVENT_LOOP
    esp_event_handler_register_with(_wifiEventLoopHandle, WIFI_EVENT, ESP_EVENT_ANY_ID, &wifiEventHandler, NULL);
    esp_event_handler_register_with(_wifiEventLoopHandle, IP_EVENT, IP_EVENT_STA_GOT_IP, &wifiEventHandler, NULL);
    esp_event_handler_register_with(_wifiEventLoopHandle, IP_EVENT, IP_EVENT_STA_LOST_IP, &wifiEventHandler, NULL);
#else
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifiEventHandler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifiEventHandler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &wifiEventHandler, NULL);
#endif

    // Set storage
    esp_wifi_set_storage(WIFI_STORAGE_FLASH);

    // Set mode
    err = esp_wifi_set_mode(_enWifiSTAMode && _enWifiAPMode ? WIFI_MODE_APSTA : 
                (_enWifiSTAMode ? WIFI_MODE_STA : 
                        (_enWifiAPMode ? WIFI_MODE_AP : WIFI_MODE_NULL)));
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "start failed to set mode err %s (%d)", esp_err_to_name(err), err);
        return false;
    }

    // Start WiFi
    err = esp_wifi_start();
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "startWifi failed to start WiFi err %s (%d)", esp_err_to_name(err), err);
        return false;
    }

    LOG_I(MODULE_PREFIX, "startWifi init complete");

    // Init ok
    _wifiSTAStarted = true;
    return true;

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Configure WiFi
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool NetworkSystem::configureWiFi(const String& ssid, const String& pw, const String& hostname, const String& apSsid, const String& apPassword)
{
    // Set hostname
    setHostname(hostname.length() == 0 ? _defaultHostname.c_str() : hostname.c_str());

    // Handle STA mode config
    bool rsltOk = true;
    if (_enWifiSTAMode)
    {
    // Check if both SSID and pw have now been set
    if (ssid.length() != 0 && pw.length() != 0)
    {
        // Populate configuration for WiFi
            wifi_config_t wifiSTAConfig = {
            .sta = {
                {.ssid = ""},
                {.password = ""},
                .scan_method = WIFI_FAST_SCAN,
                .bssid_set = 0,
                {.bssid = ""},
                .channel = 0,
                .listen_interval = 0,
                .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
                .threshold = {.rssi = 0, .authmode = WIFI_AUTH_OPEN},
                .pmf_cfg = {.capable = 0, .required = 0},
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 3, 0)
                .rm_enabled = 1,
                .btm_enabled = 0,
                .mbo_enabled  = 0,
                .reserved = 0,
#endif
                }};
            strlcpy((char *)wifiSTAConfig.sta.ssid, ssid.c_str(), 32);
            strlcpy((char *)wifiSTAConfig.sta.password, pw.c_str(), 64);

        // Set configuration
            esp_err_t err = esp_wifi_set_config(ESP_IDF_WIFI_STA_MODE_FLAG, &wifiSTAConfig);
        if (err != ESP_OK)
        {
                LOG_E(MODULE_PREFIX, "configureWiFi *** WiFi STA failed to set configuration err %s (%d) ***", esp_err_to_name(err), err);
            return false;
        }

            LOG_I(MODULE_PREFIX, "WiFi STA Credentials Set SSID %s hostname %s", ssid.c_str(), _hostname.c_str());

        // Connect
        if (esp_wifi_disconnect() == ESP_OK)
        {
            esp_wifi_connect();
        }
        }
        else
        {
            LOG_E(MODULE_PREFIX, "configureWiFi *** WiFi STA SSID and password must be set ***");
            rsltOk = false;
        }
    }

    // Handle AP mode config
    if (_enWifiAPMode)
    {
        // Populate configuration for AP
        wifi_config_t wifiAPConfig = {
            .ap = {
                {.ssid = ""},
                {.password = ""},
                .ssid_len = 0,
                .channel = 1,
                .authmode = WIFI_AUTH_WPA_WPA2_PSK,
                .ssid_hidden = 0,
                .max_connection = 10,
                .beacon_interval = 100,
                .pairwise_cipher = WIFI_CIPHER_TYPE_CCMP,
                .ftm_responder = 0
            }};
        strlcpy((char *)wifiAPConfig.ap.ssid, apSsid.c_str(), 32);
        strlcpy((char *)wifiAPConfig.ap.password, apPassword.c_str(), 64);

        // Set configuration
        esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &wifiAPConfig);
        if (err != ESP_OK)
        {
            LOG_E(MODULE_PREFIX, "configureWiFi *** WiFi AP failed to set configuration err %s (%d) ***", esp_err_to_name(err), err);
            return false;
        }

        LOG_I(MODULE_PREFIX, "WiFi AP Credentials Set SSID %s", apSsid);
    }
    return rsltOk;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clear credentials
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

esp_err_t NetworkSystem::clearCredentials()
{
    // Restore to system defaults
    esp_wifi_disconnect();
    _staSSID = "";
    _wifiIPV4Addr = "";
    esp_err_t err = esp_wifi_restore();
    if (err == ESP_OK) {
        LOG_I(MODULE_PREFIX, "apiWifiClear CLEARED WiFi Credentials");
    } else {
        LOG_W(MODULE_PREFIX, "apiWifiClear Failed to clear WiFi credentials esp_err %s (%d)", esp_err_to_name(err), err);
    }
    return err;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pause WiFi operation - used to help with BLE vs WiFi contention in ESP32
// (they share the same radio)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void NetworkSystem::pauseWiFi(bool pause)
{
    // Check if pause or resume
    if (pause)
    {
        // Check already paused
        if (_isPaused)
            return;

        // Disconnect
        esp_wifi_disconnect();

        // Debug
        LOG_I(MODULE_PREFIX, "pauseWiFi - WiFi disconnected");
    }
    else
    {
        // Check already unpaused
        if (!_isPaused)
            return;

        // Connect
        esp_wifi_connect();
        _numConnectRetries = 0;

        // Debug
        LOG_I(MODULE_PREFIX, "pauseWiFi - WiFi reconnect requested");
    }
    _isPaused = pause;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Hostname can only contain certain characters
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String NetworkSystem::hostnameMakeValid(const String& hostname)
{
    String okHostname;
    for (uint32_t i = 0; i < hostname.length(); i++)
    {
        if (isalpha(hostname[i]) || isdigit(hostname[i] || (hostname[i] == '-')))
            okHostname += hostname[i];
    }
    return okHostname;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scan WiFi
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool NetworkSystem::wifiScan(bool start, String& jsonResult)
{
    // Check for start
    if (start)
        return _wifiScanner.scanStart();

    // Check for scan completed
    if (!_wifiScanner.isScanInProgress())
    {
        // Get results
        return _wifiScanner.getResultsJSON(jsonResult);
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ethernet - WiFi bridge flow control
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

esp_err_t NetworkSystem::initEthWiFiBridgeFlowControl(void)
{
    _ethWiFiBridgeFlowControlQueue = xQueueCreate(FLOW_CONTROL_QUEUE_LENGTH, sizeof(flow_control_msg_t));
    if (!_ethWiFiBridgeFlowControlQueue) {
        ESP_LOGE(MODULE_PREFIX, "initEthWiFiBridgeFlowControl create queue failed");
        return ESP_FAIL;
    }
    BaseType_t ret = xTaskCreate(ethWiFiFlowCtrlTask, "flow_ctl", 2048, NULL, (tskIDLE_PRIORITY + 2), NULL);
    if (ret != pdTRUE) {
        ESP_LOGE(MODULE_PREFIX, "initEthWiFiBridgeFlowControl create task failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ethernet - WiFi bridge flow control task
// This task will fetch the packet from the queue, and then send out through WiFi.
// WiFi handles packets slower than Ethernet, we might add some delay between each transmit.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void NetworkSystem::ethWiFiFlowCtrlTask(void *args)
{
    flow_control_msg_t msg;
    int res = 0;
    uint32_t timeout = 0;
    while (1) {
        if (xQueueReceive(_ethWiFiBridgeFlowControlQueue, &msg, pdMS_TO_TICKS(FLOW_CONTROL_QUEUE_TIMEOUT_MS)) == pdTRUE) {
            timeout = 0;
            if ((_wifiAPClientCount > 0) && msg.length) {
                do {
                    vTaskDelay(pdMS_TO_TICKS(timeout));
                    timeout += 2;
                    res = esp_wifi_internal_tx(WIFI_IF_AP, msg.packet, msg.length);
                } while (res && timeout < FLOW_CONTROL_WIFI_SEND_TIMEOUT_MS);
                if (res != ESP_OK) {
                    ESP_LOGE(MODULE_PREFIX, "ethWiFiFlowCtrlTask WiFi send packet failed: %d", res);
                }
            }
            free(msg.packet);
        }
    }
    vTaskDelete(NULL);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Forward packets from Wi-Fi to Ethernet
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

esp_err_t NetworkSystem::wifiRxPacketCallback(void *buffer, uint16_t len, void *eb)
{
    if (_ethConnected) {
        if (esp_eth_transmit(_ethernetHandle, buffer, len) != ESP_OK) {
            ESP_LOGE(MODULE_PREFIX, "Ethernet send packet failed");
        }
    }
    esp_wifi_internal_free_rx_buffer(eb);
    return ESP_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Forward packets from Ethernet to Wi-Fi
// Note that, Ethernet works faster than Wi-Fi on ESP32,
// so we need to add an extra queue to balance their speed difference.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

esp_err_t NetworkSystem::ethRxPacketCallback(esp_eth_handle_t eth_handle, uint8_t *buffer, uint32_t len, void *priv)
{
    esp_err_t ret = ESP_OK;
    flow_control_msg_t msg = {
        .packet = buffer,
        .length = (uint16_t)len
    };
    if (xQueueSend(_ethWiFiBridgeFlowControlQueue, &msg, pdMS_TO_TICKS(FLOW_CONTROL_QUEUE_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(MODULE_PREFIX, "send flow control message failed or timeout");
        free(buffer);
        ret = ESP_FAIL;
    }
    return ret;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set Hostname
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void NetworkSystem::setHostname(const char* hostname)
{
    _hostname = hostnameMakeValid(hostname);
#ifdef DEBUG_HOSTNAME_SETTING
    LOG_I(MODULE_PREFIX, "setHostname (req %s) actual %s", hostname, _hostname.c_str());
#endif    
}
