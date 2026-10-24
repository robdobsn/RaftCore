/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// MiniHDLC tests
//
// Frames encoded and decoded again, including frames longer than 64 KB, and the frame length limit: with PSRAM,
// ProtocolRICSerial allows frames of up to 200000 bytes, which a 16-bit limit truncated to 3392. handleBuffer()
// copies runs of plain bytes in one go: it must decode exactly as handleChar() a byte at a time
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <vector>
#include <random>
#include <chrono>
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

// A stream of frames: random contents with many boundary and escape bytes, junk between frames, a frame with a bad
// CRC and one too long for the decoder
static std::vector<uint8_t> makeStream(std::mt19937& rng, uint32_t rxMaxLen)
{
    MiniHDLC enc(nullptr, nullptr, 0xE7, 0xD7, 200000, 200000);
    std::vector<uint8_t> stream;
    auto addFrame = [&](const std::vector<uint8_t>& frame, bool corrupt) {
        std::vector<uint8_t> encoded(enc.calcEncodedLen(frame.data(), frame.size()));
        uint32_t len = enc.encodeFrame(encoded.data(), encoded.size(), frame.data(), frame.size());
        encoded.resize(len);
        if (corrupt && len > 4)
            encoded[len / 2] ^= 0x01;
        stream.insert(stream.end(), encoded.begin(), encoded.end());
    };
    for (int i = 0; i < 60; i++)
    {
        uint32_t len = rng() % 3000;
        std::vector<uint8_t> frame(len);
        for (uint32_t j = 0; j < len; j++)
        {
            uint32_t r = rng() % 16;
            frame[j] = r == 0 ? 0xE7 : (r == 1 ? 0xD7 : (uint8_t)rng());
        }
        addFrame(frame, i == 20);
        if (i % 7 == 3)
            for (int j = 0; j < 50; j++)
                stream.push_back((uint8_t)rng());
    }
    addFrame(std::vector<uint8_t>(rxMaxLen + 100, 0x55), false);
    addFrame(makeFrame(100), false);
    return stream;
}

static void testBulkMatchesByteAtATime()
{
    printf("Running testBulkMatchesByteAtATime...\n");
    std::mt19937 rng(1234);
    const uint32_t RX_MAX = 5000;
    for (int rep = 0; rep < 5; rep++)
    {
        std::vector<uint8_t> stream = makeStream(rng, RX_MAX);
        std::vector<std::vector<uint8_t>> byByte, byBuffer;
        MiniHDLC hdlcByte(nullptr, [&](const uint8_t* p, unsigned n) { byByte.emplace_back(p, p + n); },
                    0xE7, 0xD7, RX_MAX, RX_MAX);
        MiniHDLC hdlcBuf(nullptr, [&](const uint8_t* p, unsigned n) { byBuffer.emplace_back(p, p + n); },
                    0xE7, 0xD7, RX_MAX, RX_MAX);
        for (uint8_t ch : stream)
            hdlcByte.handleChar(ch);
        for (size_t pos = 0; pos < stream.size(); )
        {
            size_t chunk = 1 + rng() % 3000;
            if (pos + chunk > stream.size())
                chunk = stream.size() - pos;
            hdlcBuf.handleBuffer(stream.data() + pos, chunk);
            pos += chunk;
        }
        TEST_ASSERT(byByte.size() > 50 && byByte == byBuffer, "same frames decoded");
        MiniHDLCStats* pByte = hdlcByte.getStats();
        MiniHDLCStats* pBuf = hdlcBuf.getStats();
        TEST_ASSERT(pByte->_frameCRCErrCount >= 1 && pByte->_frameCRCErrCount == pBuf->_frameCRCErrCount, "same CRC errors");
        TEST_ASSERT(pByte->_frameTooLongCount >= 1 && pByte->_frameTooLongCount == pBuf->_frameTooLongCount, "same too long");
        TEST_ASSERT(pByte->_rxBufAllocFail == 0 && pBuf->_rxBufAllocFail == 0, "too long isn't an allocation failure");
        TEST_ASSERT(pByte->_rxFrameCount == pBuf->_rxFrameCount, "same frame count");
    }
}

static void benchDecode()
{
    // Not a check - how long a 16 KB frame takes to decode each way (on this machine)
    std::vector<uint8_t> frame = makeFrame(16000);
    MiniHDLC enc(nullptr, nullptr, 0xE7, 0xD7, 200000, 200000);
    std::vector<uint8_t> encoded(enc.calcEncodedLen(frame.data(), frame.size()));
    encoded.resize(enc.encodeFrame(encoded.data(), encoded.size(), frame.data(), frame.size()));
    uint32_t frames = 0;
    MiniHDLC dec(nullptr, [&](const uint8_t*, unsigned) { frames++; }, 0xE7, 0xD7, 200000, 200000);
    const int REPS = 200;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < REPS; i++)
        for (uint8_t ch : encoded)
            dec.handleChar(ch);
    auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < REPS; i++)
        dec.handleBuffer(encoded.data(), encoded.size());
    auto t2 = std::chrono::steady_clock::now();
    printf("16 KB frame decode: byte at a time %.1f us, handleBuffer %.1f us (%u frames)\n",
            std::chrono::duration<double, std::micro>(t1 - t0).count() / REPS,
            std::chrono::duration<double, std::micro>(t2 - t1).count() / REPS, frames);
}

int main()
{
    testLimitKept();
    testFrameSizes();
    testTooLong();
    testBulkMatchesByteAtATime();
    benchDecode();
    return testSummary("test_mini_hdlc");
}
