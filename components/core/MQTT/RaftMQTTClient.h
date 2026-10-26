/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftMQTTClient
//
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <list>
#include <vector>
#include <functional>
#include <stdint.h>
#include "RaftArduino.h"
#include "MQTTProtocol.h"
#include "DNSResolver.h"

class RaftMQTTClient
{
public:
    // Callback for a message received on an inbound topic (topicName is the name given to addTopic)
    typedef std::function<void(const String& topicName, const uint8_t* pPayload, uint32_t payloadLen)> RxMsgCB;

    RaftMQTTClient();
    ~RaftMQTTClient();
    void setup(bool isEnabled, const char *brokerHostname, uint32_t brokerPort, const char* clientID,
                const char* username = "", const char* password = "",
                uint32_t keepAliveSecs = MQTT_DEFAULT_KEEPALIVE_TIME_SECS);
    void loop();
    bool publishToTopic(const String& topicName, const String& msgStr);
    bool publishToTopic(const String& topicName, const uint8_t* pMsg, uint32_t msgLen);
    void addTopic(const char* topicName, bool isInbound, const char* topicPath, uint8_t qos, bool retain = false);
    void getTopicNames(std::vector<String>& topicNames, bool includeInbound, bool includeOutbound);

    // Set callback for messages received on inbound topics
    void setRxMsgCB(RxMsgCB rxMsgCB)
    {
        _rxMsgCB = rxMsgCB;
    }

    // Connection state
    bool isConnected() const
    {
        return _connState == MQTT_STATE_MQTT_CONNECTED;
    }

    // QoS 1 publish tracking - in flight (sent and not yet acknowledged by the broker), acknowledged (PUBACK
    // received) and lost (the connection was closed before the PUBACK arrived)
    uint32_t getPubInFlightCount() const
    {
        return _pubInFlight.size();
    }
    uint32_t getPubAckedCount() const
    {
        return _pubAckedCount;
    }
    uint32_t getPubLostCount() const
    {
        return _pubLostCount;
    }

    // Messages received on inbound topics
    uint32_t getRxMsgCount() const
    {
        return _rxMsgCount;
    }

    // Status
    String getStatusJSON() const;

    // Default Port
    static const uint32_t DEFAULT_MQTT_PORT = 1883;

    // Default keep-alive
    static const uint32_t MQTT_DEFAULT_KEEPALIVE_TIME_SECS = 30;

private:
    // Settings
    bool _isEnabled = false;
    uint32_t _brokerPort = DEFAULT_MQTT_PORT;
    uint32_t _keepAliveSecs = MQTT_DEFAULT_KEEPALIVE_TIME_SECS;
    String _clientID;
    String _username;
    String _password;

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
    MQTTConnState _connState = MQTT_STATE_DISCONNECTED;
    uint32_t _lastConnStateChangeMs = 0;

    // Protocol handler
    MQTTProtocol _mqttProtocol;

    // Timeouts
    static const uint32_t MQTT_RETRY_CONNECT_TIME_MS = 5000;
    static const uint32_t MQTT_CONNACK_TIMEOUT_MS = 5000;
    static const uint32_t MQTT_PINGRESP_TIMEOUT_MS = 10000;
    static const uint32_t MQTT_PUBACK_TIMEOUT_MS = 10000;
    uint32_t _lastKeepAliveMs = 0;
    bool _pingRespPending = false;

    // Max data length
    static const uint32_t MQTT_DEFAULT_FRAME_MAX_LEN = 1024;
    static const uint32_t MQTT_RX_READ_BLOCK_LEN = 256;
    static const uint32_t MQTT_RX_MAX_READS_PER_LOOP = 8;

    // Socket client
    int _clientHandle = -1;

    // Topics
    class TopicInfo
    {
    public:
        String topicName;
        bool isInbound;
        String topicFilter;
        uint8_t qos;
        bool retain;
    };
    std::list<TopicInfo> _topicList;

    // QoS 1 publishes awaiting PUBACK
    class InFlightPub
    {
    public:
        uint16_t packetID;
        uint32_t sentMs;
    };
    std::vector<InFlightPub> _pubInFlight;
    uint32_t _pubAckedCount = 0;
    uint32_t _pubLostCount = 0;

    // Inbound messages
    RxMsgCB _rxMsgCB = nullptr;
    uint32_t _rxMsgCount = 0;

    // Error reporting
    uint32_t _internalClosedErrorLastTime = 0;
    uint32_t _internalAddrLookupErrorLastTime = 0;
    uint32_t _internalSocketCreateErrorLastTime = 0;
    uint32_t _internalSocketFcntlErrorLastTime = 0;
    uint32_t _internalSocketConnErrorLastTime = 0;
    uint32_t _internalRxDataReadErrorLastTime = 0;
    uint32_t _internalRxDataConnClosedLastTime = 0;
    uint32_t _internalTxDataSendErrorLastTime = 0;
    uint32_t _internalTxDataSendLenLastTime = 0;
    uint32_t _internalSocketCreateSlowLastTime = 0;
    static const uint32_t INTERNAL_ERROR_LOG_MIN_GAP_MS = 10000;

    // Helpers
    void disconnect();
    void closeConnection();
    void socketConnect();
    bool receiveAndProcess();
    void handleRxPacket(const MQTTProtocol::RxPacket& packet);
    void handleRxPublish(const MQTTProtocol::RxPacket& packet);
    bool sendTxData(const std::vector<uint8_t>& txData);
    void subscribeToTopics();

    // Debug
    static constexpr const char* MODULE_PREFIX = "MQTTClient";
};
