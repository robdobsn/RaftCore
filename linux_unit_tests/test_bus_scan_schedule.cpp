/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Bus scan schedule tests (Stage 9)
//
// - A first-list address comes round again within (first list size + one chunk) probes, however long the lower
//   lists are.
// - Every address of every list is probed at every slot position; the main bus twice as often as each slot.
// - Lists take turns by weight (1, 3, 9).
// - Background addresses are spread over the sweep period, on the main bus and every slot.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <map>
#include <set>
#include <vector>
#include "testhdr.h"
#include "BusScanSchedule.h"

static std::vector<uint8_t> range(uint8_t from, uint8_t to)
{
    std::vector<uint8_t> v;
    for (uint32_t a = from; a <= to; a++)
        v.push_back((uint8_t)a);
    return v;
}

static void testRevisitGap()
{
    printf("Running testRevisitGap...\n");
    BusScanSchedule sched;
    sched.setLists({range(0x10, 0x12), range(0x20, 0x3f), range(0x40, 0x6f)});
    std::map<uint32_t, uint32_t> lastSeen, maxGap, count;
    for (uint32_t i = 0; i < 20000; i++)
    {
        uint32_t addr = 0, pos = 0;
        bool sweep = false;
        TEST_ASSERT(sched.next(addr, pos, sweep) || false, "next");
        if (lastSeen.count(addr))
            maxGap[addr] = std::max(maxGap[addr], i - lastSeen[addr]);
        lastSeen[addr] = i;
        count[addr]++;
    }
    uint32_t worstFirst = 0;
    for (uint32_t a = 0x10; a <= 0x12; a++)
        worstFirst = std::max(worstFirst, maxGap[a]);
    // Chunks after each first-list turn: 32/3 and 48/9, rounded up
    TEST_ASSERT(worstFirst <= 3 + 11 + 6, "first list revisited within its size plus one chunk of each lower list");
    bool allSeen = true;
    for (uint32_t a = 0x10; a <= 0x6f; a++)
        if ((a <= 0x12) || (a >= 0x20))
            allSeen = allSeen && (count[a] > 0);
    TEST_ASSERT(allSeen, "every address in a list is probed");
    TEST_ASSERT((count[0x10] > count[0x20]) && (count[0x20] > count[0x40]), "higher lists probed more often");
    // The same share as scanning each lower list whole every 3 / 9 turns of the first: per-address rates 3:1 and 9:1
    double r1 = (double)count[0x10] / count[0x20], r2 = (double)count[0x10] / count[0x40];
    TEST_ASSERT((r1 > 2.8) && (r1 < 3.2) && (r2 > 8.5) && (r2 < 9.5), "per-address rates by weight");
    printf("  first-list max gap %d probes; probes 0x10 %d 0x20 %d 0x40 %d\n", (int)worstFirst, (int)count[0x10],
                (int)count[0x20], (int)count[0x40]);
}

static void testSlots()
{
    printf("Running testSlots...\n");
    BusScanSchedule sched;
    sched.setLists({range(0x10, 0x11), range(0x20, 0x23), range(0x40, 0x47)});
    sched.setNumSlots(3);
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> count;
    uint32_t sweeps = 0;
    for (uint32_t i = 0; i < 50000; i++)
    {
        uint32_t addr = 0, pos = 0;
        bool sweep = false;
        sched.next(addr, pos, sweep);
        TEST_ASSERT(pos <= 3, "slot in range");
        uint32_t slot = pos;   // 0 main bus, 1..3 slots
        count[{addr, slot}]++;
        sweeps += sweep ? 1 : 0;
    }
    bool allCovered = true;
    for (uint32_t a : {0x10, 0x11, 0x20, 0x23, 0x40, 0x47})
        for (uint32_t s = 0; s <= 3; s++)
            allCovered = allCovered && (count[{a, s}] > 0);
    TEST_ASSERT(allCovered, "every address probed on the main bus and every slot");
    TEST_ASSERT((count[{0x10, 0}] >= 2 * count[{0x10, 1}] - 2) && (count[{0x40, 0}] >= 2 * count[{0x40, 3}] - 2),
                "main bus covered twice as often as a slot");
    TEST_ASSERT(sweeps > 0, "sweeps complete");
}

// Slot weights: a slot with weight 3 is visited three times as often as one with weight 1, the visits spread
// through the cycle, every slot in every cycle; the cycle is the sum of the weights
static void testSlotWeights()
{
    printf("Running testSlotWeights...\n");
    BusScanSchedule sched;
    TEST_ASSERT((sched.getSlotSeq().size() == 2) && (sched.getSlotSeq()[0] == 0), "no slots: the main bus twice");
    sched.setLists({range(0x10, 0x11)});
    sched.setNumSlots(4);
    TEST_ASSERT(sched.numSlotPositions() == 2 + 4, "unweighted cycle");
    sched.setSlotWeights({1, 3, 1, 2});
    const std::vector<uint8_t>& seq = sched.getSlotSeq();
    TEST_ASSERT(seq.size() == 2 + 1 + 3 + 1 + 2, "cycle is the sum of the weights");
    std::map<uint32_t, uint32_t> perCycle;
    for (uint8_t s : seq)
        perCycle[s]++;
    TEST_ASSERT((perCycle[0] == 2) && (perCycle[1] == 1) && (perCycle[2] == 3) && (perCycle[3] == 1) && (perCycle[4] == 2),
                "visits per cycle by weight");
    bool noRepeat = true;
    for (uint32_t i = 0; i + 1 < seq.size(); i++)
        noRepeat = noRepeat && (seq[i] != seq[i + 1]);
    TEST_ASSERT(noRepeat, "visits spread (no slot twice in a row within a cycle)");
    std::map<uint32_t, uint32_t> count;
    for (uint32_t i = 0; i < 9000; i++)
    {
        uint32_t addr = 0, pos = 0;
        bool sweep = false;
        sched.next(addr, pos, sweep);
        count[pos]++;
    }
    TEST_ASSERT((count[2] == 3 * count[1]) && (count[4] == 2 * count[3]) && (count[0] == 2 * count[1]), "probes by weight");
    sched.setSlotWeights({1, 1});
    TEST_ASSERT(sched.numSlotPositions() == 2 + 3 + 1 + 1 + 2, "wrong count ignored");
}

static void testWeightsAndEdges()
{
    printf("Running testWeightsAndEdges...\n");
    BusScanSchedule empty;
    uint32_t addr = 0, pos = 0;
    bool sweep = false;
    TEST_ASSERT(!empty.next(addr, pos, sweep), "no lists: nothing to probe");

    // Only the lowest list has addresses: it is still scanned
    BusScanSchedule onlyLow;
    onlyLow.setLists({{}, {}, range(0x40, 0x41)});
    std::set<uint32_t> seen;
    for (int i = 0; i < 10; i++)
    {
        TEST_ASSERT(onlyLow.next(addr, pos, sweep), "only the lowest list");
        seen.insert(addr);
    }
    TEST_ASSERT(seen.size() == 2, "both addresses");

    // Weights with one-address lists: the second list gets a turn every 3, the third every 9
    BusScanSchedule weights;
    weights.setLists({{0x10}, {0x20}, {0x40}});
    std::map<uint32_t, uint32_t> count;
    for (int i = 0; i < 9000; i++)
    {
        weights.next(addr, pos, sweep);
        count[addr]++;
    }
    double r1 = (double)count[0x10] / count[0x20], r2 = (double)count[0x10] / count[0x40];
    printf("  ratios first/second %.2f first/third %.2f\n", r1, r2);
    TEST_ASSERT((r1 > 2.0) && (r1 < 4.0) && (r2 > 6.0) && (r2 < 12.0), "list weights");

    // Positions kept when the lists are set again with the same sizes
    BusScanSchedule keep;
    keep.setLists({range(0x10, 0x13)});
    keep.next(addr, pos, sweep);
    keep.next(addr, pos, sweep);
    keep.setLists({range(0x10, 0x13)});
    keep.next(addr, pos, sweep);
    TEST_ASSERT(addr == 0x12, "position kept");
}

static void testBackground()
{
    printf("Running testBackground...\n");
    BusScanSchedule sched;
    sched.setNumSlots(1);
    sched.setBackground(range(0x04, 0x0d), 2000);   // 10 addresses x 2 positions per 2 s = one probe per 100 ms
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> count;
    uint32_t probes = 0;
    for (uint32_t t = 0; t < 4000; t++)
    {
        uint32_t addr = 0, pos = 0;
        if (sched.nextBackground(t, addr, pos))
        {
            count[{addr, pos}]++;
            probes++;
        }
    }
    TEST_ASSERT(probes == 40, "one probe per interval");
    bool twice = true;
    for (uint32_t a = 0x04; a <= 0x0d; a++)
        for (uint32_t p = 0; p <= 1; p++)
            twice = twice && (count[{a, p}] == 2);
    TEST_ASSERT(twice, "each address on each position twice in two sweep periods");
    BusScanSchedule none;
    uint32_t addr = 0, pos = 0;
    TEST_ASSERT(!none.nextBackground(100, addr, pos), "no background addresses");
}

int main()
{
    testRevisitGap();
    testSlots();
    testSlotWeights();
    testWeightsAndEdges();
    testBackground();
    return testSummary("test_bus_scan_schedule");
}
