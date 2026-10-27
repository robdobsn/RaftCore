/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// MQTTProtocol tests
//
// Packets encoded as MQTT 3.1.1 specifies (CONNECT with and without credentials, PUBLISH at QoS 0 and 1 with
// and without retain, long remaining lengths, SUBSCRIBE, PUBACK) and packets received from a broker decoded -
// whether they arrive a byte at a time, several to a read or split across reads - with malformed or overlong
// packets reported so the connection can be dropped. Topic filter wildcards as MQTT defines them
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <vector>
#include <string.h>
#include "testhdr.h"
#include "MQTTProtocol.h"

static bool bytesEqual(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
    return a == b;
}

static void testConnect()
{
    printf("Running testConnect...\n");
    MQTTProtocol proto;
    std::vector<uint8_t> msg;

    // No credentials: clean session only
    proto.encodeMQTTConnect(msg, 30, "dev1");
    std::vector<uint8_t> expected = {0x10, 16, 0, 4, 'M', 'Q', 'T', 'T', 4, 0x02, 0, 30, 0, 4, 'd', 'e', 'v', '1'};
    TEST_ASSERT(bytesEqual(msg, expected), "connect without credentials");

    // Username and password
    proto.encodeMQTTConnect(msg, 60, "c", "u", "pw");
    expected = {0x10, 20, 0, 4, 'M', 'Q', 'T', 'T', 4, 0xC2, 0, 60, 0, 1, 'c', 0, 1, 'u', 0, 2, 'p', 'w'};
    TEST_ASSERT(bytesEqual(msg, expected), "connect with username and password");

    // A password without a username isn't sent
    proto.encodeMQTTConnect(msg, 60, "c", "", "pw");
    TEST_ASSERT(msg[9] == 0x02, "password without username not flagged");
    TEST_ASSERT(msg.size() == 15, "password without username not sent");
}

static void testPublish()
{
    printf("Running testPublish...\n");
    MQTTProtocol proto;
    std::vector<uint8_t> msg;
    const uint8_t payload[] = {'h', 'i'};

    // QoS 0 has no packet ID
    uint16_t packetID = proto.encodeMQTTPublish(msg, "a/b", payload, sizeof(payload), 0, false);
    std::vector<uint8_t> expected = {0x30, 7, 0, 3, 'a', '/', 'b', 'h', 'i'};
    TEST_ASSERT(packetID == 0, "qos 0 packet id 0");
    TEST_ASSERT(bytesEqual(msg, expected), "qos 0 publish");

    // QoS 1 with retain has a packet ID
    packetID = proto.encodeMQTTPublish(msg, "a/b", payload, sizeof(payload), 1, true);
    expected = {0x33, 9, 0, 3, 'a', '/', 'b', 0, 1, 'h', 'i'};
    TEST_ASSERT(packetID == 1, "qos 1 first packet id");
    TEST_ASSERT(bytesEqual(msg, expected), "qos 1 retained publish");

    // Packet IDs increase
    packetID = proto.encodeMQTTPublish(msg, "a/b", payload, sizeof(payload), 1, false);
    TEST_ASSERT(packetID == 2, "qos 1 second packet id");

    // Remaining length over 127 uses two bytes (2 + 1 topic + 2 packet ID + 300 payload = 305)
    std::vector<uint8_t> longPayload(300, 'x');
    proto.encodeMQTTPublish(msg, "t", longPayload.data(), longPayload.size(), 1, false);
    TEST_ASSERT(msg[1] == (0x80 | (305 % 128)), "long remaining length first byte");
    TEST_ASSERT(msg[2] == 305 / 128, "long remaining length second byte");
    TEST_ASSERT(msg.size() == 1 + 2 + 305, "long publish size");
}

static void testSubscribeAndPubAck()
{
    printf("Running testSubscribeAndPubAck...\n");
    MQTTProtocol proto;
    std::vector<uint8_t> msg;
    uint16_t packetID = proto.encodeMQTTSubscribe(msg, "x/+", 1);
    std::vector<uint8_t> expected = {0x82, 8, 0, 1, 0, 3, 'x', '/', '+', 1};
    TEST_ASSERT(packetID == 1, "subscribe packet id");
    TEST_ASSERT(bytesEqual(msg, expected), "subscribe");

    proto.encodeMQTTPubAck(msg, 0x1234);
    expected = {0x40, 2, 0x12, 0x34};
    TEST_ASSERT(bytesEqual(msg, expected), "puback");
}

// Bytes a broker might send: CONNACK, SUBACK, PUBACK, an inbound QoS 1 PUBLISH and a PINGRESP
static std::vector<uint8_t> brokerStream()
{
    return {
        0x20, 2, 0, 0,                                          // CONNACK accepted
        0x90, 3, 0, 1, 1,                                       // SUBACK packet 1 granted QoS 1
        0x40, 2, 0, 7,                                          // PUBACK packet 7
        0x32, 11, 0, 3, 'a', '/', 'k', 0, 9, 'u', 'p', '=', '5', // PUBLISH QoS 1 "a/k" packet 9 "up=5"
        0xD0, 0                                                 // PINGRESP
    };
}

static void checkBrokerPackets(MQTTProtocol& proto, const char* context)
{
    MQTTProtocol::RxPacket packet;
    TEST_ASSERT(proto.getRxPacket(packet) && (packet.type == MQTTProtocol::PACKET_CONNACK) &&
                (packet.returnCode == 0), context);
    TEST_ASSERT(proto.getRxPacket(packet) && (packet.type == MQTTProtocol::PACKET_SUBACK) &&
                (packet.packetID == 1) && (packet.returnCode == 1), context);
    TEST_ASSERT(proto.getRxPacket(packet) && (packet.type == MQTTProtocol::PACKET_PUBACK) &&
                (packet.packetID == 7), context);
    bool gotPublish = proto.getRxPacket(packet);
    TEST_ASSERT(gotPublish && (packet.type == MQTTProtocol::PACKET_PUBLISH) && (packet.qos() == 1) &&
                (packet.packetID == 9) && packet.topic.equals("a/k") &&
                (packet.payload == std::vector<uint8_t>({'u', 'p', '=', '5'})), context);
    TEST_ASSERT(proto.getRxPacket(packet) && (packet.type == MQTTProtocol::PACKET_PINGRESP), context);
    TEST_ASSERT(!proto.getRxPacket(packet), context);
}

static void testDecodeChunking()
{
    printf("Running testDecodeChunking...\n");
    std::vector<uint8_t> stream = brokerStream();

    // All in one read
    {
        MQTTProtocol proto;
        TEST_ASSERT(proto.addRxData(stream.data(), stream.size()), "one read accepted");
        checkBrokerPackets(proto, "one read");
    }

    // A byte at a time (no packet is available until it is complete)
    {
        MQTTProtocol proto;
        MQTTProtocol::RxPacket packet;
        TEST_ASSERT(proto.addRxData(stream.data(), 1), "first byte accepted");
        TEST_ASSERT(!proto.getRxPacket(packet), "no packet from one byte");
        for (size_t i = 1; i < stream.size(); i++)
            TEST_ASSERT(proto.addRxData(stream.data() + i, 1), "byte accepted");
        checkBrokerPackets(proto, "byte at a time");
    }

    // Split across two reads in the middle of a packet
    {
        MQTTProtocol proto;
        TEST_ASSERT(proto.addRxData(stream.data(), 17), "first part accepted");
        TEST_ASSERT(proto.addRxData(stream.data() + 17, stream.size() - 17), "second part accepted");
        checkBrokerPackets(proto, "split read");
    }

    // Connection refused
    {
        MQTTProtocol proto;
        const uint8_t refused[] = {0x20, 2, 0, 5};
        MQTTProtocol::RxPacket packet;
        proto.addRxData(refused, sizeof(refused));
        TEST_ASSERT(proto.getRxPacket(packet) && (packet.returnCode == 5), "connack refused code");
    }
}

static void testDecodeInvalid()
{
    printf("Running testDecodeInvalid...\n");

    // Remaining length longer than 4 bytes
    {
        MQTTProtocol proto;
        const uint8_t bad[] = {0x30, 0x80, 0x80, 0x80, 0x80, 0x01};
        TEST_ASSERT(!proto.addRxData(bad, sizeof(bad)), "five byte length rejected");
        MQTTProtocol::RxPacket packet;
        TEST_ASSERT(!proto.getRxPacket(packet), "nothing after invalid data");
        proto.clearRxData();
        const uint8_t ok[] = {0xD0, 0};
        TEST_ASSERT(proto.addRxData(ok, sizeof(ok)) && proto.getRxPacket(packet), "usable after clear");
    }

    // Packet longer than the limit
    {
        MQTTProtocol proto;
        proto.setRxPacketMaxLen(100);
        const uint8_t tooLong[] = {0x30, 0xC8, 0x01};   // 200
        TEST_ASSERT(!proto.addRxData(tooLong, sizeof(tooLong)), "overlong packet rejected");
    }

    // PUBLISH whose topic length runs past the end of the packet
    {
        MQTTProtocol proto;
        const uint8_t bad[] = {0x30, 4, 0, 9, 'a', 'b'};
        MQTTProtocol::RxPacket packet;
        proto.addRxData(bad, sizeof(bad));
        TEST_ASSERT(!proto.getRxPacket(packet), "malformed publish rejected");
    }
}

static void testTopicFilters()
{
    printf("Running testTopicFilters...\n");
    TEST_ASSERT(MQTTProtocol::topicMatchesFilter("a/b", "a/b"), "exact");
    TEST_ASSERT(!MQTTProtocol::topicMatchesFilter("a/b", "a/c"), "different level");
    TEST_ASSERT(!MQTTProtocol::topicMatchesFilter("a/b", "a"), "shorter filter");
    TEST_ASSERT(!MQTTProtocol::topicMatchesFilter("a", "a/b"), "longer filter");
    TEST_ASSERT(MQTTProtocol::topicMatchesFilter("a/b/c", "a/+/c"), "single level wildcard");
    TEST_ASSERT(!MQTTProtocol::topicMatchesFilter("a/b/x/c", "a/+/c"), "single level wildcard one level only");
    TEST_ASSERT(MQTTProtocol::topicMatchesFilter("a/b/c", "a/#"), "multi level wildcard");
    TEST_ASSERT(MQTTProtocol::topicMatchesFilter("a", "a/#"), "multi level wildcard matches parent");
    TEST_ASSERT(MQTTProtocol::topicMatchesFilter("a/b", "#"), "hash matches all");
    TEST_ASSERT(!MQTTProtocol::topicMatchesFilter("$SYS/x", "#"), "hash does not match $ topics");
    TEST_ASSERT(MQTTProtocol::topicMatchesFilter("a/", "a/+"), "empty level matches plus");
    TEST_ASSERT(!MQTTProtocol::topicMatchesFilter("ab", "a"), "prefix is not a match");
}

int main()
{
    testConnect();
    testPublish();
    testSubscribeAndPubAck();
    testDecodeChunking();
    testDecodeInvalid();
    testTopicFilters();
    return testSummary("test_mqtt_protocol");
}
