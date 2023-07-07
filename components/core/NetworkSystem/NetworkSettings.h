/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// NetworkSettings
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string.h>
#include <ArduinoOrAlt.h>
#include <ConfigBase.h>
#include <esp_wifi_types.h>

class NetworkSettings
{
public:
    // Ethernet types
    enum EthLanChip
    {
        ETH_CHIP_TYPE_NONE,
        ETH_CHIP_TYPE_LAN87XX
    };

    void setFromConfig(ConfigBase& config, const char* pDefaultHostname, const char* pPrefix = nullptr)
    {
        // Extract enables from config
        enableWifiSTAMode = config.getBool("wifiSTAEn", false, pPrefix) || 
                            config.getBool("WiFiEnabled", true, pPrefix);
        enableWifiAPMode = config.getBool("wifiAPEn", false, pPrefix);
        enableEthernet = config.getBool("ethEn", false, pPrefix) || 
                         config.getBool("EthEnabled", false, pPrefix);

        // Wifi STA
        wifiSTAScanThreshold = getAuthModeFromStr(config.getString("wifiSTAScanThreshold", "OPEN", pPrefix).c_str());

        // Wifi AP
        wifiAPAuthMode = getAuthModeFromStr(config.getString("wifiAPAuthMode", "WPA2_PSK", pPrefix).c_str());
        wifiAPMaxConn = config.getLong("wifiAPMaxConn", 4, pPrefix);
        wifiAPChannel = config.getLong("wifiAPChannel", 1, pPrefix);

        // Hostname
        defaultHostname = config.getString("defaultHostname", pDefaultHostname, pPrefix);

        // Ethernet settings
        ethLanChip = getChipEnum(config.getString("ethLanChip", "", pPrefix));
        powerPin = config.getLong("ethPowerPin", config.getLong("EthPowerPin", -1, pPrefix), pPrefix);
        smiMDCPin = config.getLong("ethMDCPin", config.getLong("EthMDCPin", -1, pPrefix), pPrefix);
        smiMDIOPin = config.getLong("ethMDIOPin", config.getLong("EthMDIOPin", -1, pPrefix), pPrefix);
        phyAddr = config.getLong("ethPhyAddr", config.getLong("EthPhyAddr", -1, pPrefix), pPrefix);
        phyRstPin = config.getLong("ethPhyRstPin", config.getLong("EthPhyRstPin", -1, pPrefix), pPrefix);

        // NTP settings
        ntpServer = config.getString("NTPServer", "pool.ntp.org");
        timezone = config.getString("timezone", 0);
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

    // NTP
    String ntpServer;
    String timezone;

private:
    EthLanChip getChipEnum(const String& ethLanChip)
    {
        if (ethLanChip.equalsIgnoreCase("LAN87XX"))
            return ETH_CHIP_TYPE_LAN87XX;
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
