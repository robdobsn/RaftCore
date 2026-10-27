/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// MQTT Protocol
// Encodes MQTT 3.1.1 client packets and decodes packets received from a broker
//
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include "MQTTProtocol.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// MQTT Protocol Defs
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Fixed header flags
#define MQTT_PUBLISH_FLAG_RETAIN 0x01
#define MQTT_PUBLISH_QOS_SHIFT 1
#define MQTT_SUBSCRIBE_FLAGS 0x02

// CONNECT flags
#define MQTT_CONNECT_FLAG_CLEAN_SESSION 0x02
#define MQTT_CONNECT_FLAG_PASSWORD 0x40
#define MQTT_CONNECT_FLAG_USERNAME 0x80

// Protocol level for MQTT 3.1.1
#define MQTT_PROTOCOL_LEVEL 0x04

// Max bytes in the remaining length field
#define MQTT_REMAINING_LEN_MAX_BYTES 4

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

MQTTProtocol::MQTTProtocol()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Encode connect packet
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MQTTProtocol::encodeMQTTConnect(std::vector<uint8_t>& msgBuf, uint16_t keepAliveSecs, const char* clientID,
            const char* username, const char* password)
{
    // Clean session with no Will - username and password are optional (a password is only sent with a username)
    bool hasUsername = username && (username[0] != 0);
    bool hasPassword = hasUsername && password && (password[0] != 0);

    // Variable header: protocol name, level, flags and keep-alive
    std::vector<uint8_t> body = {0x00, 0x04, 'M', 'Q', 'T', 'T', MQTT_PROTOCOL_LEVEL};
    uint8_t connFlags = MQTT_CONNECT_FLAG_CLEAN_SESSION;
    if (hasUsername)
        connFlags |= MQTT_CONNECT_FLAG_USERNAME;
    if (hasPassword)
        connFlags |= MQTT_CONNECT_FLAG_PASSWORD;
    body.push_back(connFlags);
    body.push_back(keepAliveSecs / 256);
    body.push_back(keepAliveSecs % 256);

    // Payload: client ID, username, password
    appendString(body, clientID ? clientID : "");
    if (hasUsername)
        appendString(body, username);
    if (hasPassword)
        appendString(body, password);

    // Fixed header
    msgBuf = {(uint8_t)(PACKET_CONNECT << 4)};
    appendRemainingLength(msgBuf, body.size());
    msgBuf.insert(msgBuf.end(), body.begin(), body.end());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Encode ping-req packet
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MQTTProtocol::encodeMQTTPingReq(std::vector<uint8_t>& msgBuf)
{
    msgBuf = {(uint8_t)(PACKET_PINGREQ << 4), 0};
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Encode disconnect packet
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MQTTProtocol::encodeMQTTDisconnect(std::vector<uint8_t>& msgBuf)
{
    msgBuf = {(uint8_t)(PACKET_DISCONNECT << 4), 0};
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Encode subscription
// Returns the packet ID used
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint16_t MQTTProtocol::encodeMQTTSubscribe(std::vector<uint8_t>& msgBuf, const char* topicFilter, uint8_t qos)
{
    // Body: packet ID, topic filter and requested QoS
    uint16_t packetID = getNextPacketID();
    std::vector<uint8_t> body = {(uint8_t)(packetID / 256), (uint8_t)(packetID % 256)};
    appendString(body, topicFilter);
    body.push_back(qos > 1 ? 1 : qos);

    // Fixed header
    msgBuf = {(uint8_t)((PACKET_SUBSCRIBE << 4) | MQTT_SUBSCRIBE_FLAGS)};
    appendRemainingLength(msgBuf, body.size());
    msgBuf.insert(msgBuf.end(), body.begin(), body.end());
    return packetID;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Encode publish
// QoS 0 and 1 are supported (QoS 2 is sent as QoS 1)
// Returns the packet ID used (0 for QoS 0 as there is none)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint16_t MQTTProtocol::encodeMQTTPublish(std::vector<uint8_t>& msgBuf, const char* topicStr,
            const uint8_t* pPayload, uint32_t payloadLen, uint8_t qos, bool retain)
{
    if (qos > 1)
        qos = 1;
    if (!pPayload)
        payloadLen = 0;

    // Fixed header
    uint8_t headerByte = (PACKET_PUBLISH << 4) | (qos << MQTT_PUBLISH_QOS_SHIFT);
    if (retain)
        headerByte |= MQTT_PUBLISH_FLAG_RETAIN;
    uint32_t topicLen = strlen(topicStr);
    uint32_t remainingLen = 2 + topicLen + (qos > 0 ? 2 : 0) + payloadLen;
    msgBuf = {headerByte};
    appendRemainingLength(msgBuf, remainingLen);
    msgBuf.reserve(msgBuf.size() + remainingLen);

    // Topic name
    appendString(msgBuf, topicStr);

    // Packet ID (only present for QoS > 0)
    uint16_t packetID = 0;
    if (qos > 0)
    {
        packetID = getNextPacketID();
        msgBuf.push_back(packetID / 256);
        msgBuf.push_back(packetID % 256);
    }

    // Payload
    if (payloadLen > 0)
        msgBuf.insert(msgBuf.end(), pPayload, pPayload + payloadLen);
    return packetID;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Encode publish acknowledgement (for a QoS 1 message received)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MQTTProtocol::encodeMQTTPubAck(std::vector<uint8_t>& msgBuf, uint16_t packetID)
{
    msgBuf = {(uint8_t)(PACKET_PUBACK << 4), 2, (uint8_t)(packetID / 256), (uint8_t)(packetID % 256)};
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add received data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool MQTTProtocol::addRxData(const uint8_t* pData, uint32_t dataLen)
{
    if (_rxDataInvalid)
        return false;
    if (pData && (dataLen > 0))
        _rxBuffer.insert(_rxBuffer.end(), pData, pData + dataLen);

    // Check the header of the first packet as far as possible so that invalid data is detected immediately
    uint32_t remainingLen = 0;
    uint32_t lenBytes = 0;
    bool isInvalid = false;
    if (decodeRemainingLength(_rxBuffer, remainingLen, lenBytes, isInvalid) && (remainingLen > _rxPacketMaxLen))
        isInvalid = true;
    if (isInvalid)
    {
        _rxDataInvalid = true;
        _rxBuffer.clear();
        return false;
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get the next complete packet received
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool MQTTProtocol::getRxPacket(RxPacket& packet)
{
    // Check for a complete packet
    uint32_t remainingLen = 0;
    uint32_t lenBytes = 0;
    bool isInvalid = false;
    if (!decodeRemainingLength(_rxBuffer, remainingLen, lenBytes, isInvalid))
    {
        if (isInvalid)
        {
            _rxDataInvalid = true;
            _rxBuffer.clear();
        }
        return false;
    }
    if (remainingLen > _rxPacketMaxLen)
    {
        _rxDataInvalid = true;
        _rxBuffer.clear();
        return false;
    }
    uint32_t packetLen = 1 + lenBytes + remainingLen;
    if (_rxBuffer.size() < packetLen)
        return false;

    // Decode the packet
    packet = RxPacket();
    packet.type = _rxBuffer[0] >> 4;
    packet.flags = _rxBuffer[0] & 0x0f;
    bool isValid = decodePacket(_rxBuffer.data() + 1 + lenBytes, remainingLen, packet);

    // Remove from the buffer
    _rxBuffer.erase(_rxBuffer.begin(), _rxBuffer.begin() + packetLen);
    if (!isValid)
    {
        _rxDataInvalid = true;
        _rxBuffer.clear();
        return false;
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Discard received data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MQTTProtocol::clearRxData()
{
    _rxBuffer.clear();
    _rxDataInvalid = false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if a topic name matches a topic filter
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool MQTTProtocol::topicMatchesFilter(const char* topic, const char* topicFilter)
{
    if (!topic || !topicFilter)
        return false;

    // Topics starting with $ (e.g. $SYS) are only matched by filters which start with $
    if ((topic[0] == '$') && (topicFilter[0] != '$'))
        return false;

    // Compare level by level
    const char* pTopic = topic;
    const char* pFilter = topicFilter;
    while (true)
    {
        // Multi-level wildcard matches this level and everything below it (including the parent level)
        if ((pFilter[0] == '#') && (pFilter[1] == 0))
            return true;

        // Find the end of this level in each
        const char* pTopicEnd = strchr(pTopic, '/');
        const char* pFilterEnd = strchr(pFilter, '/');
        size_t topicLevelLen = pTopicEnd ? (size_t)(pTopicEnd - pTopic) : strlen(pTopic);
        size_t filterLevelLen = pFilterEnd ? (size_t)(pFilterEnd - pFilter) : strlen(pFilter);

        // Single-level wildcard matches any one level, otherwise the level must be the same
        bool isSingleLevelWildcard = (filterLevelLen == 1) && (pFilter[0] == '+');
        if (!isSingleLevelWildcard)
        {
            if ((topicLevelLen != filterLevelLen) || (strncmp(pTopic, pFilter, topicLevelLen) != 0))
                return false;
        }

        // Move to the next level
        if (!pTopicEnd && !pFilterEnd)
            return true;
        if (!pFilterEnd)
            return false;
        if (!pTopicEnd)
        {
            // "a/#" also matches "a"
            return strcmp(pFilterEnd + 1, "#") == 0;
        }
        pTopic = pTopicEnd + 1;
        pFilter = pFilterEnd + 1;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get next packet ID (never 0)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint16_t MQTTProtocol::getNextPacketID()
{
    uint16_t packetID = _packetID;
    _packetID++;
    if (_packetID == 0)
        _packetID++;
    return packetID;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Append remaining length (variable length encoding)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void MQTTProtocol::appendRemainingLength(std::vector<uint8_t>& buf, uint32_t remainingLen)
{
    do
    {
        uint8_t lenByte = remainingLen % 128;
        remainingLen = remainingLen / 128;
        if (remainingLen > 0)
            lenByte |= 0x80;
        buf.push_back(lenByte);
    } while (remainingLen > 0);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Append length-prefixed string to buffer
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t MQTTProtocol::appendString(std::vector<uint8_t>& buf, const char* pStr)
{
    uint32_t strLength = strlen(pStr);
    buf.push_back(strLength / 256);
    buf.push_back(strLength % 256);
    buf.insert(buf.end(), (const uint8_t*)pStr, (const uint8_t*)pStr + strLength);
    return strLength + 2;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Decode the remaining length of the packet at the start of the buffer
// Returns false if there isn't enough data yet (isInvalid is set if the length can never be valid)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool MQTTProtocol::decodeRemainingLength(const std::vector<uint8_t>& buf, uint32_t& remainingLen,
            uint32_t& lenBytes, bool& isInvalid)
{
    remainingLen = 0;
    lenBytes = 0;
    isInvalid = false;
    uint32_t multiplier = 1;
    for (uint32_t i = 1; i < buf.size(); i++)
    {
        lenBytes++;
        remainingLen += (buf[i] & 0x7f) * multiplier;
        if ((buf[i] & 0x80) == 0)
            return true;
        if (lenBytes >= MQTT_REMAINING_LEN_MAX_BYTES)
        {
            isInvalid = true;
            return false;
        }
        multiplier *= 128;
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Decode the body (variable header and payload) of a packet whose type and flags are already set
// Returns false if the packet is malformed
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool MQTTProtocol::decodePacket(const uint8_t* pBody, uint32_t bodyLen, RxPacket& packet)
{
    switch (packet.type)
    {
        case PACKET_CONNACK:
        {
            // Acknowledge flags and return code
            if (bodyLen < 2)
                return false;
            packet.returnCode = pBody[1];
            return true;
        }
        case PACKET_PUBLISH:
        {
            // Topic name
            if (bodyLen < 2)
                return false;
            uint32_t topicLen = (pBody[0] << 8) | pBody[1];
            uint32_t pos = 2 + topicLen;
            if (pos > bodyLen)
                return false;
            packet.topic = String((const char*)pBody + 2, topicLen);

            // Packet ID (only present for QoS > 0)
            if (packet.qos() > 0)
            {
                if (pos + 2 > bodyLen)
                    return false;
                packet.packetID = (pBody[pos] << 8) | pBody[pos + 1];
                pos += 2;
            }

            // Payload
            packet.payload.assign(pBody + pos, pBody + bodyLen);
            return true;
        }
        case PACKET_PUBACK:
        case PACKET_PUBREC:
        case PACKET_PUBREL:
        case PACKET_PUBCOMP:
        case PACKET_UNSUBACK:
        {
            if (bodyLen < 2)
                return false;
            packet.packetID = (pBody[0] << 8) | pBody[1];
            return true;
        }
        case PACKET_SUBACK:
        {
            // Packet ID and a return code for each topic filter
            if (bodyLen < 3)
                return false;
            packet.packetID = (pBody[0] << 8) | pBody[1];
            packet.returnCode = pBody[2];
            return true;
        }
        default:
            // PINGRESP and anything else - nothing to decode
            return true;
    }
}
