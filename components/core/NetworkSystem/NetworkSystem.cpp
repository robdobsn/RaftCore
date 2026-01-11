/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// NetworkSystem 
// Handles WiFi / Ethernet and IP
//
// Rob Dobson 2018-2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <time.h>
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
#include "Logger.h"
#include "NetworkSystem.h"
#include "RaftUtils.h"
#include "PlatformUtils.h"
#include "RaftArduino.h"
#include "esp_idf_version.h"

#ifndef NETWORK_MDNS_DISABLED
#include "mdns.h"
#endif

// Only for recent versions of ESP-IDF
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#ifdef ETHERNET_IS_ENABLED
#include "esp_eth_netif_glue.h"
#endif
#endif

// SPI Ethernet support (W5500)
#if defined(CONFIG_ETH_USE_SPI_ETHERNET)
#include "driver/spi_master.h"
#ifdef ETHERNET_IS_ENABLED
#include "esp_eth_phy.h"
#include "esp_eth_mac.h"
#endif
#endif

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

#ifdef ETHERNET_IS_ENABLED
    // Start ethernet if required
    if (_networkSettings.enableEthernet)
    {
        // Start Ethernet
        startEthernet();
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

void NetworkSystem::loop()
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
            LOG_I(MODULE_PREFIX, "loop get RSSI %d us", (int)(endUs - startUs));
#endif
            if (rslt != ESP_OK)
            {
                _wifiRSSI = 0;
                // Debug
#ifdef DEBUG_RSSI_GET_TIME
                LOG_W(MODULE_PREFIX, "loop get RSSI failed %s", esp_err_to_name(rslt));
#endif
            }
        }
    }

    // Handle time sync
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
    if (isWifiStaConnectedWithIP())
    {
        // Check time of last time sync
        if (!_timeSyncInitialDone || Raft::isTimeout(millis(), _timeSyncLastMs, TIME_SYNC_INTERVAL_MS))
        {
            _timeSyncLastMs = millis();
            _timeSyncInitialDone = true;
            if (!_networkSettings.ntpServer.isEmpty())
            {
                // Config
                esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(_networkSettings.ntpServer.c_str());

                // Sync callback
                config.sync_cb = [](timeval *pTV) {
                    // Debug
                    time_t now = pTV->tv_sec;
                    struct tm timeinfo;
                    localtime_r(&now, &timeinfo);
                    char strftime_buf[64];
                    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
                    LOG_I(MODULE_PREFIX, "time sync %s.%03d", strftime_buf, pTV->tv_usec / 1000);
                };

                // Sync time
                esp_netif_sntp_init(&config);
            }
        }
    }
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Status
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool NetworkSystem::isWifiStaConnectedWithIP() const
{
    // Check valid
    if (!_isSetup)
        return false;
    // Use the clear bits function with nothing to clear as a way to get the current value
    uint connBits = xEventGroupClearBits(_networkRTOSEventGroup, 0);
    return (connBits & WIFI_STA_CONNECTED_BIT) && (connBits & WIFI_STA_IP_CONNECTED_BIT);
}

bool NetworkSystem::isIPConnected() const
{
    // Check valid
    if (!_isSetup)
        return false;
    // Use the clear bits function with nothing to clear as a way to get the current value
    uint connBits = xEventGroupClearBits(_networkRTOSEventGroup, 0);
    return (connBits & WIFI_STA_IP_CONNECTED_BIT) | (connBits & ETH_IP_CONNECTED_BIT);
}

bool NetworkSystem::isEthConnectedWithIP() const
{
    // Check valid
    if (!_isSetup)
        return false;
    // Use the clear bits function with nothing to clear as a way to get the current value
    uint connBits = xEventGroupClearBits(_networkRTOSEventGroup, 0);
    return (connBits & ETH_CONNECTED_BIT) && (connBits & ETH_IP_CONNECTED_BIT);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get JSON settings
/// @param includeBraces true to include braces
/// @return String JSON settings
String NetworkSystem::getSettingsJSON(bool includeBraces) const
{
    // Get the setting JSON
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
/// @brief Get conn state JSON
/// @param includeBraces true to include braces
/// @param staInfo true to include STA info
/// @param apInfo true to include AP info
/// @param ethInfo true to include Ethernet info
/// @param useBeforePauseValue true to use the value before pause
/// @return String JSON connection state
String NetworkSystem::getConnStateJSON(bool includeBraces, bool staInfo, bool apInfo, bool ethInfo, bool useBeforePauseValue) const
{
    // Get the status JSON
    String jsonStr = R"("hostname":")" + _hostname + R"(")";
    if (staInfo && _networkSettings.enableWifiSTAMode)
    {
        if (!jsonStr.isEmpty())
            jsonStr += R"(,)";
        bool wifiStaConnWithIP = isWifiStaConnectedWithIP();
        if (useBeforePauseValue)
            wifiStaConnWithIP = _wifiStaConnWithIPBeforePause;
        String ssidToUse = wifiStaConnWithIP ? _wifiStaSSID : _wifiStaSSIDConnectingTo;
        jsonStr += R"("wifiSTA":{"conn":)" + String(wifiStaConnWithIP) + 
                            R"(,"SSID":")" + ssidToUse + R"(")";
        if (wifiStaConnWithIP)
        {
            jsonStr += R"(,"MAC":")" + getSystemMACAddressStr(ESP_MAC_WIFI_STA, ":") + 
                        R"(","RSSI":)" + String(_wifiRSSI) + 
                        R"(,"IP":")" + _wifiIPV4Addr + R"(")";
        }
        else
        {
            jsonStr += R"(,"MAC":")" + getSystemMACAddressStr(ESP_MAC_WIFI_STA, ":") + R"(")";
        }
        if (isPaused())
        {
            jsonStr += R"(,"RSSI":)" + String(_wifiRSSI) + 
            R"(,"IP":")" + _wifiIPV4Addr + R"(")";
        }
        if (isPaused())
        {
            jsonStr += R"(,"paused":1)";
        }
        jsonStr += R"(})";
    }
    if (apInfo && _networkSettings.enableWifiAPMode)
    {
        if (!jsonStr.isEmpty())
            jsonStr += R"(,)";
        jsonStr += R"("wifiAP":{"SSID":")" + _wifiAPSSID;
        if (_wifiAPClientCount > 0)
            jsonStr += R"(","clients":)" + String(_wifiAPClientCount);
        jsonStr += R"(})";
    }
#ifdef ETHERNET_IS_ENABLED
    if (ethInfo && _networkSettings.enableEthernet)
    {
        if (!jsonStr.isEmpty())
            jsonStr += R"(,)";
        jsonStr += R"("eth":{"conn":)" + String(isEthConnectedWithIP()) +
                        R"(,"IP":")" + _ethIPV4Addr +
                        R"(","MAC":")" + _ethMACAddress + R"(")";
        jsonStr += R"(})";
    }
#endif
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
    delay(2);
#endif
    if (event_base == WIFI_EVENT)
    {
        networkSystem.wifiEventHandler(arg, event_id, pEventData);
    }
    else if (event_base == IP_EVENT)
    {
        networkSystem.ipEventHandler(arg, event_id, pEventData);
    }
#ifdef ETHERNET_IS_ENABLED
    else if (event_base == ETH_EVENT)
    {
        networkSystem.ethEventHandler(arg, event_id, pEventData);
    }
#endif
#ifdef DEBUG_NETWORK_EVENTS_DETAIL
    LOG_I(MODULE_PREFIX, "====== Network EVENT DONE base %d id %d ======", (int)event_base, event_id);
    delay(2);
#endif
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
        // TODO - this is a hacky fix to avoid a warning - need to fix properly
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

            // Set SSID we're trying to connect to
            String ssid = String((char*)currentWifiConfig.sta.ssid, sizeof(currentWifiConfig.sta.ssid));
            Raft::trimString(ssid);
            _wifiStaSSIDConnectingTo = ssid;

            // Debug
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
    String ssidUnescaped = Raft::unescapeString(ssidIn.c_str());
    String pwUnescaped = Raft::unescapeString(pwIn.c_str());

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
    // TODO - this is a hacky fix to avoid a warning - need to fix properly
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
    _wifiStaSSIDConnectingTo = ssidUnescaped;

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
    // TODO - this is a hacky fix to avoid a warning - need to fix properly
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
    esp_err_t err = esp_wifi_restore();
    if (err == ESP_OK)
    {
        _wifiStaSSID.clear();
        _wifiIPV4Addr.clear();
        _wifiStaSSIDConnectingTo.clear();
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

#ifdef ETHERNET_IS_ENABLED

bool NetworkSystem::startEthernet()
{
    esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, &networkEventHandler, nullptr, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &networkEventHandler, nullptr, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, &networkEventHandler, nullptr, nullptr);

    // Debug
    LOG_I(MODULE_PREFIX, "startEthernet - lanChip %d phyAddr %d phyRstPin %d smiMDCPin %d smiMDIOPin %d powerPin %d",
                _networkSettings.ethLanChip, _networkSettings.phyAddr, _networkSettings.phyRstPin,
                _networkSettings.smiMDCPin, _networkSettings.smiMDIOPin, _networkSettings.powerPin);
                
    if (!_networkSettings.enableEthernet || (_networkSettings.ethLanChip == NetworkSettings::ETH_CHIP_TYPE_NONE))
    {
        LOG_I(MODULE_PREFIX, "startEthernet - ethernet disabled");
        return false;
    }

    // Create ethernet event loop that running in background
    esp_netif_t *pEthNetif = nullptr;
    _ethernetHandle = nullptr;
    // Create new default instance of esp-netif for Ethernet
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    pEthNetif = esp_netif_new(&cfg);

    // Set hostname
    if (pEthNetif && !_hostname.isEmpty())
        esp_netif_set_hostname(pEthNetif, _hostname.c_str());

    // Handle W5500 SPI Ethernet
#if defined(CONFIG_ETH_USE_SPI_ETHERNET) && defined(CONFIG_ETH_SPI_ETHERNET_W5500)
    if (_networkSettings.ethLanChip == NetworkSettings::ETH_CHIP_TYPE_W5500)
    {
        LOG_I(MODULE_PREFIX, "startEthernet - W5500 SPI mode MOSI:%d MISO:%d SCLK:%d CS:%d INT:%d RST:%d",
                    _networkSettings.spiMOSIPin, _networkSettings.spiMISOPin, _networkSettings.spiSCLKPin,
                    _networkSettings.spiCSPin, _networkSettings.spiIntPin, _networkSettings.phyRstPin);

        // Configure SPI bus
        spi_bus_config_t buscfg = {
            .mosi_io_num = _networkSettings.spiMOSIPin,
            .miso_io_num = _networkSettings.spiMISOPin,
            .sclk_io_num = _networkSettings.spiSCLKPin,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .data4_io_num = -1,
            .data5_io_num = -1,
            .data6_io_num = -1,
            .data7_io_num = -1,
            .data_io_default_level = 0,
            .max_transfer_sz = 0,
            .flags = 0,
            .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
            .intr_flags = 0
        };
        esp_err_t err = spi_bus_initialize((spi_host_device_t)_networkSettings.spiHostDevice, &buscfg, SPI_DMA_CH_AUTO);
        if (err != ESP_OK)
        {
            LOG_E(MODULE_PREFIX, "startEthernet - W5500 SPI bus init failed err %s", esp_err_to_name(err));
            return false;
        }

        // Configure SPI device
        spi_device_interface_config_t devcfg = {};
        devcfg.command_bits = 16;
        devcfg.address_bits = 8;
        devcfg.mode = 0;
        devcfg.clock_speed_hz = _networkSettings.spiClockSpeedMHz * 1000 * 1000;
        devcfg.queue_size = 20;
        devcfg.spics_io_num = _networkSettings.spiCSPin;

        // W5500 specific configuration
        eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG((spi_host_device_t)_networkSettings.spiHostDevice, &devcfg);
        w5500_config.int_gpio_num = _networkSettings.spiIntPin;

        // MAC configuration
        eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
        mac_config.rx_task_stack_size = 4096;
        
        // Create MAC instance
        esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
        if (!mac)
        {
            LOG_E(MODULE_PREFIX, "startEthernet - W5500 MAC creation failed");
            return false;
        }

        // PHY configuration
        eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
        phy_config.phy_addr = _networkSettings.phyAddr;
        phy_config.reset_gpio_num = _networkSettings.phyRstPin;

        // Create PHY instance
        esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
        if (!phy)
        {
            LOG_E(MODULE_PREFIX, "startEthernet - W5500 PHY creation failed");
            return false;
        }

        // Install Ethernet driver
        esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
        err = esp_eth_driver_install(&eth_config, &_ethernetHandle);
        if (err != ESP_OK)
        {
            LOG_E(MODULE_PREFIX, "startEthernet - W5500 driver install failed err %s", esp_err_to_name(err));
            return false;
        }

        // Set MAC address for W5500 from ESP32's base MAC
        uint8_t eth_mac[6];
        esp_read_mac(eth_mac, ESP_MAC_ETH);
        err = esp_eth_ioctl(_ethernetHandle, ETH_CMD_S_MAC_ADDR, eth_mac);
        if (err != ESP_OK)
        {
            LOG_E(MODULE_PREFIX, "startEthernet - W5500 set MAC failed err %s", esp_err_to_name(err));
            return false;
        }
        LOG_I(MODULE_PREFIX, "startEthernet - W5500 MAC set to %02x:%02x:%02x:%02x:%02x:%02x", 
              eth_mac[0], eth_mac[1], eth_mac[2], eth_mac[3], eth_mac[4], eth_mac[5]);

        // Attach Ethernet driver to TCP/IP stack
        err = esp_netif_attach(pEthNetif, esp_eth_new_netif_glue(_ethernetHandle));
        if (err != ESP_OK)
        {
            LOG_E(MODULE_PREFIX, "startEthernet - W5500 netif attach failed err %s", esp_err_to_name(err));
            return false;
        }

        // Start Ethernet driver
        err = esp_eth_start(_ethernetHandle);
        if (err != ESP_OK)
        {
            LOG_E(MODULE_PREFIX, "startEthernet - W5500 start failed err %s", esp_err_to_name(err));
            return false;
        }

        LOG_I(MODULE_PREFIX, "startEthernet - W5500 initialized successfully");
        return true;
    }
#endif

    // RMII-based Ethernet (LAN87XX, etc.)
#if defined(CONFIG_ETH_USE_ESP32_EMAC)
    // Handle power pin
    if (_networkSettings.powerPin >= 0)
    {
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
    }

    // Init MAC and PHY configs to default
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();

    // Create Ethernet driver
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
// TODO - This is a hacky fix - hopefully temporary - for ESP IDF 5.3.0 which throws a compile error
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
#if CONFIG_ETH_RMII_CLK_INPUT // IDF-9724
    #define DEFAULT_RMII_CLK_MODE EMAC_CLK_EXT_IN
#if CONFIG_ETH_RMII_CLK_IN_GPIO == 0
    #define DEFAULT_RMII_CLK_GPIO CONFIG_ETH_RMII_CLK_IN_GPIO
#else
    #error "ESP32 EMAC only support input RMII clock to GPIO0"
#endif // CONFIG_ETH_RMII_CLK_IN_GPIO == 0
#elif CONFIG_ETH_RMII_CLK_OUTPUT
    #define DEFAULT_RMII_CLK_MODE EMAC_CLK_OUT
#if CONFIG_ETH_RMII_CLK_OUTPUT_GPIO0
    #define DEFAULT_RMII_CLK_GPIO EMAC_APPL_CLK_OUT_GPIO
#else
    #undef DEFAULT_RMII_CLK_GPIO
    #define DEFAULT_RMII_CLK_GPIO ((emac_rmii_clock_gpio_t)CONFIG_ETH_RMII_CLK_OUT_GPIO)
#endif // CONFIG_ETH_RMII_CLK_OUTPUT_GPIO0
#else
#error "Unsupported RMII clock mode"
#endif // CONFIG_ETH_RMII_CLK_INPUT
    eth_esp32_emac_config_t esp32_emac_config = 
    {
        .smi_gpio =
        {
            .mdc_num = 23,
            .mdio_num = 18
        },
        .interface = EMAC_DATA_INTERFACE_RMII,
        .clock_config =
        {
            .rmii =
            {
                .clock_mode = DEFAULT_RMII_CLK_MODE,
                .clock_gpio = DEFAULT_RMII_CLK_GPIO
            }
        },
        .dma_burst_len = ETH_DMA_BURST_LEN_32,
        .intr_priority = 0,
    };
    esp32_emac_config.smi_gpio.mdc_num = (gpio_num_t) _networkSettings.smiMDCPin;
    esp32_emac_config.smi_gpio.mdio_num= (gpio_num_t) _networkSettings.smiMDIOPin;
    // Create new ESP32 Ethernet MAC instance
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
#else
    // Init vendor specific MAC config to default
    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    esp32_emac_config.smi_mdc_gpio_num = (gpio_num_t) _networkSettings.smiMDCPin;
    esp32_emac_config.smi_mdio_gpio_num = (gpio_num_t) _networkSettings.smiMDIOPin;
    // Create new ESP32 Ethernet MAC instance
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
#endif
#else
    mac_config.smi_mdc_gpio_num = (gpio_num_t) _networkSettings.smiMDCPin;
    mac_config.smi_mdio_gpio_num = (gpio_num_t) _networkSettings.smiMDIOPin;
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&mac_config);
#endif

    // Any phy hw?
#if defined(HW_ETH_PHY_IP101) || defined(HW_ETH_PHY_RTL8201) || defined(HW_ETH_PHY_LAN87XX) || \
        defined(HW_ETH_PHY_DP83848) || defined(HW_ETH_PHY_KSZ8041) || defined(HW_ETH_PHY_KSZ8081)
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = _networkSettings.phyAddr;
    phy_config.reset_gpio_num = _networkSettings.phyRstPin;
#endif

    // Create Ethernet PHY instance
    esp_eth_phy_t *phy = nullptr;
#if defined(HW_ETH_PHY_IP101)
    phy = esp_eth_phy_new_ip101(&phy_config);
#elif defined(HW_ETH_PHY_RTL8201)
    phy = esp_eth_phy_new_rtl8201(&phy_config);
#elif defined(HW_ETH_PHY_LAN87XX)
    phy = esp_eth_phy_new_lan87xx(&phy_config);
#elif defined(HW_ETH_PHY_DP83848)
    phy = esp_eth_phy_new_dp83848(&phy_config);
#elif defined(HW_ETH_PHY_KSZ8041)
    phy = esp_eth_phy_new_ksz8041(&phy_config);
#elif defined(HW_ETH_PHY_KSZ8081)
    phy = esp_eth_phy_new_ksz8081(&phy_config);
#endif
    if (!phy)
    {
        LOG_W(MODULE_PREFIX, "setup failed to create phy");
        return false;
    }

    // Ethernet config
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);

    // Install Ethernet driver
    esp_err_t err = esp_eth_driver_install(&config, &_ethernetHandle);

    // Check driver ok
    if (err != ESP_OK)
    {
        LOG_W(MODULE_PREFIX, "setup failed to install eth driver");
        return false;
    }

    if (!pEthNetif)
    {
        LOG_W(MODULE_PREFIX, "setup failed to create netif for ethernet");
        return false;
    }

    // Attach Ethernet driver to Ethernet netif
    err = esp_netif_attach(pEthNetif, esp_eth_new_netif_glue(_ethernetHandle));
    // Check event handler ok
    if (err != ESP_OK)
    {
        LOG_W(MODULE_PREFIX, "setup failed to attach eth driver");
        return false;
    }

    // Start Ethernet driver state machine
    err = esp_eth_start(_ethernetHandle);
    if (err != ESP_OK)
    {
        LOG_W(MODULE_PREFIX, "setup failed to start eth driver");
        return false;
    }

    // Debug
    LOG_I(MODULE_PREFIX, "setup ethernet OK");
    return true;
#endif // CONFIG_ETH_USE_ESP32_EMAC

    // If we get here, no supported ethernet type was configured
    LOG_E(MODULE_PREFIX, "startEthernet - no supported Ethernet type configured");
    return false;
}
#endif // ETHERNET_IS_ENABLED

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
        Raft::trimString(_wifiStaSSID);
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

#ifdef ETHERNET_IS_ENABLED

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

#endif

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
#ifndef NETWORK_MDNS_DISABLED
        // Setup mDNS
        setupMDNS();
#endif
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
#ifdef ETHERNET_IS_ENABLED
    case IP_EVENT_ETH_GOT_IP:
    {
        // Get IP address string
        sprintf(ipAddrStr, IPSTR, IP2STR(&pEvent->ip_info.ip));
        _ethIPV4Addr = ipAddrStr;
        // Set event group bit
        xEventGroupSetBits(_networkRTOSEventGroup, ETH_IP_CONNECTED_BIT);
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "Ethernet got IP %s", _ethIPV4Addr.c_str());
#ifndef NETWORK_MDNS_DISABLED
        // Setup mDNS
        setupMDNS();
#endif
        break;
    }
    case IP_EVENT_ETH_LOST_IP:
    {
        _ethIPV4Addr = "";
        xEventGroupClearBits(_networkRTOSEventGroup, ETH_IP_CONNECTED_BIT);
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "Ethernet lost IP");
        break;
    }
#endif
    case IP_EVENT_PPP_GOT_IP:
        LOG_NETWORK_EVENT_INFO(MODULE_PREFIX, "PPP got IP");
#ifndef NETWORK_MDNS_DISABLED
        // Setup mDNS
        setupMDNS();
#endif
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

////////////////////////////////////////////////////////////////////////////////
// Setup mDNS
////////////////////////////////////////////////////////////////////////////////

void NetworkSystem::setupMDNS()
{
#ifndef NETWORK_MDNS_DISABLED

// Check valid
    if (!_isSetup)
        return;

    // Check if mDNS is enabled
    if (!_networkSettings.enableMDNS)
        return;

    // Check if we have an IP address
    if (_wifiIPV4Addr.isEmpty() && _ethIPV4Addr.isEmpty())
        return;

    // Set hostname
    if (_hostname.isEmpty())
        _hostname = "esp32";

    // Start mDNS
    esp_err_t err = mdns_init();
    if (err != ESP_OK)
    {
        LOG_W(MODULE_PREFIX, "setupMDNS failed to init err %s", esp_err_to_name(err));
        return;
    }

    // Set hostname
    err = mdns_hostname_set(_hostname.c_str());
    if (err != ESP_OK)
    {
        LOG_W(MODULE_PREFIX, "setupMDNS failed to set hostname err %s", esp_err_to_name(err));
        return;
    }

    // Add service
    mdns_txt_item_t serviceTxtData[] = {
        {"board", "esp32"},
        {"path", "/"}
    };
    err = mdns_service_add(_hostname.c_str(), "_http", "_tcp", 80, serviceTxtData, 2);
    if (err != ESP_OK)
    {
        LOG_W(MODULE_PREFIX, "setupMDNS failed to add service err %s", esp_err_to_name(err));
        return;
    }

    // Debug
    LOG_I(MODULE_PREFIX, "setupMDNS OK hostname %s", _hostname.c_str());
#endif // NETWORK_MDNS_DISABLED
}
