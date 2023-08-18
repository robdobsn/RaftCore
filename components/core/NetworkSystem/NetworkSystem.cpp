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
#include <RaftUtils.h>
#include <ESPUtils.h>
#include <RaftArduino.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_private/wifi.h"
#include "sdkconfig.h"
#include "time.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
#include "esp_netif_sntp.h"
#include "esp_sntp.h"

// This is a hacky fix - hopefully temporary - for ESP IDF 5.1.0 which throws a compile error
#define HACK_ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(servers_in_list, list_of_servers)   {   \
            .smooth_sync = false,                   \
            .server_from_dhcp = false,              \
            .wait_for_sync = true,                  \
            .start = true,                          \
            .sync_cb = NULL,                        \
            .renew_servers_after_new_IP = false,    \
            .ip_event_to_renew = (ip_event_t)0,                 \
            .index_of_first_server = 0,             \
            .num_of_servers = (servers_in_list),    \
            .servers = list_of_servers,             \
}
#define HACK_ESP_NETIF_SNTP_DEFAULT_CONFIG(server) \
            HACK_ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(1, {server})

#endif

static const char* MODULE_PREFIX = "NetworkSystem";

// Global object
NetworkSystem networkSystem;

// Warnings
#define WARN_ON_WIFI_DISCONNECT_IF_ETH_NOT_CONNECTED
#define WARN_NETWORK_EVENTS

// Debug
// #define DEBUG_RSSI_GET_TIME
// #define DEBUG_HOSTNAME_SETTING
// #define DEBUG_NETWORK_EVENTS
// #define DEBUG_NETWORK_EVENTS_DETAIL

#ifdef ETHERNET_HARDWARE_OLIMEX
#define ETHERNET_IS_SUPPORTED
#define ETH_PHY_LAN87XX
#endif

#ifdef DEBUG_NETWORK_EVENTS
#define LOG_NETWORK_EVENT_INFO( tag, format, ... ) LOG_I( tag, format, ##__VA_ARGS__ )
#else
#define LOG_NETWORK_EVENT_INFO( tag, format, ... )
#endif
#ifdef WARN_NETWORK_EVENTS
#define LOG_NETWORK_EVENT_WARN( tag, format, ... ) LOG_W( tag, format, ##__VA_ARGS__ )
#else
#define LOG_NETWORK_EVENT_WARN( tag, format, ... )
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NetworkSystem::NetworkSystem()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool NetworkSystem::setup(const NetworkSettings& networkSettings)
{
    // Check if already setup
    if (_isSetup)
    {
        LOG_W(MODULE_PREFIX, "setup called when already setup");
        return false;
    }

    // Save settings
    _networkSettings = networkSettings;

    // Create an RTOS event group to record network events
    _networkRTOSEventGroup = xEventGroupCreate();
    if (!_networkRTOSEventGroup)
    {
        LOG_E(MODULE_PREFIX, "setup failed to create RTOS event group");
        return false;
    }

    // Create the default event loop
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "setup failed to create default event loop");
        return false;
    }

    // Not initially connected
    xEventGroupClearBits(_networkRTOSEventGroup, WIFI_STA_CONNECTED_BIT);
    xEventGroupClearBits(_networkRTOSEventGroup, WIFI_STA_IP_CONNECTED_BIT);
    xEventGroupClearBits(_networkRTOSEventGroup, WIFI_STA_FAIL_BIT);
    xEventGroupClearBits(_networkRTOSEventGroup, ETH_CONNECTED_BIT);
    xEventGroupClearBits(_networkRTOSEventGroup, ETH_IP_CONNECTED_BIT);

    // Is setup
    _isSetup = true;

    // Check if no network enabled
    if (!(_networkSettings.enableEthernet || 
          _networkSettings.enableWifiSTAMode || 
          _networkSettings.enableWifiAPMode))
    {
        LOG_I(MODULE_PREFIX, "setup - no network enabled");
        return false;
    }

    // Init netif
    err = esp_netif_init();
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "setup failed to init netif err %s", esp_err_to_name(err));
        return false;
    }

    // Start WiFi if required
    if (_networkSettings.enableWifiSTAMode || _networkSettings.enableWifiAPMode)
    {
        // Start Wifi
        startWifi();
    }

    // Start ethernet if required
    if (_networkSettings.enableEthernet)
    {
        // Start Ethernet
        startEthernet();
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    // SNTP server
    if (!_networkSettings.ntpServer.isEmpty())
    {
        // Setup server
        esp_sntp_config_t config = HACK_ESP_NETIF_SNTP_DEFAULT_CONFIG(_networkSettings.ntpServer.c_str());
        esp_netif_sntp_init(&config);
    }
#endif

    // Timezone
    if (!_networkSettings.timezone.isEmpty())
    {
        setenv("TZ", _networkSettings.timezone.c_str(), 1);
        tzset();
    }

    // Debug
    LOG_I(MODULE_PREFIX, "setup OK");
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void NetworkSystem::service()
{
    // Get WiFi RSSI value if connected
    // Don't set WIFI_RSSI_CHECK_MS too low as getting AP info takes ~2ms
    if (Raft::isTimeout(millis(), _wifiRSSILastMs, WIFI_RSSI_CHECK_MS))
    {
        _wifiRSSILastMs = millis();
        _wifiRSSI = 0;
        if (isWifiStaConnectedWithIP())
        {
#ifdef DEBUG_RSSI_GET_TIME
            uint64_t startUs = micros();
#endif
            wifi_ap_record_t ap;
            esp_err_t rslt = esp_wifi_sta_get_ap_info(&ap);
            _wifiRSSI = ap.rssi;
#ifdef DEBUG_RSSI_GET_TIME
            uint64_t endUs = micros();
            LOG_I(MODULE_PREFIX, "service get RSSI %d us", (int)(endUs - startUs));
#endif
            if (rslt != ESP_OK)
            {
                _wifiRSSI = 0;
                // Debug
#ifdef DEBUG_RSSI_GET_TIME
                LOG_W(MODULE_PREFIX, "service get RSSI failed %s", esp_err_to_name(rslt));
#endif
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Status
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool NetworkSystem::isWifiStaConnectedWithIP()
{
    // Check valid
    if (!_isSetup)
        return false;
    // Use the clear bits function with nothing to clear as a way to get the current value
    uint connBits = xEventGroupClearBits(_networkRTOSEventGroup, 0);
    return (connBits & WIFI_STA_CONNECTED_BIT) && (connBits & WIFI_STA_IP_CONNECTED_BIT);
}

bool NetworkSystem::isIPConnected()
{
    // Check valid
    if (!_isSetup)
        return false;
    // Use the clear bits function with nothing to clear as a way to get the current value
    uint connBits = xEventGroupClearBits(_networkRTOSEventGroup, 0);
    return (connBits & WIFI_STA_IP_CONNECTED_BIT) | (connBits & ETH_IP_CONNECTED_BIT);
}

bool NetworkSystem::isEthConnectedWithIP()
{
    // Check valid
    if (!_isSetup)
        return false;
    // Use the clear bits function with nothing to clear as a way to get the current value
    uint connBits = xEventGroupClearBits(_networkRTOSEventGroup, 0);
    return (connBits & ETH_CONNECTED_BIT) && (connBits & ETH_IP_CONNECTED_BIT);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get settings JSON
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String NetworkSystem::getSettingsJSON(bool includeBraces)
{
    // Get the status JSON
    String jsonStr = R"("wifiSTA":")" + String(_networkSettings.enableWifiSTAMode) + 
                        R"(","wifiAP":")" + String(_networkSettings.enableWifiAPMode) + 
                        R"(","eth":")" + String(_networkSettings.enableEthernet) +
                        R"(","hostname":")" + _hostname +
                        R"(")";

    // Add braces if required
    if (includeBraces)
        return "{" + jsonStr + "}";
    return jsonStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get conn state JSON
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String NetworkSystem::getConnStateJSON(bool includeBraces, bool staInfo, bool apInfo, bool ethInfo, bool useBeforePauseValue)
{
    // Get the status JSON
    String jsonStr;
    if (staInfo)
    {
        bool wifiStaConnWithIP = isWifiStaConnectedWithIP();
        if (useBeforePauseValue)
            wifiStaConnWithIP = _wifiStaConnWithIPBeforePause;
        jsonStr = R"("wifiSTA":{"en":)" + String(_networkSettings.enableWifiSTAMode);
        if (_networkSettings.enableWifiSTAMode)
            jsonStr += R"(,"conn":)" + String(wifiStaConnWithIP) + 
                            R"(,"SSID":")" + _wifiStaSSID +
                            R"(","RSSI":)" + String(_wifiRSSI) + 
                            R"(,"IP":")" + _wifiIPV4Addr + 
                            R"(","MAC":")" + getSystemMACAddressStr(ESP_MAC_WIFI_STA, ":") +
                            R"(","paused":)" + String(isPaused() ? 1 : 0) +
                            R"(,"hostname":")" + _hostname + R"(")";
        jsonStr += R"(})";
    }
    if (apInfo)
    {
        if (!jsonStr.isEmpty())
            jsonStr += R"(,)";
        jsonStr += R"("wifiAP":{"en":)" + String(_networkSettings.enableWifiAPMode);
        if (_networkSettings.enableWifiAPMode)
            jsonStr += R"(,"SSID":")" + _wifiAPSSID +
                        R"(","clients":)" + String(_wifiAPClientCount);
        jsonStr += R"(})";
    }
    if (ethInfo)
    {
        if (!jsonStr.isEmpty())
            jsonStr += R"(,)";
        jsonStr += R"("eth":{"en":)" + String(_networkSettings.enableEthernet);
        if (_networkSettings.enableEthernet)
            jsonStr += R"(,"conn":)" + String(isEthConnectedWithIP()) +
                        R"(,"IP":")" + _ethIPV4Addr +
                        R"(","MAC":")" + _ethMACAddress + R"(")";
        jsonStr += R"(})";
    }
    // Add braces if required
    if (includeBraces)
        return "{" + jsonStr + "}";
    return jsonStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// WiFi Event handler
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void NetworkSystem::networkEventHandler(void *arg, esp_event_base_t event_base,
                                      int32_t event_id, void *pEventData)
{
#ifdef DEBUG_NETWORK_EVENTS_DETAIL
    LOG_I(MODULE_PREFIX, "====== Network EVENT base %d id %d ======", (int)event_base, event_id);
#endif
    if (event_base == WIFI_EVENT)
    {
        networkSystem.wifiEventHandler(arg, event_id, pEventData);
    }
    else if (event_base == IP_EVENT)
    {
        networkSystem.ipEventHandler(arg, event_id, pEventData);
    }
    else if (event_base == ETH_EVENT)
    {
        networkSystem.ethEventHandler(arg, event_id, pEventData);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start WiFi
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool NetworkSystem::startWifi()
{
    // Create the default WiFi elements
    bool enWifiSTAMode = _networkSettings.enableWifiSTAMode;
    bool enWifiAPMode = _networkSettings.enableWifiAPMode;
    if (enWifiSTAMode && !_pWifiStaNetIf)
        _pWifiStaNetIf = esp_netif_create_default_wifi_sta();
    if (enWifiAPMode && !_pWifiApNetIf)
        _pWifiApNetIf = esp_netif_create_default_wifi_ap();

    // Set hostname
    if (_pWifiStaNetIf && !_hostname.isEmpty())
        esp_netif_set_hostname(_pWifiStaNetIf, _hostname.c_str());

    // Setup a config to initialise the WiFi resources
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "startWifi failed to init err %s (%d)", esp_err_to_name(err), err);
        return false;
    }

    // Attach event handlers
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &networkEventHandler, nullptr, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &networkEventHandler, nullptr, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &networkEventHandler, nullptr, nullptr);

    // Set storage
    esp_wifi_set_storage(WIFI_STORAGE_FLASH);

    // Set mode
    wifi_mode_t mode = WIFI_MODE_APSTA;
    if (!enWifiSTAMode) mode = WIFI_MODE_AP;
    if (!enWifiAPMode) mode = WIFI_MODE_STA;
    err = esp_wifi_set_mode(mode);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "start failed to set mode err %s (%d)", esp_err_to_name(err), err);
        return false;
    }

    // Wifi config
    if (enWifiSTAMode)
    {
        // Get the config from NVS if present
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
        wifi_config_t currentWifiConfig = {0};
#pragma GCC diagnostic pop
        esp_err_t err = esp_wifi_get_config(ESP_IDF_WIFI_STA_MODE_FLAG, &currentWifiConfig);
        if ((err != ESP_OK) || 
            (currentWifiConfig.sta.threshold.authmode != _networkSettings.wifiSTAScanThreshold))
        {
            // Amend config as required
            LOG_I(MODULE_PREFIX, "startWifi threshold %d set to %d", 
                        currentWifiConfig.sta.threshold.authmode,
                        _networkSettings.wifiSTAScanThreshold);

            // Set new settings
            currentWifiConfig.sta.threshold.authmode = _networkSettings.wifiSTAScanThreshold;
            esp_wifi_set_config(ESP_IDF_WIFI_STA_MODE_FLAG, &currentWifiConfig);

            // Debug
            String ssid = String((char*)currentWifiConfig.sta.ssid, sizeof(currentWifiConfig.sta.ssid));
            LOG_I(MODULE_PREFIX, "setup connecting to ssid %s", ssid.c_str());
        }
    }

    // Start WiFi
    err = esp_wifi_start();
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "startWifi failed to start WiFi err %s (%d)", esp_err_to_name(err), err);
        return false;
    }

    LOG_I(MODULE_PREFIX, "startWifi init complete");
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Stop WiFi - called when pausing
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void NetworkSystem::stopWifi()
{
    // Stop WiFi
    esp_wifi_stop();

    // Remove event handlers
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, nullptr);
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_LOST_IP, nullptr);
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, nullptr);

    // Deinit WiFi
    esp_wifi_deinit();

    // Debug
    LOG_I(MODULE_PREFIX, "stopWifi complete");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Configure Wifi STA mode
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool NetworkSystem::configWifiSTA(const String& ssidIn, const String& pwIn)
{
    // Check valid
    if (!_isSetup)
        return false;

    // Unescape strings
    String ssidUnescaped = Raft::unescapeString(ssidIn);
    String pwUnescaped = Raft::unescapeString(pwIn);

    LOG_I(MODULE_PREFIX, "configWifiSTA SSID %s (original %s) PW %s", 
                    ssidUnescaped.isEmpty() ? "<<NONE>>" : ssidUnescaped.c_str(),
                    ssidIn.isEmpty() ? "<<NONE>>" : ssidIn.c_str(),
                    pwUnescaped.isEmpty() ? "<<NONE>>" : "OK");

    // Handle STA mode config
    if (!_networkSettings.enableWifiSTAMode)
        return false;

    // Check if both SSID and pw have now been set
    if ((ssidUnescaped.isEmpty()) || (pwUnescaped.isEmpty()))
        return false;

    // Populate configuration for WiFi
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    wifi_config_t currentWifiConfig = {};
#pragma GCC diagnostic pop
    esp_err_t err = esp_wifi_get_config(ESP_IDF_WIFI_STA_MODE_FLAG, &currentWifiConfig);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "configWifiSTA failed to get config err %s (%d)", esp_err_to_name(err), err);
        return false;
    }

    // Setup config
    strlcpy((char *)currentWifiConfig.sta.ssid, ssidUnescaped.c_str(), 32);
    strlcpy((char *)currentWifiConfig.sta.password, pwUnescaped.c_str(), 64);
    currentWifiConfig.sta.threshold.authmode = _networkSettings.wifiSTAScanThreshold;

    // Set configuration
    err = esp_wifi_set_config(ESP_IDF_WIFI_STA_MODE_FLAG, &currentWifiConfig);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "configWifiSTA FAILED err %s (%d) ***", esp_err_to_name(err), err);
        return false;
    }

    // Check if we need to disconnect
    uint connBits = xEventGroupClearBits(_networkRTOSEventGroup, 0);
    if (connBits & WIFI_STA_CONNECTED_BIT)
    {
        // Disconnecting will start the connection process again
        esp_wifi_disconnect();
        LOG_I(MODULE_PREFIX, "configWifiSTA disconnect requested (will reconnect) SSID %s", ssidUnescaped.c_str());
    }
    else
    {
        // Connect
        esp_wifi_connect();
        LOG_I(MODULE_PREFIX, "configWifiSTA connect requested SSID %s", ssidUnescaped.c_str());
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Configure Wifi AP
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool NetworkSystem::configWifiAP(const String& apSSID, const String& apPassword)
{
    // Handle AP mode config
    if (!_networkSettings.enableWifiAPMode)
        return false;

    // Populate configuration for AP
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    wifi_config_t wifiAPConfig = {0};
#pragma GCC diagnostic pop

    // Settings
    wifiAPConfig.ap.channel = _networkSettings.wifiAPChannel;
    wifiAPConfig.ap.max_connection = _networkSettings.wifiAPMaxConn;
    wifiAPConfig.ap.authmode = _networkSettings.wifiAPAuthMode;
    strlcpy((char *)wifiAPConfig.ap.ssid, apSSID.c_str(), 32);
    strlcpy((char *)wifiAPConfig.ap.password, apPassword.c_str(), 64);

    // Set configuration
    esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &wifiAPConfig);
    if (err != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "configWifiAP FAILED err %s (%d)", esp_err_to_name(err), err);
        return false;
    }

    // Debug
    LOG_I(MODULE_PREFIX, "configWifiAP OK SSID %s", apSSID);
    _wifiAPSSID = apSSID;
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clear credentials
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

esp_err_t NetworkSystem::clearCredentials()
{
    // Check valid
    if (!_networkSettings.enableWifiSTAMode)
        return ESP_ERR_INVALID_STATE;

    // Restore to system defaults
    esp_wifi_disconnect();
    _wifiStaSSID.clear();
    _wifiIPV4Addr.clear();
    esp_err_t err = esp_wifi_restore();
    if (err == ESP_OK)
    {
        LOG_I(MODULE_PREFIX, "apiWifiClear CLEARED WiFi Credentials");
    }
    else
    {
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

        // Store current connection state
        _wifiStaConnWithIPBeforePause = isWifiStaConnectedWithIP();

        // Stop WiFi
        stopWifi();

        // Debug
        LOG_I(MODULE_PREFIX, "pauseWiFi - WiFi disconnected");
    }
    else
    {
        // Check already unpaused
        if (!_isPaused)
            return;

        // Start WiFi if enabled
        if (_networkSettings.enableWifiSTAMode || _networkSettings.enableWifiAPMode)
        {
            startWifi();
            _numWifiConnectRetries = 0;

            // Debug
            LOG_I(MODULE_PREFIX, "pauseWiFi - WiFi reconnect requested");
        }
    }
    _isPaused = pause;
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
// Set Hostname
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void NetworkSystem::setHostname(const char* hostname)
{
    _hostname = hostnameMakeValid(hostname);
#ifdef DEBUG_HOSTNAME_SETTING
    LOG_I(MODULE_PREFIX, "setHostname (req %s) actual %s", hostname, _hostname.c_str());
#endif    
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Hostname can only contain certain characters
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String NetworkSystem::hostnameMakeValid(const String& hostname)
{
    String okHostname;
    for (uint32_t i = 0; i < hostname.length(); i++)
    {
        if (isalpha(hostname[i]) || isdigit(hostname[i]) || (hostname[i] == '-'))
            okHostname += hostname[i];
    }
    return okHostname;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set log level
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void NetworkSystem::setLogLevel(esp_log_level_t logLevel)
{
    // WiFi log level
    esp_log_level_set("wifi", ESP_LOG_WARN);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start ethernet
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool NetworkSystem::startEthernet()
{
    esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, &networkEventHandler, nullptr, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &networkEventHandler, nullptr, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, &networkEventHandler, nullptr, nullptr);

    #ifdef ETHERNET_HARDWARE_OLIMEX
    if (_networkSettings.enableEthernet &&  
        (_networkSettings.ethLanChip != NetworkSettings::ETH_CHIP_TYPE_NONE) && (_networkSettings.powerPin != -1))
    {
        // Create ethernet event loop that running in background
        esp_netif_t *pEthNetif = nullptr;
        _ethernetHandle = nullptr;
        // Create new default instance of esp-netif for Ethernet
        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
        pEthNetif = esp_netif_new(&cfg);

        // Set hostname
        if (pEthNetif && !_hostname.isEmpty())
            esp_netif_set_hostname(pEthNetif, _hostname.c_str());

        // Init MAC and PHY configs to default
        eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
        eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

        phy_config.phy_addr = _networkSettings.phyAddr;
        phy_config.reset_gpio_num = _networkSettings.phyRstPin;
        gpio_config_t powerPinConfig(
        {
            .pin_bit_mask = (1ULL << _networkSettings.powerPin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        });
        gpio_config(&powerPinConfig);
        gpio_set_level((gpio_num_t) _networkSettings.powerPin, 1);
        vTaskDelay(pdMS_TO_TICKS(10));

        // Create Ethernet driver
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        // Init vendor specific MAC config to default
        eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
        esp32_emac_config.smi_mdc_gpio_num = (gpio_num_t) _networkSettings.smiMDCPin;
        esp32_emac_config.smi_mdio_gpio_num = (gpio_num_t) _networkSettings.smiMDIOPin;
        // Create new ESP32 Ethernet MAC instance
        esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
#else
        mac_config.smi_mdc_gpio_num = (gpio_num_t) _networkSettings.smiMDCPin;
        mac_config.smi_mdio_gpio_num = (gpio_num_t) _networkSettings.smiMDIOPin;
        esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&mac_config);
#endif
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
            return false;
        }
        return true;
    }
#endif
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle WiFi events
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void NetworkSystem::wifiEventHandler(void *pArg, int32_t eventId, void *pEventData)
{
    // Check event id
    switch (eventId)
    {
    case WIFI_EVENT_SCAN_DONE:
        networkSystem._wifiScanner.scanComplete();
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi scan done");
        break;
    case WIFI_EVENT_WIFI_READY:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi ready");
        break;
    case WIFI_EVENT_STA_START:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi station start");
        esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_STOP:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi station stopped");
        break;
    case WIFI_EVENT_STA_CONNECTED:
    {
        // Get event data and check
        wifi_event_sta_connected_t *pEvent = (wifi_event_sta_connected_t *)pEventData;
        _wifiStaSSID = String((const char*)(pEvent->ssid),
                    pEvent->ssid_len > sizeof(wifi_event_sta_connected_t::ssid) ?
                    sizeof(wifi_event_sta_connected_t::ssid) : pEvent->ssid_len);
        xEventGroupSetBits(_networkRTOSEventGroup, WIFI_STA_CONNECTED_BIT);
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi station connected");
        break;
    }
    case WIFI_EVENT_STA_DISCONNECTED:
    {
        // Handle disconnect
        handleWiFiStaDisconnectEvent();
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi station disconnected");
        break;
    }
    case WIFI_EVENT_STA_AUTHMODE_CHANGE:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi station auth mode changed");
        break;
    case WIFI_EVENT_STA_WPS_ER_SUCCESS:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi station WPS success");
        break;
    case WIFI_EVENT_STA_WPS_ER_FAILED:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi station WPS failed");
        break;
    case WIFI_EVENT_STA_WPS_ER_TIMEOUT:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi station WPS timeout");
        break;
    case WIFI_EVENT_STA_WPS_ER_PIN:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi station WPS pin");
        break;
    case WIFI_EVENT_AP_START:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi AP started");
        break;
    case WIFI_EVENT_AP_STOP:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi AP stopped");
        break;
    case WIFI_EVENT_AP_STACONNECTED:
    {
        wifi_event_ap_staconnected_t *pEvent = (wifi_event_ap_staconnected_t *)pEventData;
        String macStr = Raft::formatMACAddr(pEvent->mac, ":");
        _wifiAPClientCount++;
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi AP station connected MAC %s aid %d numClients %d", 
                        macStr.c_str(), pEvent->aid, _wifiAPClientCount);
        break;
    }
    case WIFI_EVENT_AP_STADISCONNECTED:
    {
        wifi_event_ap_stadisconnected_t *pEvent = (wifi_event_ap_stadisconnected_t *)pEventData;
        String macStr = Raft::formatMACAddr(pEvent->mac, ":");
        LOG_I(MODULE_PREFIX, "WiFi AP client leave MAC %s aid %d", macStr.c_str(), pEvent->aid);
        _wifiAPClientCount--;
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi AP station disconnected MAC %s aid %d numClients %d", 
                        macStr.c_str(), pEvent->aid, _wifiAPClientCount);
        break;
    }
    case WIFI_EVENT_AP_PROBEREQRECVED:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi AP probe request received");
        break;
    case WIFI_EVENT_FTM_REPORT:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi FTM report");
        break;
    case WIFI_EVENT_STA_BSS_RSSI_LOW:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi station BSS RSSI low");
        break;
    case WIFI_EVENT_ACTION_TX_STATUS:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi action TX status");
        break;
    case WIFI_EVENT_ROC_DONE:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi ROC done");
        break;
    case WIFI_EVENT_STA_BEACON_TIMEOUT:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi station beacon timeout");
        break;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    case WIFI_EVENT_AP_WPS_RG_SUCCESS:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi AP WPS RG success");
        break;
    case WIFI_EVENT_AP_WPS_RG_FAILED:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi AP WPS RG failed");
        break;
    case WIFI_EVENT_AP_WPS_RG_TIMEOUT:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi AP WPS RG timeout");
        break;
#endif
    default:
        break;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ethernet Event handler
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void NetworkSystem::ethEventHandler(void *arg, int32_t event_id, void *pEventData)
{
    switch (event_id)
    {
    case ETHERNET_EVENT_CONNECTED:
    {
        // get the ethernet driver handle from event data
        esp_eth_handle_t ethHandle = *(esp_eth_handle_t *)pEventData;
        uint8_t mac_addr[6] = {0};
        esp_eth_ioctl(ethHandle, ETH_CMD_G_MAC_ADDR, mac_addr);
        _ethMACAddress = Raft::formatMACAddr(mac_addr, ":");
        xEventGroupSetBits(_networkRTOSEventGroup, ETH_CONNECTED_BIT);
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "Ethernet Link Up HW Addr %s", _ethMACAddress.c_str());
        break;
    }
    case ETHERNET_EVENT_DISCONNECTED:
    {
        xEventGroupClearBits(_networkRTOSEventGroup, ETH_CONNECTED_BIT);
        _ethMACAddress = "";
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "Ethernet Link Down");
        break;
    }
    case ETHERNET_EVENT_START:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

////////////////////////////////////////////////////////////////////////////////
// Handle IP events
////////////////////////////////////////////////////////////////////////////////

void NetworkSystem::ipEventHandler(void *arg, int32_t event_id, void *pEventData)
{
    // Get event data
    ip_event_got_ip_t *pEvent = (ip_event_got_ip_t *)pEventData;
    char ipAddrStr[32];
    switch (event_id)
    {
    case IP_EVENT_STA_GOT_IP:
    {
        // Get IP address string
        sprintf(ipAddrStr, IPSTR, IP2STR(&pEvent->ip_info.ip));
        _wifiIPV4Addr = ipAddrStr;
        _numWifiConnectRetries = 0;
        // Set event group bit
        xEventGroupSetBits(_networkRTOSEventGroup, WIFI_STA_IP_CONNECTED_BIT);
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi station got IP %s", _wifiIPV4Addr.c_str());
        break;
    }
    case IP_EVENT_STA_LOST_IP:
    {
        if (!_isPaused)
            _wifiIPV4Addr.clear();
        xEventGroupClearBits(_networkRTOSEventGroup, WIFI_STA_IP_CONNECTED_BIT);
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi station lost IP");
        break;
    }
    case IP_EVENT_AP_STAIPASSIGNED:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi AP station assigned IP");
        break;
    case IP_EVENT_GOT_IP6:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "WiFi station/AP IPv6 preferred");
        break;
    case IP_EVENT_ETH_GOT_IP:
    {
        // Get IP address string
        sprintf(ipAddrStr, IPSTR, IP2STR(&pEvent->ip_info.ip));
        _ethIPV4Addr = ipAddrStr;
        // Set event group bit
        xEventGroupSetBits(_networkRTOSEventGroup, ETH_IP_CONNECTED_BIT);
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "Ethernet got IP %s", _ethIPV4Addr.c_str());
        break;
    }
    case IP_EVENT_ETH_LOST_IP:
    {
        _ethIPV4Addr = "";
        xEventGroupClearBits(_networkRTOSEventGroup, ETH_IP_CONNECTED_BIT);
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "Ethernet lost IP");
        break;
    }
    case IP_EVENT_PPP_GOT_IP:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "PPP got IP");
        break;
    case IP_EVENT_PPP_LOST_IP:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "PPP lost IP");
        break;
    }

}

////////////////////////////////////////////////////////////////////////////////
// Handle WiFi disconnect event
////////////////////////////////////////////////////////////////////////////////

void NetworkSystem::handleWiFiStaDisconnectEvent()
{
    // Handle pause
    if (!_isPaused)
    {
        if ((WIFI_CONNECT_MAX_RETRY < 0) || (_numWifiConnectRetries < WIFI_CONNECT_MAX_RETRY))
        {
            warnOnWiFiDisconnectIfEthNotConnected();
            esp_wifi_connect();
            _numWifiConnectRetries++;
        }
        else
        {
            xEventGroupSetBits(_networkRTOSEventGroup, WIFI_STA_FAIL_BIT);
        }
        _wifiIPV4Addr.clear();
        _wifiStaSSID.clear();
    }
    // Clear connected bit
    xEventGroupClearBits(_networkRTOSEventGroup, WIFI_STA_CONNECTED_BIT);
}

////////////////////////////////////////////////////////////////////////////////
// Debug and Warnings
////////////////////////////////////////////////////////////////////////////////

void NetworkSystem::warnOnWiFiDisconnectIfEthNotConnected()
{
#ifdef WARN_ON_WIFI_DISCONNECT_IF_ETH_NOT_CONNECTED
    if (!isEthConnectedWithIP())
    {
        if ((_numWifiConnectRetries < 3) || 
            ((_numWifiConnectRetries < 100) && (_numWifiConnectRetries % 10) == 0) ||
            ((_numWifiConnectRetries < 1000) && (_numWifiConnectRetries % 100) == 0) ||
            ((_numWifiConnectRetries % 1000) == 0))
        {
            LOG_W(MODULE_PREFIX, "WiFi disconnected, retry to connect to the AP retries %d", _numWifiConnectRetries);
        }
    }
#endif
}

