/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// MiniHDLC tests
//
// Frames encoded and decoded again, including frames longer than 64 KB, and the frame length limit: with PSRAM,
// ProtocolRICSerial allows frames of up to 200000 bytes, which a 16-bit limit truncated to 3392
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <vector>
#include "testhdr.h"
#include "MiniHDLC.h"

static std::vector<uint8_t> makeFrame(uint32_t len)
{
    std::vector<uint8_t> frame(len);
    for (uint32_t i = 0; i < len; i++)
        frame[i] = (uint8_t)(i * 7 + i / 251);
    return frame;
}

// Encode `frame` and decode it with a decoder limited to `rxMaxLen`; returns the frames decoded
static std::vector<std::vector<uint8_t>> roundTrip(const std::vector<uint8_t>& frame, uint32_t rxMaxLen)
{
    std::vector<std::vector<uint8_t>> received;
    MiniHDLC hdlc(nullptr, [&](const uint8_t* pFrame, unsigned len) { received.emplace_back(pFrame, pFrame + len); },
                0xE7, 0xD7, rxMaxLen, rxMaxLen);
    std::vector<uint8_t> encoded(hdlc.calcEncodedLen(frame.data(), frame.size()));
    uint32_t encodedLen = hdlc.encodeFrame(encoded.data(), encoded.size(), frame.data(), frame.size());
    hdlc.handleBuffer(encoded.data(), encodedLen);
    return received;
}

static void testLimitKept()
{
    printf("Running testLimitKept...\n");
    MiniHDLC hdlc(nullptr, nullptr, 0xE7, 0xD7, 200000, 200000);
    TEST_ASSERT(hdlc.getFrameRxMaxLen() == 200000, "rx max length 200000 kept");
}

static void testFrameSizes()
{
    printf("Running testFrameSizes...\n");
    for (uint32_t len : {10u, 3392u, 4000u, 10000u, 70000u})
    {
        std::vector<uint8_t> frame = makeFrame(len);
        auto received = roundTrip(frame, 200000);
        char name[60];
        snprintf(name, sizeof(name), "frame of %u bytes decoded", (unsigned)len);
        TEST_ASSERT(received.size() == 1 && received[0] == frame, name);
    }
}

static void testTooLong()
{
    printf("Running testTooLong...\n");
    auto received = roundTrip(makeFrame(6000), 5000);
    TEST_ASSERT(received.empty(), "frame over the limit dropped");
    received = roundTrip(makeFrame(4900), 5000);
    TEST_ASSERT(received.size() == 1, "frame under the limit decoded");
}

int main()
{
    testLimitKept();
    testFrameSizes();
    testTooLong();
    return testSummary("test_mini_hdlc");
}
