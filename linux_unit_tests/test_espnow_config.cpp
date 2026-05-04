// Standalone test runner for ESPNowConfig tests
// Build: make test_espnow_config

#include <stdio.h>
#include <string.h>
#include "ArduinoWString.h"
#include "RaftJson.h"
#include "ESPNowConfig.h"

static int gFailures = 0;

static void check(bool condition, const char* pMsg)
{
    if (!condition)
    {
        printf("FAIL: %s\n", pMsg);
        gFailures++;
    }
}

static void testDefaults()
{
    RaftJson json("{}");
    ESPNowConfig config;
    config.setup(json);

    check(!config.enable, "defaults disabled");
    check(strcmp(config.protocol.c_str(), "RICSerial") == 0, "default protocol");
    check(strcmp(config.interfaceName.c_str(), "ESPNow") == 0, "default interface");
    check(strcmp(config.wifiOwner.c_str(), "netman") == 0, "default wifi owner");
    check(config.maxPayload == 250, "default max payload");
    check(config.maxPeers == 4, "default max peers");
    check(config.rxQueueMax == 20, "default rx queue");
    check(config.peers.empty(), "default peers empty");
}

static void testMACParsing()
{
    uint8_t mac[ESPNowConfig::ESPNOW_MAC_ADDR_LEN] = {0};
    check(ESPNowConfig::parseMACAddress("aa:bb:cc:dd:ee:ff", mac), "colon MAC accepted");
    check(mac[0] == 0xaa && mac[5] == 0xff, "colon MAC bytes");
    check(ESPNowConfig::formatMACAddress(mac) == "aa:bb:cc:dd:ee:ff", "MAC formatting");
    check(ESPNowConfig::parseMACAddress("001122334455", mac), "plain MAC accepted");
    check(mac[0] == 0x00 && mac[1] == 0x11 && mac[5] == 0x55, "plain MAC bytes");
    check(!ESPNowConfig::parseMACAddress("00112233445", mac), "short MAC rejected");
    check(!ESPNowConfig::parseMACAddress("00:11:22:33:44:xx", mac), "non-hex MAC rejected");
}

static void testPeerParsing()
{
    RaftJson json(R"({
        "enable": true,
        "protocol": "RICFrame",
        "interface": "ESPNowTest",
        "wifiOwner": "NetMan",
        "channel": 6,
        "maxPayload": 180,
        "txQueueMax": 5,
        "pubQueueMax": 3,
        "rxQueueMax": 7,
        "peers": [
            {"name":"left","mac":"aa:bb:cc:dd:ee:ff","encrypt":true},
            {"name":"right","mac":"001122334455","channel":1,"maxPayload":32},
            {"name":"bad","mac":"not-a-mac"}
        ]
    })");

    ESPNowConfig config;
    config.setup(json);

    check(config.enable, "enabled parsed");
    check(strcmp(config.protocol.c_str(), "RICFrame") == 0, "protocol parsed");
    check(strcmp(config.interfaceName.c_str(), "ESPNowTest") == 0, "interface parsed");
    check(config.channel == 6, "channel parsed");
    check(config.maxPayload == 180, "max payload parsed");
    check(config.txQueueMax == 5, "tx queue parsed");
    check(config.pubQueueMax == 3, "pub queue parsed");
    check(config.rxQueueMax == 7, "rx queue parsed");
    check(config.peers.size() == 2, "invalid peer skipped");
    check(config.peers[0].name == "left", "peer 0 name");
    check(config.peers[0].channel == 6, "peer 0 inherits channel");
    check(config.peers[0].encrypt, "peer 0 encrypt");
    check(config.peers[1].name == "right", "peer 1 name");
    check(config.peers[1].channel == 1, "peer 1 channel override");
    check(config.peers[1].maxPayload == 32, "peer 1 max payload override");
}

static void testPayloadConstraint()
{
    check(ESPNowConfig::constrainPayloadLen(8) == ESPNowConfig::ESPNOW_DEFAULT_MAX_PAYLOAD, "too-small payload reset");
    check(ESPNowConfig::constrainPayloadLen(9) == 9, "minimum usable payload accepted");
}

int main()
{
    testDefaults();
    testMACParsing();
    testPeerParsing();
    testPayloadConstraint();

    if (gFailures != 0)
    {
        printf("ESPNowConfig tests failed: %d\n", gFailures);
        return 1;
    }
    printf("ESPNowConfig tests passed\n");
    return 0;
}
