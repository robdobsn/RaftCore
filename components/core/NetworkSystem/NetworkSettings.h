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

    void setFromConfig(RaftJsonIF& config, const String& defaultHostnameIn, const char* pPrefix = nullptr)
    {
        RaftJsonPrefixed configPrefixed(config, pPrefix);

        // Extract enables from config
        enableWifiSTAMode = configPrefixed.getBool("wifiSTAEn", false) || 
                            configPrefixed.getBool("WiFiEnabled", false);
        enableWifiAPMode = configPrefixed.getBool("wifiAPEn", false);
        enableEthernet = configPrefixed.getBool("ethEn", false) || 
                         configPrefixed.getBool("EthEnabled", false);

        // Wifi STA
        wifiSTAScanThreshold = getAuthModeFromStr(configPrefixed.getString("wifiSTAScanThreshold", "OPEN").c_str());

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
    }

    // Enables
    bool enableEthernet = false;
    bool enableWifiSTAMode = false;
    bool enableWifiAPMode = false;

    // Hostname
    String defaultHostname;

    // Wifi STA
    wifi_auth_mode_t wifiSTAScanThreshold = WIFI_AUTH_WPA2_PSK;

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
