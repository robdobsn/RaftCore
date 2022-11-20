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

static const char* MODULE_PREFIX = "NetworkSystem";

// Global object
NetworkSystem networkSystem;

// Retries
int NetworkSystem::_numConnectRetries = 0;
EventGroupHandle_t NetworkSystem::_wifiRTOSEventGroup;
String NetworkSystem::_staSSID;
String NetworkSystem::_wifiIPV4Addr;
bool NetworkSystem::_ethConnected = false;
String NetworkSystem::_ethIPV4Addr;

// Paused
bool NetworkSystem::_isPaused = false;

// #define DEBUG_RSSI_GET_TIME

#ifdef ETHERNET_HARDWARE_OLIMEX
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
    // Vars
    _isEthEnabled = false;
    _isWiFiEnabled = false;
    _netifStarted = false;
    _wifiSTAStarted = false;

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

void NetworkSystem::setup(bool enableWiFi, bool enableEthernet, const char* defaultHostname)
{
    LOG_I(MODULE_PREFIX, "setup enableWiFi %s defaultHostname %s", enableWiFi ? "YES" : "NO", defaultHostname);

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

    // Start WiFi STA if required
    if (_netifStarted && _isWiFiEnabled)
    {
        // Station mode
        startStationMode();

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

#ifdef ETHERNET_HARDWARE_OLIMEX
    if (_netifStarted && _isEthEnabled)
    {
        // Create ethernet event loop that running in background
        esp_netif_t *pEthNetif = nullptr;
        esp_eth_handle_t eth_handle = nullptr;
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
        esp_err_t err = esp_eth_driver_install(&config, &eth_handle);

        // Check driver ok
        if (err != ESP_OK)
        {
            LOG_W(MODULE_PREFIX, "setup failed to install eth driver");
        }
        else if (pEthNetif)
        {
            // Attach Ethernet driver to Ethernet netif
            err = esp_netif_attach(pEthNetif, esp_eth_new_netif_glue(eth_handle));
        }
        else
        {
            LOG_W(MODULE_PREFIX, "setup failed to create netif for ethernet");
            err = ESP_FAIL;
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
            err = esp_eth_start(eth_handle);
        }

        // Check for error
        if (err != ESP_OK)
        {
            LOG_W(MODULE_PREFIX, "setup failed to start eth driver");
        }
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
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        LOG_I(MODULE_PREFIX, "WiFi STA_START ... calling connect");
        esp_wifi_connect();
        LOG_I(MODULE_PREFIX, "WiFi STA_START ... connect returned");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED)
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
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
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
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE)
    {
        // LOG_W(MODULE_PREFIX, "scan complete");
        networkSystem._wifiScanner.scanComplete();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        LOG_I(MODULE_PREFIX, "WiFi got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        _numConnectRetries = 0;
        char ipAddrStr[32];
        sprintf(ipAddrStr, IPSTR, IP2STR(&event->ip_info.ip));
        _wifiIPV4Addr = ipAddrStr;
        xEventGroupSetBits(_wifiRTOSEventGroup, IP_CONNECTED_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP)
    {
        LOG_W(MODULE_PREFIX, "WiFi lost ip");
        if (!_isPaused)
            _wifiIPV4Addr = "";
        xEventGroupClearBits(_wifiRTOSEventGroup, IP_CONNECTED_BIT);
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
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
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
    // ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    // ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Station Mode
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool NetworkSystem::startStationMode()
{
    // Check if already initialised
    if (_wifiSTAStarted)
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
            LOG_E(MODULE_PREFIX, "startStationMode FAILED to start event loop");
            return false;
        }            
    }
#endif

    // Create the default WiFi STA
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0)
    esp_netif_create_default_wifi_sta();
#endif

    // Setup a config to initialise the WiFi resources
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "startStationMode failed to init wifi err %d", err);
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
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "startStationMode failed to set mode err %s (%d)", esp_err_to_name(err), err);
        return false;
    }

    // Start WiFi
    err = esp_wifi_start();
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "startStationMode failed to start WiFi err %s (%d)", esp_err_to_name(err), err);
        return false;
    }

    LOG_I(MODULE_PREFIX, "startStationMode init complete");

    // Init ok
    _wifiSTAStarted = true;
    return true;

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Configure WiFi
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool NetworkSystem::configureWiFi(const String& ssid, const String& pw, const String& hostname)
{
    if (hostname.length() == 0)
        _hostname = _defaultHostname;
    else
        _hostname = hostname;
    _hostname = hostnameMakeValid(_hostname);

    // Check if both SSID and pw have now been set
    if (ssid.length() != 0 && pw.length() != 0)
    {
        // Populate configuration for WiFi
        wifi_config_t wifiConfig = {
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
        strlcpy((char *)wifiConfig.sta.ssid, ssid.c_str(), 32);
        strlcpy((char *)wifiConfig.sta.password, pw.c_str(), 64);

        // Set configuration
        esp_err_t err = esp_wifi_set_config(ESP_IDF_WIFI_STA_MODE_FLAG, &wifiConfig);
        if (err != ESP_OK)
        {
            LOG_E(MODULE_PREFIX, "configureWiFi *** WiFi failed to set configuration err %s (%d) ***", esp_err_to_name(err), err);
            return false;
        }

        LOG_I(MODULE_PREFIX, "WiFi Credentials Set SSID %s hostname %s", ssid.c_str(), hostname.c_str());

        // Connect
        if (esp_wifi_disconnect() == ESP_OK)
        {
            esp_wifi_connect();
        }

        return true;
    }
    return false;
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
