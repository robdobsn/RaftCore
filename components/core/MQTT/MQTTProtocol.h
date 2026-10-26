/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// MQTT Protocol
// Encodes MQTT 3.1.1 client packets and decodes packets received from a broker
//
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <vector>
#include "RaftArduino.h"

class MQTTProtocol
{
public:
    // Packet types (upper nibble of the first byte of a packet)
    enum PacketType : uint8_t
    {
        PACKET_CONNECT = 1,
        PACKET_CONNACK = 2,
        PACKET_PUBLISH = 3,
        PACKET_PUBACK = 4,
        PACKET_PUBREC = 5,
        PACKET_PUBREL = 6,
        PACKET_PUBCOMP = 7,
        PACKET_SUBSCRIBE = 8,
        PACKET_SUBACK = 9,
        PACKET_UNSUBSCRIBE = 10,
        PACKET_UNSUBACK = 11,
        PACKET_PINGREQ = 12,
        PACKET_PINGRESP = 13,
        PACKET_DISCONNECT = 14,
    };

    // SUBACK return code for a failed subscription
    static const uint8_t SUBACK_FAILURE = 0x80;

    // Packet received from the broker
    class RxPacket
    {
    public:
        uint8_t type = 0;
        // Flags (lower nibble of the first byte) - for PUBLISH these are DUP, QoS and RETAIN
        uint8_t flags = 0;
        // Packet identifier (PUBLISH with QoS > 0, PUBACK, PUBREC, PUBREL, PUBCOMP, SUBACK, UNSUBACK)
        uint16_t packetID = 0;
        // CONNACK return code or the first SUBACK return code
        uint8_t returnCode = 0;
        // PUBLISH topic and payload
        String topic;
        std::vector<uint8_t> payload;
        uint8_t qos() const
        {
            return (flags >> 1) & 0x03;
        }
    };

    // Constructor
    MQTTProtocol();

    // Encode packets
    void encodeMQTTConnect(std::vector<uint8_t>& msgBuf, uint16_t keepAliveSecs, const char* clientID,
                const char* username = nullptr, const char* password = nullptr);
    void encodeMQTTPingReq(std::vector<uint8_t>& msgBuf);
    void encodeMQTTDisconnect(std::vector<uint8_t>& msgBuf);
    uint16_t encodeMQTTSubscribe(std::vector<uint8_t>& msgBuf, const char* topicFilter, uint8_t qos);
    uint16_t encodeMQTTPublish(std::vector<uint8_t>& msgBuf, const char* topicStr,
                const uint8_t* pPayload, uint32_t payloadLen, uint8_t qos, bool retain);
    void encodeMQTTPubAck(std::vector<uint8_t>& msgBuf, uint16_t packetID);

    // Received data - bytes from the broker connection are added as they arrive (they can hold part of a
    // packet or several packets) and complete packets are then taken with getRxPacket()
    // Returns false if the data is not valid MQTT (or a packet is too long) - the connection should be dropped
    bool addRxData(const uint8_t* pData, uint32_t dataLen);

    // Get the next complete packet received - returns false if there isn't one
    bool getRxPacket(RxPacket& packet);

    // Discard received data (e.g. when a new connection is started)
    void clearRxData();

    // Max length of a packet (remaining length) which will be received
    void setRxPacketMaxLen(uint32_t rxMaxLen)
    {
        _rxPacketMaxLen = rxMaxLen;
    }
    uint32_t getRxPacketMaxLen() const
    {
        return _rxPacketMaxLen;
    }

    // Check if a topic name matches a topic filter (which can contain + and # wildcards)
    static bool topicMatchesFilter(const char* topic, const char* topicFilter);

    static const uint32_t MQTT_DEFAULT_FRAME_MAX_LEN = 1024;

private:
    // Packet ID
    uint16_t _packetID = 1;

    // Received data not yet decoded
    std::vector<uint8_t> _rxBuffer;
    uint32_t _rxPacketMaxLen = MQTT_DEFAULT_FRAME_MAX_LEN;
    bool _rxDataInvalid = false;

    // Helpers
    uint16_t getNextPacketID();
    static void appendRemainingLength(std::vector<uint8_t>& buf, uint32_t remainingLen);
    static uint32_t appendString(std::vector<uint8_t>& buf, const char* pStr);
    static bool decodeRemainingLength(const std::vector<uint8_t>& buf, uint32_t& remainingLen,
                uint32_t& lenBytes, bool& isInvalid);
    static bool decodePacket(const uint8_t* pBody, uint32_t bodyLen, RxPacket& packet);

    // Debug
    static constexpr const char* MODULE_PREFIX = "MQTTProto";
};
