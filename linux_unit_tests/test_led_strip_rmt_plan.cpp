/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// LED strip RMT plan tests
//
// - A frame that fits in the strip's share of TX memory gets exactly the blocks it needs (no refill).
// - A longer frame gets DMA where the chip has it (the whole frame up to DMA_MAX_SYMBOLS), then the most RMT memory.
// - The share of TX memory is split between strips; a configured memBlockSymbols is used as given.
// - DMA modes; refill detection.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <vector>
#include "testhdr.h"
#include "LEDStripRMTPlan.h"

using Plan = LEDStripRMTPlan;
using Attempts = std::vector<Plan::Attempt>;

static const Plan::ChipCaps CAPS_ESP32 = {64, 8, false};
static const Plan::ChipCaps CAPS_S3 = {48, 4, true};
static const Plan::ChipCaps CAPS_C3 = {48, 2, false};

static Attempts plan(uint32_t numPixels, const Plan::ChipCaps& caps, uint32_t numStrips = 1,
            Plan::DmaMode dmaMode = Plan::DMA_AUTO, uint32_t configMemSymbols = 0, uint32_t bytesPerPixel = 3)
{
    return Plan::attempts(Plan::frameSymbols(numPixels, bytesPerPixel), configMemSymbols, dmaMode, numStrips, caps);
}

static void testFrameSymbols()
{
    printf("Running testFrameSymbols...\n");
    TEST_ASSERT(Plan::frameSymbols(6, 3) == 145, "6 RGB pixels");
    TEST_ASSERT(Plan::frameSymbols(10, 5) == 401, "10 RGBWW pixels");
    TEST_ASSERT(Plan::frameSymbols(0, 3) == 1, "no pixels: reset only");
}

static void testFitsInRMTMemory()
{
    printf("Running testFitsInRMTMemory...\n");
    // Axiom018: 6 pixels on the S3 - all four blocks, the frame in memory
    Attempts a = plan(6, CAPS_S3);
    TEST_ASSERT((a.size() >= 1) && (a[0] == Plan::Attempt{192, false}), "S3 6 pixels: 192 symbols first");
    TEST_ASSERT(!Plan::needsRefill(Plan::frameSymbols(6, 3), a[0]), "S3 6 pixels: no refill");
    // Fallbacks after the whole frame: DMA, then 3, 2, 1 blocks
    Attempts expect6 = {{192, false}, {Plan::dmaSymbols(145), true}, {144, false}, {96, false}, {48, false}};
    TEST_ASSERT(a == expect6, "S3 6 pixels: fallbacks");
    // Smaller frames take only what they need
    TEST_ASSERT(plan(1, CAPS_S3)[0] == (Plan::Attempt{48, false}), "S3 1 pixel: one block");
    TEST_ASSERT(plan(5, CAPS_S3)[0] == (Plan::Attempt{144, false}), "S3 5 pixels: three blocks");
    TEST_ASSERT(plan(7, CAPS_S3)[0] == (Plan::Attempt{192, false}), "S3 7 pixels: four blocks");
    // ESP32: 64-symbol blocks, eight TX channels
    TEST_ASSERT(plan(21, CAPS_ESP32)[0] == (Plan::Attempt{512, false}), "ESP32 21 pixels: eight blocks");
    TEST_ASSERT(plan(2, CAPS_ESP32)[0] == (Plan::Attempt{64, false}), "ESP32 2 pixels: one block");
    // One-block frame: nothing smaller to fall back to (DMA still offered)
    Attempts one = plan(1, CAPS_S3);
    TEST_ASSERT(one.size() == 2, "S3 1 pixel: block then DMA");
}

static void testDma()
{
    printf("Running testDma...\n");
    // 8 pixels (193 symbols) don't fit in the S3's 192: DMA, whole frame
    Attempts a = plan(8, CAPS_S3);
    TEST_ASSERT(a.size() == 5, "S3 8 pixels: DMA then four sizes");
    TEST_ASSERT(a[0].withDma, "S3 8 pixels: DMA first");
    TEST_ASSERT(!Plan::needsRefill(193, a[0]), "S3 8 pixels: whole frame in DMA buffer");
    TEST_ASSERT(a[1] == (Plan::Attempt{192, false}), "S3 8 pixels: then 192 symbols");
    TEST_ASSERT(Plan::needsRefill(193, a[1]), "S3 8 pixels: 192 symbols needs refill");
    // Whole frames in the DMA buffer up to its limit, refilled beyond
    uint32_t largestWhole = 0;
    for (uint32_t numPix = 1; numPix < 200; numPix++)
    {
        uint32_t frame = Plan::frameSymbols(numPix, 3);
        Plan::Attempt dma = {Plan::dmaSymbols(frame), true};
        TEST_ASSERT((dma.memSymbols % 2 == 0) && (dma.memSymbols >= CAPS_S3.blockSymbols) &&
                    (dma.memSymbols <= Plan::DMA_MAX_SYMBOLS), "DMA buffer even, at least a block, at most the max");
        if (!Plan::needsRefill(frame, dma))
            largestWhole = numPix;
    }
    TEST_ASSERT(largestWhole == 83, "whole frames in DMA up to 83 RGB pixels");
    TEST_ASSERT(Plan::needsRefill(Plan::frameSymbols(300, 3), {Plan::dmaSymbols(Plan::frameSymbols(300, 3)), true}),
                "300 pixels refilled by DMA");
    // dma on: DMA first even when the frame fits
    Attempts on = plan(6, CAPS_S3, 1, Plan::DMA_ON);
    TEST_ASSERT(on[0].withDma && (on[1] == Plan::Attempt{192, false}), "dma on: DMA then the frame in memory");
    // dma off: never
    Attempts off = plan(8, CAPS_S3, 1, Plan::DMA_OFF);
    Attempts expectOff = {{192, false}, {144, false}, {96, false}, {48, false}};
    TEST_ASSERT(off == expectOff, "dma off: RMT memory only");
    // No DMA on the chip: largest share first
    Attempts c3 = plan(6, CAPS_C3);
    Attempts expectC3 = {{96, false}, {48, false}};
    TEST_ASSERT(c3 == expectC3, "C3 6 pixels: two blocks then one");
    TEST_ASSERT(Plan::needsRefill(145, c3[0]), "C3 6 pixels: refilled");
    TEST_ASSERT(plan(6, CAPS_C3, 1, Plan::DMA_ON) == expectC3, "C3 dma on: ignored");
}

static void testShares()
{
    printf("Running testShares...\n");
    // Two strips on the S3: two blocks each, so a 6-pixel strip needs DMA
    Attempts two = plan(6, CAPS_S3, 2);
    TEST_ASSERT(two.size() == 3, "S3 two strips: DMA, 96, 48");
    TEST_ASSERT(two[0].withDma && (two[1] == Plan::Attempt{96, false}) && (two[2] == Plan::Attempt{48, false}),
                "S3 two strips: order");
    // Two strips of 3 pixels fit in two blocks each
    TEST_ASSERT(plan(3, CAPS_S3, 2)[0] == (Plan::Attempt{96, false}), "S3 two 3-pixel strips: 96 each");
    // More strips than TX channels: one block each
    TEST_ASSERT(plan(1, CAPS_C3, 3)[0] == (Plan::Attempt{48, false}), "more strips than channels: one block");
    TEST_ASSERT(plan(1, CAPS_S3, 0)[0] == (Plan::Attempt{48, false}), "zero strips treated as one");
}

static void testConfigured()
{
    printf("Running testConfigured...\n");
    TEST_ASSERT(plan(6, CAPS_S3, 1, Plan::DMA_AUTO, 64) == (Attempts{{64, false}}), "configured 64: as given");
    TEST_ASSERT(plan(6, CAPS_S3, 1, Plan::DMA_AUTO, 192) == (Attempts{{192, false}}), "configured 192");
    TEST_ASSERT(plan(6, CAPS_S3, 1, Plan::DMA_ON, 512) == (Attempts{{512, true}}), "configured with dma on");
    TEST_ASSERT(plan(6, CAPS_C3, 1, Plan::DMA_ON, 512) == (Attempts{{512, false}}), "configured, dma on, no DMA");
    TEST_ASSERT(plan(6, CAPS_S3, 1, Plan::DMA_AUTO, 33) == (Attempts{{48, false}}), "configured below a block");
    TEST_ASSERT(plan(6, CAPS_S3, 1, Plan::DMA_AUTO, 97) == (Attempts{{98, false}}), "configured odd: rounded up even");
}

static void testParseDmaMode()
{
    printf("Running testParseDmaMode...\n");
    TEST_ASSERT(Plan::parseDmaMode("auto") == Plan::DMA_AUTO, "auto");
    TEST_ASSERT(Plan::parseDmaMode("ON") == Plan::DMA_ON, "ON");
    TEST_ASSERT(Plan::parseDmaMode("1") == Plan::DMA_ON, "1");
    TEST_ASSERT(Plan::parseDmaMode("true") == Plan::DMA_ON, "true");
    TEST_ASSERT(Plan::parseDmaMode("off") == Plan::DMA_OFF, "off");
    TEST_ASSERT(Plan::parseDmaMode("0") == Plan::DMA_OFF, "0");
    TEST_ASSERT(Plan::parseDmaMode("") == Plan::DMA_AUTO, "empty");
    TEST_ASSERT(Plan::parseDmaMode("onx") == Plan::DMA_AUTO, "unknown");
    TEST_ASSERT(Plan::parseDmaMode(nullptr) == Plan::DMA_AUTO, "null");
}

int main()
{
    testFrameSymbols();
    testFitsInRMTMemory();
    testDma();
    testShares();
    testConfigured();
    testParseDmaMode();
    return testSummary("test_led_strip_rmt_plan");
}
