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

// Debug
#define WARN_MQTT_DNS_LOOKUP_FAILED
// #define DEBUG_MQTT_GENERAL
// #define DEBUG_MQTT_CONNECTION
// #define DEBUG_SEND_DATA
// #define DEBUG_MQTT_CLIENT_RX
// #define DEBUG_MQTT_TOPIC_DETAIL
// #define DEBUG_MQTT_DNS_LOOKUP
// #define DEBUG_MQTT_SOCKET_CREATE

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftMQTTClient::RaftMQTTClient() :
            _mqttProtocol(
                std::bind(&RaftMQTTClient::putDataCB, this, std::placeholders::_1, std::placeholders::_2),
                std::bind(&RaftMQTTClient::frameRxCB, this, std::placeholders::_1, std::placeholders::_2))
{
    // Settings
    _isEnabled = false;
    _brokerPort = DEFAULT_MQTT_PORT;
    _keepAliveSecs = MQTT_DEFAULT_KEEPALIVE_TIME_SECS;

    // Vars
    _connState = MQTT_STATE_DISCONNECTED;
    _clientHandle = 0;
    _lastConnStateChangeMs = 0;
    _rxFrameMaxLen = MQTT_DEFAULT_FRAME_MAX_LEN;
    _lastKeepAliveMs = 0;
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

void RaftMQTTClient::setup(bool isEnabled, const char *brokerHostname, uint32_t brokerPort, const char* clientID)
{
    // Disconnect if required
    disconnect();

    // Clear list of topics
    _topicList.clear();

    // Store settings
    _isEnabled = isEnabled;
    _brokerPort = brokerPort;
    _clientID = clientID;

    // DNS resolver
    _dnsResolver.setHostname(brokerHostname);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add topic
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftMQTTClient::addTopic(const char* topicName, bool isInbound, const char* topicFilter, uint8_t qos)
{
    // Create record
    TopicInfo topicInfo = {topicName, isInbound, topicFilter, qos};
    _topicList.push_back(topicInfo);
#ifdef DEBUG_MQTT_GENERAL
    LOG_I(MODULE_PREFIX, "addTopic name %s isInbound %s path %s qos %d", topicName, isInbound ? "Y" : "N", topicFilter, qos);
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
    bool isError = false;
    bool connClosed = false;
    switch (_connState)
    {
        case MQTT_STATE_DISCONNECTED:
        {
            // See if it's time to try to connect
            if (Raft::isTimeout(millis(), _lastConnStateChangeMs, MQTT_RETRY_CONNECT_TIME_MS))
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
                // Go back to connecting
                _connState = MQTT_STATE_DISCONNECTED;
                _lastConnStateChangeMs = millis();
                ESP_LOGW(MODULE_PREFIX, "loop socket select error %d", errno);
                isError = true;
                break;
            }

            // Check for max time waiting for socket to be ready
            if (Raft::isTimeout(millis(), _lastConnStateChangeMs, MQTT_RETRY_CONNECT_TIME_MS))
            {
                // Go back to connecting
                _connState = MQTT_STATE_DISCONNECTED;
                _lastConnStateChangeMs = millis();
                ESP_LOGW(MODULE_PREFIX, "loop socket select timeout");
                isError = true;
                break;
            }

            // Still waiting to connect
            if (selectErr == 0)
            {
                if (Raft::isTimeout(millis(), _internalSocketCreateSlowLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
                {
                    _internalSocketCreateSlowLastTime = millis();
                    ESP_LOGW(MODULE_PREFIX, "loop socket select still waiting");
                }
                break;
            }

            // Connected
            ESP_LOGI(MODULE_PREFIX, "loop connId %d CONNECTED to %s", _clientHandle, _dnsResolver.getHostname());

            // Send MQTT CONNECT packet
            std::vector<uint8_t> msgBuf;
            _mqttProtocol.encodeMQTTConnect(msgBuf, _keepAliveSecs, _clientID.c_str());

            // Send data packet
            sendTxData(msgBuf, isError, connClosed);
            _connState = MQTT_STATE_MQTT_CONN_SENT;
            _lastConnStateChangeMs = millis();
            break;
        }
        case MQTT_STATE_MQTT_CONN_SENT:
        {
            // Check for data
            std::vector<uint8_t> rxData;
            bool dataRxOk = getRxData(rxData, isError, connClosed);
            if (dataRxOk)
            {
                // Handle rx data
                String rxDataStr;
                Raft::getHexStrFromBytes(rxData.data(), rxData.size(), rxDataStr);
#ifdef DEBUG_MQTT_CLIENT_RX
                ESP_LOGI(MODULE_PREFIX, "loop rx %s", rxDataStr.c_str());
#endif

                // Check response
                if (_mqttProtocol.checkForConnAck(rxData, isError))
                {
                    // Perform subscriptions
                    subscribeToTopics();

                    // Now connected
                    _connState = MQTT_STATE_MQTT_CONNECTED;
                    _lastConnStateChangeMs = millis();
                    _lastKeepAliveMs = millis();
                }
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

                // Send packet
                sendTxData(msgBuf, isError, connClosed);
                _lastKeepAliveMs = millis();
            }

            // Check for data
            std::vector<uint8_t> rxData;
            bool dataRxOk = getRxData(rxData, isError, connClosed);
            if (dataRxOk)
            {
                // Handle rx data
                String rxDataStr;
                Raft::getHexStrFromBytes(rxData.data(), rxData.size(), rxDataStr);
#ifdef DEBUG_MQTT_CLIENT_RX
                ESP_LOGI(MODULE_PREFIX, "loop rx %s", rxDataStr.c_str());
#endif
            }
            break;
        }
    }

    // Error handling
    if (isError || connClosed)
    {
        if (isError)
        {
            close(_clientHandle);
            if (Raft::isTimeout(millis(), _internalClosedErrorLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
            {
                _internalClosedErrorLastTime = millis();
                ESP_LOGW(MODULE_PREFIX, "loop ERROR connId %d CLOSED", _clientHandle);
            }
        }
        // Conn closed so we'll need to retry sometime later
        _connState = MQTT_STATE_DISCONNECTED;
        _lastConnStateChangeMs = millis();
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Deinit
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftMQTTClient::disconnect()
{
    // Check if already connected
    if (_connState == MQTT_STATE_DISCONNECTED)
        return;

    // Close socket
    close(_clientHandle);
#ifdef DEBUG_MQTT_CONNECTION
    ESP_LOGI(MODULE_PREFIX, "disconnect connId %d CLOSED", _clientHandle);
#endif
    _connState = MQTT_STATE_DISCONNECTED;
    _lastConnStateChangeMs = millis();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Publish to topic
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftMQTTClient::publishToTopic(const String& topicName, const String& msgStr)
{
    // Check connected
    if (_connState != MQTT_STATE_MQTT_CONNECTED)
        return false;

    // Find topic
    String topicFilter = "";
    for (TopicInfo& topicInfo : _topicList)
    {
        if (topicName.equals(topicInfo.topicName))
        {
            topicFilter = topicInfo.topicFilter;
            break;
        }
    }
    if (topicFilter.length() == 0)
        return false;

    // Form PUBLISH packet
    std::vector<uint8_t> msgBuf;
    _mqttProtocol.encodeMQTTPublish(msgBuf, topicFilter.c_str(), msgStr.c_str());

    // Send packet
    bool isError = false;
    bool connClosed = false;
    return sendTxData(msgBuf, isError, connClosed);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Put data to interface callback function
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftMQTTClient::putDataCB(const uint8_t *pBuf, unsigned bufLen)
{

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Frame rx callback function (frame received from interface)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftMQTTClient::frameRxCB(const uint8_t *pBuf, unsigned bufLen)
{

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Attempt to connect via socket
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftMQTTClient::socketConnect()
{
#ifdef DEBUG_MQTT_CONNECTION
    ESP_LOGI(MODULE_PREFIX, "sockConn attempting to connect to %s port %d", _dnsResolver.getHostname(), (int)_brokerPort);
#endif

    // Get IP address
    ip_addr_t ipAddr;
    if (!_dnsResolver.getIPAddr(ipAddr))
        return;

    // Get address info for broker
    String portStr = String(_brokerPort);
    uint64_t microsStart = micros();

    // Create socket
#ifdef DEBUG_MQTT_SOCKET_CREATE
    ESP_LOGI(MODULE_PREFIX, "sockConn creating socket");
#endif
    _clientHandle = socket(AF_INET, SOCK_STREAM, 0);
    if (_clientHandle < 0)
    {
        if (Raft::isTimeout(millis(), _internalSocketCreateErrorLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
        {
            _internalSocketCreateErrorLastTime = millis();
            ESP_LOGW(MODULE_PREFIX, "sockConn FAIL sock create errno %d hostname %s addr %s port %d", 
                        errno, _dnsResolver.getHostname(), ipaddr_ntoa(&ipAddr), (int)_brokerPort);
        }
        return;
    }

    // Set non-blocking
    int flags = fcntl(_clientHandle, F_GETFL, 0);
    if (flags < 0)
    {
        if (Raft::isTimeout(millis(), _internalSocketFcntlErrorLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
        {
            _internalSocketFcntlErrorLastTime = millis();
            ESP_LOGW(MODULE_PREFIX, "sockConn FAIL fcntl get errno %d hostname %s addr %s port %d", 
                            errno, _dnsResolver.getHostname(), ipaddr_ntoa(&ipAddr), (int)_brokerPort);
        }
        close(_clientHandle);
        return;
    }
    flags |= O_NONBLOCK;
    if (fcntl(_clientHandle, F_SETFL, flags) < 0)
    {
        if (Raft::isTimeout(millis(), _internalSocketFcntlErrorLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
        {
            _internalSocketFcntlErrorLastTime = millis();
            ESP_LOGW(MODULE_PREFIX, "sockConn FAIL fcntl set errno %d hostname %s addr %s port %d", 
                            errno, _dnsResolver.getHostname(), ipaddr_ntoa(&ipAddr), (int)_brokerPort);
        }
        close(_clientHandle);
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
            if (Raft::isTimeout(millis(), _internalSocketConnErrorLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
            {
                _internalSocketConnErrorLastTime = millis();
                ESP_LOGW(MODULE_PREFIX, "sockConn connect error %d", errno);
            }
            close(_clientHandle);
            return;
        }
    }

    // Set state
    _connState = MQTT_STATE_SOCK_CONN_REQD;
    _lastConnStateChangeMs = millis();
#ifdef DEBUG_MQTT_CONNECTION
    ESP_LOGI(MODULE_PREFIX, "sockConn connId %d result %s", 
                _clientHandle, connectErr < 0 ? "in progress" : "connected OK");
#endif

    // Debug
    uint64_t microsEnd = micros();
    ESP_LOGI(MODULE_PREFIX, "sockConn took %d ms", int((microsEnd - microsStart) / 1000));

    // Done
    return;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Attempt to connect via socket
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftMQTTClient::getRxData(std::vector<uint8_t>& rxData, bool& isError, bool& connClosed)
{
    // Create data buffer
    isError = false;
    connClosed = false;
    uint8_t* pBuf = new uint8_t[_rxFrameMaxLen];
    if (!pBuf)
    {
        if (Raft::isTimeout(millis(), _internalRxDataAllocErrorLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
        {
            _internalRxDataAllocErrorLastTime = millis();
            ESP_LOGE(MODULE_PREFIX, "getRxData failed alloc");
        }
        return false;
    }

    // Check for data
    int32_t bufLen = recv(_clientHandle, pBuf, _rxFrameMaxLen, MSG_DONTWAIT);

    // Handle error conditions
    if (bufLen < 0)
    {
        switch(errno)
        {
            case EWOULDBLOCK:
                bufLen = 0;
                break;
            default:
                if (Raft::isTimeout(millis(), _internalRxDataReadErrorLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
                {
                    _internalRxDataReadErrorLastTime = millis();
                    ESP_LOGW(MODULE_PREFIX, "getRxData read error %d", errno);
                }
                isError = true;
                break;
        }
        delete [] pBuf;
        return false;
    }

    // Handle connection closed
    if (bufLen == 0)
    {
        if (Raft::isTimeout(millis(), _internalRxDataConnClosedLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
        {
            _internalRxDataConnClosedLastTime = millis();
            ESP_LOGW(MODULE_PREFIX, "getRxData conn closed %d", errno);
        }
        connClosed = true;
        delete [] pBuf;
        return false;
    }

    // Return received data
    rxData.assign(pBuf, pBuf+bufLen);
    delete [] pBuf;
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send data over socket
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftMQTTClient::sendTxData(std::vector<uint8_t>& txData, bool& isError, bool& connClosed)
{
    int rslt = send(_clientHandle, txData.data(), txData.size(), 0);
    connClosed = false;
    isError = false;
    if (rslt < 0)
    {
        if (Raft::isTimeout(millis(), _internalTxDataSendErrorLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
        {
            _internalTxDataSendErrorLastTime = millis();
            ESP_LOGW(MODULE_PREFIX, "sendTxData send error %d", errno);
        }
        isError = true;
        return false;
    }
    else if (rslt != txData.size())
    {
        if (Raft::isTimeout(millis(), _internalTxDataSendLenLastTime, INTERNAL_ERROR_LOG_MIN_GAP_MS))
        {
            _internalTxDataSendLenLastTime = millis();
            ESP_LOGW(MODULE_PREFIX, "sendTxData sent length %d != frame length %d", rslt, txData.size());
        }
        return true;
    }

#ifdef DEBUG_SEND_DATA
    // Debug
    String txDataStr;
    Raft::getHexStrFromBytes(txData.data(), txData.size(), txDataStr);
    ESP_LOGI(MODULE_PREFIX, "sendTxData %s %s", rslt == txData.size() ? "OK" : "FAIL", txDataStr.c_str());
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
            // Send CONNECT packet
            std::vector<uint8_t> msgBuf;
            _mqttProtocol.encodeMQTTSubscribe(msgBuf, topicInfo.topicFilter.c_str(), topicInfo.qos);

            // Send packet
            bool isError = false;
            bool connClosed = false;
            sendTxData(msgBuf, isError, connClosed);
        }
    }
}
