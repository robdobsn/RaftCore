/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// NetworkSettings
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string.h>
#include "esp_wifi_types.h"
#include "RaftArduino.h"
#include "RaftJsonPrefixed.h"

class NetworkSettings
{
public:
    // Ethernet types
    enum EthLanChip
    {
        ETH_CHIP_TYPE_NONE,
        ETH_CHIP_TYPE_LAN87XX,
        ETH_CHIP_TYPE_W5500
    };

    // RMII clock source — replaces the removed CONFIG_ETH_RMII_CLK_* Kconfig in IDF 6.0
    enum EthRmiiClockMode
    {
        ETH_RMII_CLK_EXT_IN,    // External clock fed to GPIO0
        ETH_RMII_CLK_OUT        // ESP32 sources clock out on a GPIO
    };

    void setFromConfig(RaftJsonIF& config, const String& defaultHostnameIn, const char* pPrefix = nullptr)
    {
        RaftJsonPrefixed configPrefixed(config, pPrefix);

        // Extract enables from config
        enableWifiSTAMode = configPrefixed.getBool("wifiSTAEn", false) || 
                            configPrefixed.getBool("WiFiEnabled", false);
        enableWifiAPMode = configPrefixed.getBool("wifiAPEn", false);
        enableEthernet = configPrefixed.getBool("ethEn", false) ||
                         configPrefixed.getBool("EthEnabled", false);

        // WiFi start mode - "auto" (started at setup) or "onDemand" (started when requested)
        wifiStartOnDemand = configPrefixed.getString("startMode", "auto").equalsIgnoreCase("onDemand");

        // Wifi STA
        wifiSTAScanThreshold = getAuthModeFromStr(configPrefixed.getString("wifiSTAScanThreshold", "WPA2_PSK").c_str());
        wifiSTAConnectBySignal = configPrefixed.getBool("wifiSTAConnectBySignal", true);
        wifiSTAFastReconnect = configPrefixed.getBool("wifiSTAFastReconnect", false);
        wifiSTAFastReconnectMinRSSI = configPrefixed.getLong("wifiSTAFastReconnectMinRSSI", -80);
        wifiSTAFastReconnectMaxUses = configPrefixed.getLong("wifiSTAFastReconnectMaxUses", 0);

        // WiFi band (on chips with 5 GHz)
        wifiBand = configPrefixed.getString("wifiBand", "auto");

        // WiFi STA static IP (blank for DHCP)
        wifiStaticIP = configPrefixed.getString("wifiStaticIP", "");
        wifiStaticGateway = configPrefixed.getString("wifiStaticGateway", "");
        wifiStaticNetmask = configPrefixed.getString("wifiStaticNetmask", "255.255.255.0");
        wifiStaticDNS = configPrefixed.getString("wifiStaticDNS", "");

        // Maximum WiFi TX power (0 = ESP-IDF default)
        wifiTxPowerDbm = configPrefixed.getLong("wifiTxPowerDbm", 0);

        // Wifi AP
        wifiAPAuthMode = getAuthModeFromStr(configPrefixed.getString("wifiAPAuthMode", "WPA2_PSK").c_str());
        wifiAPMaxConn = configPrefixed.getLong("wifiAPMaxConn", 4);
        wifiAPChannel = configPrefixed.getLong("wifiAPChannel", 1);

        // Hostname
        defaultHostname = configPrefixed.getString("defaultHostname", defaultHostnameIn.c_str());

        // Ethernet settings
        ethLanChip = getChipEnum(configPrefixed.getString("ethLanChip", ""));
        powerPin = configPrefixed.getLong("ethPowerPin", -1);
        smiMDCPin = configPrefixed.getLong("ethMDCPin", -1);
        smiMDIOPin = configPrefixed.getLong("ethMDIOPin", -1);
        phyAddr = configPrefixed.getLong("ethPhyAddr", -1);
        phyRstPin = configPrefixed.getLong("ethPhyRstPin", -1);
        ethRmiiClockMode = getRmiiClockModeEnum(configPrefixed.getString("ethRmiiClockMode", "OUTPUT"));
        ethRmiiClockGpio = configPrefixed.getLong("ethRmiiClockGpio", 17);

        // SPI Ethernet settings (for W5500)
        spiHostDevice = configPrefixed.getLong("spiHostDevice", 2);
        spiMOSIPin = configPrefixed.getLong("spiMOSIPin", -1);
        spiMISOPin = configPrefixed.getLong("spiMISOPin", -1);
        spiSCLKPin = configPrefixed.getLong("spiSCLKPin", -1);
        spiCSPin = configPrefixed.getLong("spiCSPin", -1);
        spiIntPin = configPrefixed.getLong("spiIntPin", -1);
        spiClockSpeedMHz = configPrefixed.getLong("spiClockSpeedMHz", 20);

        // NTP settings
        ntpServer = configPrefixed.getString("NTPServer", "pool.ntp.org");
        timezone = configPrefixed.getString("timezone", "UTC");

        // mDNS
        enableMDNS = configPrefixed.getBool("enableMDNS", true);

        // WiFi diagnostics and link options (see WiFiDiagnostics::setOptions())
        wifiDiagJSON = configPrefixed.getString("wifiDiag", "");
    }

    // Enables
    bool enableEthernet = false;
    bool enableWifiSTAMode = false;
    bool enableWifiAPMode = false;

    // WiFi started on demand (NetworkSystem::wifiStart()) rather than at setup - e.g. a battery device which
    // only uses the network on some wakes from deep sleep
    bool wifiStartOnDemand = false;

    // Hostname
    String defaultHostname;

    // Wifi STA
    wifi_auth_mode_t wifiSTAScanThreshold = WIFI_AUTH_WPA2_PSK;

    // Wifi STA AP selection: true scans all channels and joins the strongest AP
    // with the configured SSID; false uses ESP-IDF's fast scan, which joins the
    // first matching AP found (possibly a weak one on a multi-AP network)
    bool wifiSTAConnectBySignal = true;

    // Wifi STA fast reconnect: the AP connected to (channel and BSSID) is remembered in RTC memory - so it
    // survives deep sleep and software restarts but not power on - and the next connection goes straight to it
    // without scanning. If that connection fails the remembered AP is forgotten and a normal scan is made.
    // So that a better AP (e.g. on a mesh network) is found the AP isn't remembered when the signal is weaker
    // than wifiSTAFastReconnectMinRSSI dBm (0 for no minimum) and a scan is made after it has been used
    // wifiSTAFastReconnectMaxUses times (0 for no limit).
    // Not for networks which rely on 802.11v/k roaming (BTM/MBO) as the AP is chosen by the device
    bool wifiSTAFastReconnect = false;
    int wifiSTAFastReconnectMinRSSI = -80;
    uint32_t wifiSTAFastReconnectMaxUses = 0;

    // WiFi band on chips with 5 GHz: "2g" or "5g" - restricting to one band shortens scans. Unset (or "auto")
    // leaves the driver's band setting alone
    String wifiBand = "auto";

    // WiFi STA static IP - when set DHCP isn't used, which saves the time it takes on each connection. The
    // gateway is required; the DNS server defaults to the gateway
    String wifiStaticIP;
    String wifiStaticGateway;
    String wifiStaticNetmask = "255.255.255.0";
    String wifiStaticDNS;

    // Maximum WiFi TX power in dBm (2..20, 0 = ESP-IDF default). Some boards need this limited as their transmit
    // signal is poor at full power (e.g. an AP can't decode the frames - seen as connections which fail or which
    // stay associated but pass no data)
    int wifiTxPowerDbm = 0;

    // Wifi AP
    wifi_auth_mode_t wifiAPAuthMode = WIFI_AUTH_WPA2_PSK;
    uint32_t wifiAPMaxConn = 4;
    uint32_t wifiAPChannel = 1;

    // Ethernet
    EthLanChip ethLanChip = ETH_CHIP_TYPE_NONE;
    int powerPin = -1;
    int smiMDCPin = -1;
    int smiMDIOPin = -1;
    int phyAddr = 0;
    int phyRstPin = -1;
    EthRmiiClockMode ethRmiiClockMode = ETH_RMII_CLK_OUT;
    int ethRmiiClockGpio = 17;

    // SPI Ethernet (W5500)
    int spiHostDevice = 2;      // SPI2_HOST
    int spiMOSIPin = -1;
    int spiMISOPin = -1;
    int spiSCLKPin = -1;
    int spiCSPin = -1;
    int spiIntPin = -1;
    int spiClockSpeedMHz = 20;

    // NTP
    String ntpServer;

    // WiFi diagnostics and link options JSON (see WiFiDiagnostics::setOptions())
    String wifiDiagJSON;
    String timezone;

    // mDNS
    bool enableMDNS = true;

private:
    EthLanChip getChipEnum(const String& ethLanChip)
    {
        if (ethLanChip.equalsIgnoreCase("LAN87XX"))
            return ETH_CHIP_TYPE_LAN87XX;
        if (ethLanChip.equalsIgnoreCase("W5500"))
            return ETH_CHIP_TYPE_W5500;
        return ETH_CHIP_TYPE_NONE;
    }
    EthRmiiClockMode getRmiiClockModeEnum(const String& mode)
    {
        if (mode.equalsIgnoreCase("INPUT") || mode.equalsIgnoreCase("EXT_IN"))
            return ETH_RMII_CLK_EXT_IN;
        return ETH_RMII_CLK_OUT;
    }
    wifi_auth_mode_t getAuthModeFromStr(const String& inStr)
    {
        if (inStr.equalsIgnoreCase("OPEN"))
            return WIFI_AUTH_OPEN;
        if (inStr.equalsIgnoreCase("WEP"))
            return WIFI_AUTH_WEP;
        if (inStr.equalsIgnoreCase("WPA_PSK"))
            return WIFI_AUTH_WPA_PSK;
        if (inStr.equalsIgnoreCase("WPA2_PSK"))
            return WIFI_AUTH_WPA2_PSK;
        if (inStr.equalsIgnoreCase("WPA_WPA2_PSK"))
            return WIFI_AUTH_WPA_WPA2_PSK;
        if (inStr.equalsIgnoreCase("WPA3_PSK"))
            return WIFI_AUTH_WPA3_PSK;
        if (inStr.equalsIgnoreCase("WPA2_WPA3_PSK"))
            return WIFI_AUTH_WPA2_WPA3_PSK;
        if (inStr.equalsIgnoreCase("WAPI_PSK"))
            return WIFI_AUTH_WAPI_PSK;
        if (inStr.equalsIgnoreCase("WPA2_ENTERPRISE"))
            return WIFI_AUTH_WPA2_ENTERPRISE;
        return WIFI_AUTH_WPA2_PSK;
    }
};
