/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// SimpleBuffer tests
//
// SimpleBuffer (MiniHDLC's frame buffers) grows only when the memory is there - an allocation that fails can't be
// detected on the device (exceptions are disabled) and would crash - and clear() releases a buffer grown by an
// unusually long frame. The memory free is set by the test. Also frames through MiniHDLC, which uses it.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <vector>
#include "testhdr.h"
#include "SimpleBuffer.h"
#include "MiniHDLC.h"

// A SimpleBuffer with the memory free set by the test
class TestBuffer : public SimpleBuffer
{
public:
    TestBuffer(unsigned maxLen, uint32_t memFree) : SimpleBuffer(maxLen), memFree(memFree) {}
    uint32_t memFree;
protected:
    virtual uint32_t largestFreeBlock() const override
    {
        return memFree;
    }
};

static void testGrowsAndKeepsData()
{
    printf("Running testGrowsAndKeepsData...\n");
    TestBuffer buf(200000, UINT32_MAX);
    bool allSet = true;
    for (unsigned i = 0; i < 50000; i++)
        allSet = buf.setAt(i, (uint8_t)(i * 7)) && allSet;
    TEST_ASSERT(allSet && buf.size() == 50000, "50000 bytes set");
    bool intact = true;
    for (unsigned i = 0; i < 50000; i++)
        intact = intact && buf.getAt(i) == (uint8_t)(i * 7);
    TEST_ASSERT(intact, "data intact across growth");
    TEST_ASSERT(buf.capacity() >= 50000 && buf.capacity() <= 2 * 50000, "capacity grows geometrically");
    TEST_ASSERT(!buf.setAt(200000, 1) && !buf.resize(200001), "not beyond the maximum length");
}

static void testMemoryShort()
{
    printf("Running testMemoryShort...\n");
    // Room for 5000 bytes over the margin
    TestBuffer buf(200000, SimpleBuffer::HEAP_MARGIN_BYTES + 5000);
    TEST_ASSERT(buf.resize(4000), "4000 bytes fit");
    buf.setAt(3999, 0x5a);
    // Growing to 8000 (geometric) wouldn't fit, 5000 exactly does
    TEST_ASSERT(buf.resize(5000) && buf.capacity() == 5000, "grows to just what is needed when memory is short");
    TEST_ASSERT(!buf.resize(6000) && buf.size() == 5000, "resize beyond the memory free fails, size unchanged");
    TEST_ASSERT(!buf.setAt(5000, 1) && buf.size() == 5000, "setAt beyond the memory free fails");
    TEST_ASSERT(buf.getAt(3999) == 0x5a, "data kept after a failed resize");
    // No memory at all
    TestBuffer none(200000, 1000);
    TEST_ASSERT(!none.setAt(0, 1) && none.size() == 0, "no growth without memory");
}

static void testClearReleasesLargeBuffers()
{
    printf("Running testClearReleasesLargeBuffers...\n");
    TestBuffer buf(200000, UINT32_MAX);
    buf.resize(1000);
    buf.clear();
    TEST_ASSERT(buf.size() == 0 && buf.capacity() >= 1000, "a usual size is kept");
    buf.resize(SimpleBuffer::RETAIN_CAPACITY_MAX + 1);
    buf.clear();
    TEST_ASSERT(buf.size() == 0 && buf.capacity() == 0, "a buffer grown by a long frame is released");
}

static std::vector<uint8_t> makeFrame(uint32_t len)
{
    std::vector<uint8_t> frame(len);
    for (uint32_t i = 0; i < len; i++)
        frame[i] = (uint8_t)(i * 7 + i / 251);
    return frame;
}

static void testMiniHDLCFrames()
{
    printf("Running testMiniHDLCFrames...\n");
    std::vector<std::vector<uint8_t>> received;
    MiniHDLC rx(nullptr, [&](const uint8_t* pFrame, unsigned len) { received.emplace_back(pFrame, pFrame + len); },
                0xE7, 0xD7, 60000, 60000);
    std::vector<uint8_t> wire;
    MiniHDLC tx([&](const uint8_t* pBuf, unsigned len) { wire.insert(wire.end(), pBuf, pBuf + len); }, nullptr,
                0xE7, 0xD7, 60000, 60000);

    // Frames of several sizes, one after another on the same decoder (the buffer is reused, or released)
    std::vector<uint32_t> lens = {10, 5000, 40000, 20, 33000, 100};
    for (uint32_t len : lens)
    {
        std::vector<uint8_t> frame = makeFrame(len);
        tx.sendFrame(frame.data(), frame.size());
    }
    rx.handleBuffer(wire.data(), wire.size());
    bool allOk = received.size() == lens.size();
    for (size_t i = 0; allOk && i < lens.size(); i++)
        allOk = received[i] == makeFrame(lens[i]);
    TEST_ASSERT(allOk, "frames sent and decoded");

    // A run of bytes with no frame boundary, longer than the limit, then a frame: the frame is decoded
    received.clear();
    std::vector<uint8_t> junk(70000, 0x55);
    rx.handleBuffer(junk.data(), junk.size());
    wire.clear();
    std::vector<uint8_t> frame = makeFrame(300);
    tx.sendFrame(frame.data(), frame.size());
    rx.handleBuffer(wire.data(), wire.size());
    TEST_ASSERT(received.size() == 1 && received[0] == frame, "frame decoded after a too-long run");
}

int main()
{
    testGrowsAndKeepsData();
    testMemoryShort();
    testClearReleasesLargeBuffers();
    testMiniHDLCFrames();
    return testSummary("test_simple_buffer");
}
