/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftMQTTClient
//
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <vector>
#if defined(ARDUINO_ARCH_ESP32)
#include "Arduino.h"
#endif
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "RaftMQTTClient.h"
#include "RaftArduino.h"
#include "RaftUtils.h"
#include "Logger.h"

// Debug
#define WARN_MQTT_DNS_LOOKUP_FAILED
#define WARN_MQTT_CONNECTION_REFUSED
#define WARN_MQTT_SUBSCRIBE_FAILED
#define WARN_MQTT_PUBACK_TIMEOUT
// #define DEBUG_MQTT_GENERAL
// #define DEBUG_MQTT_CONNECTION
// #define DEBUG_SEND_DATA
// #define DEBUG_MQTT_CLIENT_RX
// #define DEBUG_MQTT_TOPIC_DETAIL
// #define DEBUG_MQTT_DNS_LOOKUP
// #define DEBUG_MQTT_SOCKET_CREATE
// #define DEBUG_MQTT_PUBLISH

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftMQTTClient::RaftMQTTClient()
{
    _mqttProtocol.setRxPacketMaxLen(MQTT_DEFAULT_FRAME_MAX_LEN);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftMQTTClient::~RaftMQTTClient()
{
    disconnect();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftMQTTClient::setup(bool isEnabled, const char *brokerHostname, uint32_t brokerPort, const char* clientID,
            const char* username, const char* password, uint32_t keepAliveSecs)
{
    // Disconnect if required
    disconnect();

    // Clear list of topics
    _topicList.clear();

    // Store settings
    _isEnabled = isEnabled;
    _brokerPort = brokerPort;
    _clientID = clientID;
    _username = username ? username : "";
    _password = password ? password : "";
    _keepAliveSecs = (keepAliveSecs == 0) || (keepAliveSecs > 0xffff) ? MQTT_DEFAULT_KEEPALIVE_TIME_SECS : keepAliveSecs;

    // DNS resolver
    _dnsResolver.setHostname(brokerHostname);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add topic
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftMQTTClient::addTopic(const char* topicName, bool isInbound, const char* topicFilter, uint8_t qos, bool retain)
{
    // Create record
    TopicInfo topicInfo = {topicName, isInbound, topicFilter, qos, retain};
    _topicList.push_back(topicInfo);
#ifdef DEBUG_MQTT_GENERAL
    LOG_I(MODULE_PREFIX, "addTopic name %s isInbound %s path %s qos %d retain %d",
                topicName, isInbound ? "Y" : "N", topicFilter, qos, retain);
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get topic names
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftMQTTClient::getTopicNames(std::vector<String>& topicNames, bool includeInbound, bool includeOutbound)
{
    // Get names of topics
    topicNames.clear();
    for (TopicInfo& topicInfo : _topicList)
    {
        if (includeInbound && topicInfo.isInbound)
            topicNames.push_back(topicInfo.topicName);
        if (includeOutbound && !topicInfo.isInbound)
            topicNames.push_back(topicInfo.topicName);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftMQTTClient::loop()
{
    // Check if enabled
    if (!_isEnabled)
        return;

    // Handle connection state
    switch (_connState)
    {
        case MQTT_STATE_DISCONNECTED:
        {
            // See if it's time to try to connect
            if (Raft::isTimeout(millis(), _lastConnStateChangeMs,
                        _retryConnectSoon ? MQTT_RETRY_CONNECT_NO_NETWORK_MS : MQTT_RETRY_CONNECT_TIME_MS))
            {
                // Don't try again immediately
                _lastConnStateChangeMs = millis();

                // Try to establish connection
                socketConnect();

                // Set time for slow connect
                _internalSocketCreateSlowLastTime = millis();
            }
            break;
        }
        case MQTT_STATE_SOCK_CONN_REQD:
        {
            // Check if the socket is ready for comms - connect() may have returned EIN_PROGRESS
            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 0;
            fd_set writeSet;
            FD_ZERO(&writeSet);
            FD_SET(_clientHandle, &writeSet);
            int selectErr = select(_clientHandle + 1, NULL, &writeSet, NULL, &timeout);
            if (selectErr < 0)
            {
                LOG_W(MODULE_PREFIX, "loop socket select error %d", errno);
                closeConnection();
                break;
            }

            // Check for max time waiting for socket to be ready
            if (Raft::isTimeout(millis(), _lastConnStateChangeMs, MQTT_RETRY_CONNECT_TIME_MS))
            {
                LOG_W(MODULE_PREFIX, "loop socket select timeout");
                closeConnection();
                break;
            }

            // Still waiting to connect
            if (selectErr == 0)
            {
                if (Raft::isTimeout(millis(), _internalSocketCreateSlowLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
                {
                    _internalSocketCreateSlowLastTime = millis();
                    LOG_W(MODULE_PREFIX, "loop socket select still waiting");
                }
                break;
            }

            // Connected
            LOG_I(MODULE_PREFIX, "loop connId %d CONNECTED to %s", _clientHandle, _dnsResolver.getHostname());

            // Send MQTT CONNECT packet
            std::vector<uint8_t> msgBuf;
            _mqttProtocol.encodeMQTTConnect(msgBuf, _keepAliveSecs, _clientID.c_str(),
                        _username.c_str(), _password.c_str());
            _mqttProtocol.clearRxData();
            if (!sendTxData(msgBuf))
            {
                closeConnection();
                break;
            }
            _connState = MQTT_STATE_MQTT_CONN_SENT;
            _lastConnStateChangeMs = millis();
            break;
        }
        case MQTT_STATE_MQTT_CONN_SENT:
        {
            // Wait for CONNACK
            if (!receiveAndProcess())
                break;
            if ((_connState == MQTT_STATE_MQTT_CONN_SENT) &&
                        Raft::isTimeout(millis(), _lastConnStateChangeMs, MQTT_CONNACK_TIMEOUT_MS))
            {
                LOG_W(MODULE_PREFIX, "loop CONNACK timeout");
                closeConnection();
            }
            break;
        }
        case MQTT_STATE_MQTT_CONNECTED:
        {
            // Check for keep-alive
            if (Raft::isTimeout(millis(), _lastKeepAliveMs, _keepAliveSecs * 500))
            {
                // Send PINGREQ packet
                std::vector<uint8_t> msgBuf;
                _mqttProtocol.encodeMQTTPingReq(msgBuf);
                if (!sendTxData(msgBuf))
                {
                    closeConnection();
                    break;
                }
                _lastKeepAliveMs = millis();
                _pingRespPending = true;
            }

            // Handle received data
            if (!receiveAndProcess())
                break;

            // Check the broker is still responding
            if (_pingRespPending && Raft::isTimeout(millis(), _lastKeepAliveMs, MQTT_PINGRESP_TIMEOUT_MS))
            {
                LOG_W(MODULE_PREFIX, "loop PINGRESP timeout");
                closeConnection();
                break;
            }
            if (!_pubInFlight.empty() &&
                        Raft::isTimeout(millis(), _pubInFlight.front().sentMs, MQTT_PUBACK_TIMEOUT_MS))
            {
#ifdef WARN_MQTT_PUBACK_TIMEOUT
                LOG_W(MODULE_PREFIX, "loop PUBACK timeout packetID %d inFlight %d",
                            _pubInFlight.front().packetID, (int)_pubInFlight.size());
#endif
                closeConnection();
            }
            break;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Deinit
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftMQTTClient::disconnect()
{
    // Check if already disconnected
    if (_connState == MQTT_STATE_DISCONNECTED)
        return;

    // Tell the broker if connected
    if (_connState == MQTT_STATE_MQTT_CONNECTED)
    {
        std::vector<uint8_t> msgBuf;
        _mqttProtocol.encodeMQTTDisconnect(msgBuf);
        sendTxData(msgBuf);
    }
#ifdef DEBUG_MQTT_CONNECTION
    LOG_I(MODULE_PREFIX, "disconnect connId %d CLOSED", _clientHandle);
#endif
    closeConnection();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Close the connection (QoS 1 publishes not yet acknowledged are counted as lost)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftMQTTClient::closeConnection()
{
    if (_clientHandle >= 0)
    {
        close(_clientHandle);
        _clientHandle = -1;
    }
    _pubLostCount += _pubInFlight.size();
    _pubInFlight.clear();
    _pingRespPending = false;
    _mqttProtocol.clearRxData();
    _connState = MQTT_STATE_DISCONNECTED;
    _lastConnStateChangeMs = millis();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Publish to topic
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftMQTTClient::publishToTopic(const String& topicName, const String& msgStr)
{
    return publishToTopic(topicName, (const uint8_t*)msgStr.c_str(), msgStr.length());
}

bool RaftMQTTClient::publishToTopic(const String& topicName, const uint8_t* pMsg, uint32_t msgLen)
{
    // Check connected
    if (_connState != MQTT_STATE_MQTT_CONNECTED)
        return false;

    // Find topic
    const TopicInfo* pTopicInfo = nullptr;
    for (const TopicInfo& topicInfo : _topicList)
    {
        if (!topicInfo.isInbound && topicName.equals(topicInfo.topicName))
        {
            pTopicInfo = &topicInfo;
            break;
        }
    }
    if (!pTopicInfo || (pTopicInfo->topicFilter.length() == 0))
        return false;

    // Form PUBLISH packet
    std::vector<uint8_t> msgBuf;
    uint16_t packetID = _mqttProtocol.encodeMQTTPublish(msgBuf, pTopicInfo->topicFilter.c_str(),
                pMsg, msgLen, pTopicInfo->qos, pTopicInfo->retain);

    // Send packet
    if (!sendTxData(msgBuf))
    {
        closeConnection();
        return false;
    }

    // Track QoS 1 publishes until acknowledged
    if (packetID != 0)
        _pubInFlight.push_back({packetID, (uint32_t)millis()});
#ifdef DEBUG_MQTT_PUBLISH
    LOG_I(MODULE_PREFIX, "publishToTopic %s len %d packetID %d inFlight %d",
                topicName.c_str(), (int)msgLen, packetID, (int)_pubInFlight.size());
#endif
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Status JSON
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String RaftMQTTClient::getStatusJSON() const
{
    return R"({"conn":)" + String(isConnected() ? 1 : 0) +
            R"(,"inFlight":)" + String((unsigned long)_pubInFlight.size()) +
            R"(,"acked":)" + String((unsigned long)_pubAckedCount) +
            R"(,"lost":)" + String((unsigned long)_pubLostCount) +
            R"(,"rx":)" + String((unsigned long)_rxMsgCount) + "}";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Attempt to connect via socket
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftMQTTClient::socketConnect()
{
#ifdef DEBUG_MQTT_CONNECTION
    LOG_I(MODULE_PREFIX, "sockConn attempting to connect to %s port %d", _dnsResolver.getHostname(), (int)_brokerPort);
#endif

    // Get IP address (no address yet means no network or the lookup is in progress - retry soon)
    ip_addr_t ipAddr;
    _retryConnectSoon = true;
    if (!_dnsResolver.getIPAddr(ipAddr))
        return;
    _retryConnectSoon = false;

    // Get address info for broker
    uint64_t microsStart = micros();

    // Create socket
#ifdef DEBUG_MQTT_SOCKET_CREATE
    LOG_I(MODULE_PREFIX, "sockConn creating socket");
#endif
    _clientHandle = socket(AF_INET, SOCK_STREAM, 0);
    if (_clientHandle < 0)
    {
        if (Raft::isTimeout(millis(), _internalSocketCreateErrorLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
        {
            _internalSocketCreateErrorLastTime = millis();
            LOG_W(MODULE_PREFIX, "sockConn FAIL sock create errno %d hostname %s addr %s port %d",
                        errno, _dnsResolver.getHostname(), ipaddr_ntoa(&ipAddr), (int)_brokerPort);
        }
        _clientHandle = -1;
        return;
    }

    // Set non-blocking
    int flags = fcntl(_clientHandle, F_GETFL, 0);
    if (flags < 0)
    {
        if (Raft::isTimeout(millis(), _internalSocketFcntlErrorLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
        {
            _internalSocketFcntlErrorLastTime = millis();
            LOG_W(MODULE_PREFIX, "sockConn FAIL fcntl get errno %d hostname %s addr %s port %d",
                            errno, _dnsResolver.getHostname(), ipaddr_ntoa(&ipAddr), (int)_brokerPort);
        }
        closeConnection();
        return;
    }
    flags |= O_NONBLOCK;
    if (fcntl(_clientHandle, F_SETFL, flags) < 0)
    {
        if (Raft::isTimeout(millis(), _internalSocketFcntlErrorLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
        {
            _internalSocketFcntlErrorLastTime = millis();
            LOG_W(MODULE_PREFIX, "sockConn FAIL fcntl set errno %d hostname %s addr %s port %d",
                            errno, _dnsResolver.getHostname(), ipaddr_ntoa(&ipAddr), (int)_brokerPort);
        }
        closeConnection();
        return;
    }

    // Connect
    struct sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(_brokerPort);
    serverAddress.sin_addr.s_addr = ipAddr.u_addr.ip4.addr;
    int connectErr = connect(_clientHandle, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
    if (connectErr < 0)
    {
        if (errno != EINPROGRESS)
        {
            // Failing at once (e.g. no route as there is no IP address yet) means no network - retry soon
            if (Raft::isTimeout(millis(), _internalSocketConnErrorLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
            {
                _internalSocketConnErrorLastTime = millis();
                LOG_W(MODULE_PREFIX, "sockConn connect error %d", errno);
            }
            closeConnection();
            _retryConnectSoon = true;
            return;
        }
    }

    // Set state
    _connState = MQTT_STATE_SOCK_CONN_REQD;
    _lastConnStateChangeMs = millis();
#ifdef DEBUG_MQTT_CONNECTION
    LOG_I(MODULE_PREFIX, "sockConn connId %d result %s took %dms",
                _clientHandle, connectErr < 0 ? "in progress" : "connected OK",
                int((micros() - microsStart) / 1000));
#else
    (void)microsStart;
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Receive data and handle the packets in it
// Returns false if the connection has been closed
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftMQTTClient::receiveAndProcess()
{
    // Read what is available (in blocks, up to a limit so the loop isn't held up)
    uint8_t rxBuf[MQTT_RX_READ_BLOCK_LEN];
    for (uint32_t readIdx = 0; readIdx < MQTT_RX_MAX_READS_PER_LOOP; readIdx++)
    {
        int32_t bufLen = recv(_clientHandle, rxBuf, sizeof(rxBuf), MSG_DONTWAIT);
        if (bufLen < 0)
        {
            if ((errno == EWOULDBLOCK) || (errno == EAGAIN))
                break;
            if (Raft::isTimeout(millis(), _internalRxDataReadErrorLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
            {
                _internalRxDataReadErrorLastTime = millis();
                LOG_W(MODULE_PREFIX, "receiveAndProcess read error %d", errno);
            }
            closeConnection();
            return false;
        }
        if (bufLen == 0)
        {
            if (Raft::isTimeout(millis(), _internalRxDataConnClosedLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
            {
                _internalRxDataConnClosedLastTime = millis();
                LOG_W(MODULE_PREFIX, "receiveAndProcess conn closed by broker");
            }
            closeConnection();
            return false;
        }
#ifdef DEBUG_MQTT_CLIENT_RX
        String rxDataStr;
        Raft::getHexStrFromBytes(rxBuf, bufLen, rxDataStr);
        LOG_I(MODULE_PREFIX, "receiveAndProcess rx %s", rxDataStr.c_str());
#endif
        if (!_mqttProtocol.addRxData(rxBuf, bufLen))
        {
            LOG_W(MODULE_PREFIX, "receiveAndProcess invalid data from broker");
            closeConnection();
            return false;
        }
        if (bufLen < (int32_t)sizeof(rxBuf))
            break;
    }

    // Handle complete packets
    MQTTProtocol::RxPacket packet;
    while (_mqttProtocol.getRxPacket(packet))
    {
        handleRxPacket(packet);
        if (_connState == MQTT_STATE_DISCONNECTED)
            return false;
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle a packet received from the broker
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftMQTTClient::handleRxPacket(const MQTTProtocol::RxPacket& packet)
{
    switch (packet.type)
    {
        case MQTTProtocol::PACKET_CONNACK:
        {
            if (_connState != MQTT_STATE_MQTT_CONN_SENT)
                break;
            if (packet.returnCode != 0)
            {
                // 4 = bad username or password, 5 = not authorised
#ifdef WARN_MQTT_CONNECTION_REFUSED
                LOG_W(MODULE_PREFIX, "handleRxPacket connection refused by broker code %d", packet.returnCode);
#endif
                closeConnection();
                break;
            }

            // Now connected
            _connState = MQTT_STATE_MQTT_CONNECTED;
            _lastConnStateChangeMs = millis();
            _lastKeepAliveMs = millis();
            _pingRespPending = false;
            LOG_I(MODULE_PREFIX, "handleRxPacket MQTT connected to %s", _dnsResolver.getHostname());
            subscribeToTopics();
            break;
        }
        case MQTTProtocol::PACKET_PUBACK:
        {
            for (auto it = _pubInFlight.begin(); it != _pubInFlight.end(); ++it)
            {
                if (it->packetID == packet.packetID)
                {
                    _pubInFlight.erase(it);
                    _pubAckedCount++;
                    break;
                }
            }
            break;
        }
        case MQTTProtocol::PACKET_PUBLISH:
            handleRxPublish(packet);
            break;
        case MQTTProtocol::PACKET_SUBACK:
        {
#ifdef WARN_MQTT_SUBSCRIBE_FAILED
            if (packet.returnCode == MQTTProtocol::SUBACK_FAILURE)
                LOG_W(MODULE_PREFIX, "handleRxPacket subscription refused packetID %d", packet.packetID);
#endif
            break;
        }
        case MQTTProtocol::PACKET_PINGRESP:
            _pingRespPending = false;
            break;
        default:
            break;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle a message published to a topic this client subscribed to
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftMQTTClient::handleRxPublish(const MQTTProtocol::RxPacket& packet)
{
    // Acknowledge QoS 1 (the subscriptions request at most QoS 1)
    if (packet.qos() > 0)
    {
        std::vector<uint8_t> msgBuf;
        _mqttProtocol.encodeMQTTPubAck(msgBuf, packet.packetID);
        if (!sendTxData(msgBuf))
        {
            closeConnection();
            return;
        }
    }

    // Find the inbound topic it was received on
    for (const TopicInfo& topicInfo : _topicList)
    {
        if (!topicInfo.isInbound || !MQTTProtocol::topicMatchesFilter(packet.topic.c_str(), topicInfo.topicFilter.c_str()))
            continue;
        _rxMsgCount++;
#ifdef DEBUG_MQTT_CLIENT_RX
        LOG_I(MODULE_PREFIX, "handleRxPublish topic %s name %s len %d",
                    packet.topic.c_str(), topicInfo.topicName.c_str(), (int)packet.payload.size());
#endif
        if (_rxMsgCB)
            _rxMsgCB(topicInfo.topicName, packet.payload.data(), packet.payload.size());
        break;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send data over socket
// Returns false if the data could not be sent (the connection should be closed as a partial packet may have been sent)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftMQTTClient::sendTxData(const std::vector<uint8_t>& txData)
{
    if (_clientHandle < 0)
        return false;
    int rslt = send(_clientHandle, txData.data(), txData.size(), 0);
    if (rslt < 0)
    {
        if (Raft::isTimeout(millis(), _internalTxDataSendErrorLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
        {
            _internalTxDataSendErrorLastTime = millis();
            LOG_W(MODULE_PREFIX, "sendTxData send error %d", errno);
        }
        return false;
    }
    else if (rslt != (int)txData.size())
    {
        if (Raft::isTimeout(millis(), _internalTxDataSendLenLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
        {
            _internalTxDataSendLenLastTime = millis();
            LOG_W(MODULE_PREFIX, "sendTxData sent length %d != frame length %d", rslt, (int)txData.size());
        }
        return false;
    }

#ifdef DEBUG_SEND_DATA
    // Debug
    String txDataStr;
    Raft::getHexStrFromBytes(txData.data(), txData.size(), txDataStr);
    LOG_I(MODULE_PREFIX, "sendTxData OK %s", txDataStr.c_str());
#endif

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Subscribe to topics
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftMQTTClient::subscribeToTopics()
{
#ifdef DEBUG_MQTT_GENERAL
    LOG_I(MODULE_PREFIX, "subscribeToTopics topics %d", _topicList.size());
#endif

    // Iterate list and subscribe to inbound topics
    for (TopicInfo& topicInfo : _topicList)
    {
#ifdef DEBUG_MQTT_TOPIC_DETAIL
        LOG_I(MODULE_PREFIX, "subscribeToTopics topic isInbound %d filter %s QoS %d",
                topicInfo.isInbound, topicInfo.topicFilter.c_str(), topicInfo.qos);
#endif

        if (topicInfo.isInbound)
        {
            // Send SUBSCRIBE packet
            std::vector<uint8_t> msgBuf;
            _mqttProtocol.encodeMQTTSubscribe(msgBuf, topicInfo.topicFilter.c_str(), topicInfo.qos);
            if (!sendTxData(msgBuf))
            {
                closeConnection();
                return;
            }
        }
    }
}
