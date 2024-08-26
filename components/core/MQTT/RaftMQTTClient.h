/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftMQTTClient
//
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <list>
#include <stdint.h>
#include "RaftArduino.h"
#include "MQTTProtocol.h"
#include "DNSResolver.h"

class RaftMQTTClient
{
public:
    RaftMQTTClient();
    ~RaftMQTTClient();
    void setup(bool isEnabled, const char *brokerHostname, uint32_t brokerPort, const char* clientID);
    void loop();
    bool publishToTopic(const String& topicName, const String& msgStr);
    void addTopic(const char* topicName, bool isInbound, const char* topicPath, uint8_t qos);
    void getTopicNames(std::vector<String>& topicNames, bool includeInbound, bool includeOutbound);

    // Default Port
    static const uint32_t DEFAULT_MQTT_PORT = 1883;

private:
    // Settings
    bool _isEnabled;
    uint32_t _brokerPort;
    uint32_t _keepAliveSecs;
    String _clientID;

    // DNSResolved
    DNSResolver _dnsResolver;

    // Connection state
    enum MQTTConnState
    {
       MQTT_STATE_DISCONNECTED,
       MQTT_STATE_SOCK_CONN_REQD,
       MQTT_STATE_MQTT_CONN_SENT,
       MQTT_STATE_MQTT_CONNECTED,
    };
    MQTTConnState _connState;
    uint32_t _lastConnStateChangeMs;

    // Protocol handler
    MQTTProtocol _mqttProtocol;

    // Timeouts
    static const uint32_t MQTT_DEFAULT_KEEPALIVE_TIME_SECS = 30;
    static const uint32_t MQTT_RETRY_CONNECT_TIME_MS = 5000;
    uint32_t _lastKeepAliveMs;

    // Max data length
    static const uint32_t MQTT_DEFAULT_FRAME_MAX_LEN = 1024;
    uint32_t _rxFrameMaxLen;

    // Socket client
    int _clientHandle;

    // Topics
    class TopicInfo
    {
    public:
        String topicName;
        bool isInbound;
        String topicFilter;
        uint8_t qos;
    };
    std::list<TopicInfo> _topicList;

    // Error reporting
    uint32_t _internalClosedErrorLastTime = 0;
    uint32_t _internalAddrLookupErrorLastTime = 0;
    uint32_t _internalSocketCreateErrorLastTime = 0;
    uint32_t _internalSocketFcntlErrorLastTime = 0;
    uint32_t _internalSocketConnErrorLastTime = 0;
    uint32_t _internalRxDataAllocErrorLastTime = 0;
    uint32_t _internalRxDataReadErrorLastTime = 0;
    uint32_t _internalRxDataConnClosedLastTime = 0;
    uint32_t _internalTxDataSendErrorLastTime = 0;
    uint32_t _internalTxDataSendLenLastTime = 0;
    uint32_t _internalSocketCreateSlowLastTime = 0;
    static const uint32_t INTERNAL_ERROR_LOG_MIN_GAP_MS = 10000;

    // Helpers
    void disconnect();
    void putDataCB(const uint8_t *pBuf, unsigned bufLen);
    void frameRxCB(const uint8_t *pBuf, unsigned bufLen);
    void socketConnect();
    bool getRxData(std::vector<uint8_t>& rxData, bool& isError, bool& connClosed);
    bool sendTxData(std::vector<uint8_t>& txData, bool& isError, bool& connClosed);
    void subscribeToTopics();

    // Debug
    static constexpr const char* MODULE_PREFIX = "MQTTClient";
};
