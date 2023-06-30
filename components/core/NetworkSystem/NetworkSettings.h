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
        enableWifiSTAMode = config.getBool("WiFiSTAModeEn", false, pPrefix) || 
                            config.getBool("WiFiEnabled", true, pPrefix);
        enableWifiAPMode = config.getBool("WiFiAPModeEn", false, pPrefix);
        enableEthernet = config.getBool("EthEnabled", false, pPrefix);

        // Hostname
        defaultHostname = config.getString("defaultHostname", pDefaultHostname, pPrefix);

        // Ethernet settings
        ethLanChip = getChipEnum(config.getString("EthLanChip", "", pPrefix));
        powerPin = config.getLong("EthPowerPin", -1, pPrefix);
        smiMDCPin = config.getLong("EthMDCPin", -1, pPrefix);
        smiMDIOPin = config.getLong("EthMDIOPin", -1, pPrefix);
        phyAddr = config.getLong("EthPhyAddr", 0, pPrefix);
        phyRstPin = config.getLong("EthPhyRstPin", -1, pPrefix);
    }

    // Enables
    bool enableEthernet = false;
    bool enableWifiSTAMode = false;
    bool enableWifiAPMode = false;

    // Hostname
    String defaultHostname;

    // Ethernet
    EthLanChip ethLanChip = ETH_CHIP_TYPE_NONE;
    int powerPin = -1;
    int smiMDCPin = -1;
    int smiMDIOPin = -1;
    int phyAddr = 0;
    int phyRstPin = -1;

private:
    EthLanChip getChipEnum(const String& ethLanChip)
    {
        if (ethLanChip.equalsIgnoreCase("LAN87XX"))
            return ETH_CHIP_TYPE_LAN87XX;
        return ETH_CHIP_TYPE_NONE;
    }
};
