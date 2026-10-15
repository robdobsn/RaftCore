/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device poll scheduling tests
//
// DeviceStatus keeps each device's polls on a fixed schedule (the next poll is due one interval after the last
// was due, not after it ran), and DevicePollScheduler chooses which due device to poll (earliest due first).
// The bus worker is simulated: it runs a pass every few ms, and in each pass takes polls until none are due
// or the pass's poll budget (a number of polls here) is used. Each poll takes a fixed time.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <vector>
#include "testhdr.h"
#include "ArduinoWString.h"
#include "RaftJson.h"
#include "DeviceStatus.h"
#include "DevicePollScheduler.h"

static const uint32_t MS = 1000;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Simulated bus
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct SimDevice
{
    DeviceStatus status;
    bool online = true;
    uint32_t pauseAfterFirstMs = 0;
    std::vector<uint64_t> pollStartsUs;
    std::vector<uint64_t> stepStartsUs;
};

static DeviceStatus* simDeviceStatus(SimDevice& dev)
{
    return dev.online ? &dev.status : nullptr;
}

// A device polled with one request, or with a request split by a pause after the first
static void setupDevice(SimDevice& dev, uint32_t intervalUs, uint32_t pauseAfterFirstMs = 0)
{
    dev.status.deviceTypeIndex = 1;
    dev.status.deviceIdentPolling.pollIntervalUs = intervalUs;
    dev.status.deviceIdentPolling.pollReqs.push_back(BusRequestInfo("", 0x40));
    if (pauseAfterFirstMs > 0)
        dev.status.deviceIdentPolling.pollReqs.push_back(BusRequestInfo("", 0x40));
    dev.pauseAfterFirstMs = pauseAfterFirstMs;
}

class SimBus
{
public:
    SimBus(std::vector<SimDevice>& devices, uint32_t passPeriodUs, uint32_t pollTimeUs, uint32_t maxPollsPerPass)
        : _devices(devices), _passPeriodUs(passPeriodUs), _pollTimeUs(pollTimeUs), _maxPollsPerPass(maxPollsPerPass)
    {
    }

    // Run passes until endUs
    void run(uint64_t startUs, uint64_t endUs, std::vector<uint32_t>* pPollsPerPass = nullptr)
    {
        for (uint64_t passStartUs = startUs; passStartUs < endUs; passStartUs += _passPeriodUs)
        {
            uint64_t timeUs = passStartUs;
            uint32_t pollsThisPass = 0;
            while (pollsThisPass < _maxPollsPerPass)
            {
                int32_t idx = _scheduler.findEarliestDue(_devices, simDeviceStatus, timeUs);
                if (idx == DevicePollScheduler::NONE_DUE)
                    break;
                SimDevice& dev = _devices[idx];
                DevicePollingInfo pollInfo;
                if (!dev.status.getPendingIdentPollInfo(timeUs, pollInfo))
                    break;
                _scheduler.markPolled(idx);
                pollsThisPass++;
                runPoll(dev, pollInfo, timeUs);
                timeUs += _pollTimeUs;
            }
            if (pPollsPerPass)
                pPollsPerPass->push_back(pollsThisPass);
        }
    }

private:
    // As DevicePollingMgr: run the requests from the next index, stopping after one with a pause
    void runPoll(SimDevice& dev, const DevicePollingInfo& pollInfo, uint64_t timeUs)
    {
        uint32_t reqIdx = pollInfo.partialPollNextReqIdx;
        if (reqIdx == 0)
            dev.pollStartsUs.push_back(timeUs);
        else
            dev.stepStartsUs.push_back(timeUs);
        std::vector<uint8_t> data = {1, 2};
        if ((reqIdx == 0) && (dev.pauseAfterFirstMs > 0))
        {
            dev.status.deviceIdentPolling.recordPartialPollResult(1, timeUs, data, dev.pauseAfterFirstMs);
            return;
        }
        // Poll complete
        std::vector<uint8_t> result;
        dev.status.deviceIdentPolling.getPartialPollResultsAndClear(result);
    }

    std::vector<SimDevice>& _devices;
    uint32_t _passPeriodUs;
    uint32_t _pollTimeUs;
    uint32_t _maxPollsPerPass;
    DevicePollScheduler _scheduler;
};

// Mean interval between poll starts, from the second start on (the first is one interval after the start)
static double meanIntervalUs(const std::vector<uint64_t>& startsUs)
{
    if (startsUs.size() < 2)
        return 0;
    return (double)(startsUs.back() - startsUs.front()) / (startsUs.size() - 1);
}

static uint64_t maxIntervalUs(const std::vector<uint64_t>& startsUs)
{
    uint64_t maxUs = 0;
    for (size_t i = 1; i < startsUs.size(); i++)
        if (startsUs[i] - startsUs[i - 1] > maxUs)
            maxUs = startsUs[i] - startsUs[i - 1];
    return maxUs;
}

static bool withinPct(double value, double target, double pct)
{
    return (value >= target * (1 - pct / 100)) && (value <= target * (1 + pct / 100));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DeviceStatus schedule
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void testFixedPhase()
{
    printf("Running testFixedPhase...\n");
    DeviceStatus status;
    status.deviceTypeIndex = 1;
    status.deviceIdentPolling.pollIntervalUs = 50 * MS;
    status.deviceIdentPolling.pollReqs.push_back(BusRequestInfo("", 0x68));
    DevicePollingInfo pollInfo;

    // First check sets the schedule: due one interval later, and due at exactly that time
    uint64_t t0 = 1000 * MS;
    uint64_t dueUs = 0;
    TEST_ASSERT(status.getNextPollDueUs(t0, dueUs) && (dueUs == t0 + 50 * MS), "first poll due one interval on");
    TEST_ASSERT(!status.getPendingIdentPollInfo(t0 + 50 * MS - 1, pollInfo), "not due before the due time");
    TEST_ASSERT(status.getPendingIdentPollInfo(t0 + 50 * MS, pollInfo), "due at the due time");

    // Polled 7ms late: the next poll is still due on the original schedule, not 50ms after it ran
    TEST_ASSERT(status.getNextPollDueUs(t0 + 50 * MS, dueUs) && (dueUs == t0 + 100 * MS), "next due on schedule");
    TEST_ASSERT(status.getPendingIdentPollInfo(t0 + 107 * MS, pollInfo), "late poll");
    TEST_ASSERT(status.getNextPollDueUs(t0 + 107 * MS, dueUs) && (dueUs == t0 + 150 * MS), "lateness not carried on");

    // More than an interval late: no catch-up burst - the schedule restarts from now
    TEST_ASSERT(status.getPendingIdentPollInfo(t0 + 260 * MS, pollInfo), "very late poll");
    TEST_ASSERT(status.getNextPollDueUs(t0 + 260 * MS, dueUs) && (dueUs == t0 + 310 * MS), "schedule restarts from now");
    TEST_ASSERT(!status.getPendingIdentPollInfo(t0 + 261 * MS, pollInfo), "no catch-up poll");
    RaftJson json(status.pollTimingStats.getJson());
    TEST_ASSERT(json.getLong("missed", 0) == 1, "missed period counted");

    // A device with no poll requests is never due
    DeviceStatus unpolled;
    unpolled.deviceIdentPolling.pollIntervalUs = 10 * MS;
    TEST_ASSERT(!unpolled.getNextPollDueUs(t0, dueUs), "unpolled device has no due time");
    TEST_ASSERT(!unpolled.getPendingIdentPollInfo(t0 + 100 * MS, pollInfo), "unpolled device never due");
}

static void testIntervalClampAndChange()
{
    printf("Running testIntervalClampAndChange...\n");
    DeviceStatus status;
    status.deviceTypeIndex = 1;
    status.deviceIdentPolling.pollIntervalUs = 0;
    status.deviceIdentPolling.pollReqs.push_back(BusRequestInfo("", 0x68));
    DevicePollingInfo pollInfo;

    // An interval of 0 is treated as the minimum, not "every pass"
    uint64_t t0 = 1000 * MS;
    uint64_t dueUs = 0;
    status.getNextPollDueUs(t0, dueUs);
    TEST_ASSERT(dueUs == t0 + DevicePollingInfo::POLL_INTERVAL_MIN_US, "zero interval clamped");

    // Setting an interval clamps it and brings the next poll forward if the new interval is shorter
    status.setPollIntervalUs(1 * MS, t0);
    TEST_ASSERT(status.deviceIdentPolling.pollIntervalUs == DevicePollingInfo::POLL_INTERVAL_MIN_US, "set interval clamped");
    status.setPollIntervalUs(1000 * MS, t0);
    TEST_ASSERT(status.getNextPollDueUs(t0, dueUs) && (dueUs == t0 + DevicePollingInfo::POLL_INTERVAL_MIN_US),
                "longer interval doesn't delay the poll already due");
    TEST_ASSERT(status.getPendingIdentPollInfo(t0 + 2 * MS, pollInfo), "poll");
    TEST_ASSERT(status.getNextPollDueUs(t0 + 2 * MS, dueUs) && (dueUs == t0 + 1002 * MS), "then the new interval");
    status.setPollIntervalUs(10 * MS, t0 + 3 * MS);
    TEST_ASSERT(status.getNextPollDueUs(t0 + 3 * MS, dueUs) && (dueUs == t0 + 13 * MS), "shorter interval brings poll forward");
}

static void testSplitPollTiming()
{
    printf("Running testSplitPollTiming...\n");
    DeviceStatus status;
    status.deviceTypeIndex = 1;
    status.deviceIdentPolling.pollIntervalUs = 50 * MS;
    status.deviceIdentPolling.pollReqs.push_back(BusRequestInfo("", 0x61));
    status.deviceIdentPolling.pollReqs.push_back(BusRequestInfo("", 0x61));
    DevicePollingInfo pollInfo;
    uint64_t t0 = 1000 * MS;
    uint64_t dueUs = 0;
    status.getNextPollDueUs(t0, dueUs);

    // First step at 50ms, then a 10ms pause
    TEST_ASSERT(status.getPendingIdentPollInfo(t0 + 50 * MS, pollInfo), "first step");
    std::vector<uint8_t> data = {1};
    status.deviceIdentPolling.recordPartialPollResult(1, t0 + 50 * MS, data, 10);
    TEST_ASSERT(status.getNextPollDueUs(t0 + 50 * MS, dueUs) && (dueUs == t0 + 60 * MS), "second step due after pause");
    TEST_ASSERT(!status.getPendingIdentPollInfo(t0 + 59 * MS, pollInfo), "second step not due during pause");

    // The second step fails (e.g. a bus error): it is retried after the pause, not on every pass
    TEST_ASSERT(status.getPendingIdentPollInfo(t0 + 60 * MS, pollInfo), "second step");
    TEST_ASSERT(pollInfo.partialPollNextReqIdx == 1, "second step starts at request 1");
    TEST_ASSERT(!status.getPendingIdentPollInfo(t0 + 61 * MS, pollInfo), "failed step not retried at once");
    TEST_ASSERT(status.getPendingIdentPollInfo(t0 + 70 * MS, pollInfo), "failed step retried after pause");

    // Step succeeds; the next full poll is due on the original schedule (the pause doesn't push it later)
    std::vector<uint8_t> result;
    status.deviceIdentPolling.getPartialPollResultsAndClear(result);
    TEST_ASSERT(status.getNextPollDueUs(t0 + 70 * MS, dueUs) && (dueUs == t0 + 100 * MS), "next poll on schedule");
    TEST_ASSERT(status.pollTimingStats.getIntervalCount() == 0, "steps are not poll starts");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scheduler with a simulated bus
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Polls keep to their intervals over a long run, including when the interval isn't a multiple of the pass period
static void testNoDrift()
{
    printf("Running testNoDrift...\n");
    std::vector<SimDevice> devices(3);
    setupDevice(devices[0], 10 * MS);
    setupDevice(devices[1], 25 * MS);
    setupDevice(devices[2], 100 * MS);
    SimBus bus(devices, 7 * MS, 300, 4);
    bus.run(0, 10000 * MS);
    TEST_ASSERT(withinPct(meanIntervalUs(devices[0].pollStartsUs), 10 * MS, 0.2), "10ms device mean interval");
    TEST_ASSERT(withinPct(meanIntervalUs(devices[1].pollStartsUs), 25 * MS, 0.2), "25ms device mean interval");
    TEST_ASSERT(withinPct(meanIntervalUs(devices[2].pollStartsUs), 100 * MS, 0.2), "100ms device mean interval");
    // A poll is at most one pass late, so no interval is longer than the interval plus a pass (plus polls ahead)
    TEST_ASSERT(maxIntervalUs(devices[0].pollStartsUs) <= 10 * MS + 7 * MS + 1 * MS, "10ms device max interval");
    TEST_ASSERT(devices[0].pollStartsUs.size() >= 998, "10ms device poll count");
}

// The same devices with one poll per pass at the old 5ms pass (the Stage 0 baseline starvation case):
// earliest due first shares the passes, so neither device is starved
static void testEarliestDueFirstSharesPolls()
{
    printf("Running testEarliestDueFirstSharesPolls...\n");
    std::vector<SimDevice> devices(2);
    setupDevice(devices[0], 10 * MS);
    setupDevice(devices[1], 10 * MS);
    SimBus bus(devices, 5 * MS, 300, 1);
    bus.run(0, 2000 * MS);
    size_t n0 = devices[0].pollStartsUs.size();
    size_t n1 = devices[1].pollStartsUs.size();
    TEST_ASSERT((n0 >= 195) && (n1 >= 195), "both devices polled at their rate");
    TEST_ASSERT(maxIntervalUs(devices[0].pollStartsUs) <= 15 * MS, "device 0 not starved");
    TEST_ASSERT(maxIntervalUs(devices[1].pollStartsUs) <= 15 * MS, "device 1 not starved");
}

// Overloaded bus: more polls due than fit. Every device still gets polls (no starvation) and the rates fall
// evenly; nobody bursts to catch up
static void testOverloadIsFair()
{
    printf("Running testOverloadIsFair...\n");
    std::vector<SimDevice> devices(3);
    for (auto& dev : devices)
        setupDevice(dev, 5 * MS);
    SimBus bus(devices, 5 * MS, 300, 1);
    bus.run(0, 3000 * MS);
    size_t minN = SIZE_MAX, maxN = 0;
    for (auto& dev : devices)
    {
        minN = std::min(minN, dev.pollStartsUs.size());
        maxN = std::max(maxN, dev.pollStartsUs.size());
    }
    TEST_ASSERT(minN > 150, "every device polled");
    TEST_ASSERT(maxN - minN <= 2, "polls shared evenly");
}

// Earliest due first: the device that has waited longest goes first
static void testEarliestDueOrder()
{
    printf("Running testEarliestDueOrder...\n");
    std::vector<SimDevice> devices(3);
    setupDevice(devices[0], 30 * MS);
    setupDevice(devices[1], 20 * MS);
    setupDevice(devices[2], 10 * MS);
    DevicePollScheduler scheduler;
    uint64_t dueUs = 0;
    // Start the schedules at 0: due at 30, 20 and 10ms
    for (auto& dev : devices)
        dev.status.getNextPollDueUs(0, dueUs);
    TEST_ASSERT(scheduler.findEarliestDue(devices, simDeviceStatus, 9 * MS) == DevicePollScheduler::NONE_DUE, "none due");
    TEST_ASSERT(scheduler.findEarliestDue(devices, simDeviceStatus, 35 * MS) == 2, "earliest due first");
    TEST_ASSERT(DevicePollScheduler::getEarliestDueUs(devices, simDeviceStatus, 0, dueUs) && (dueUs == 10 * MS),
                "earliest due time");

    // Ties go round robin
    std::vector<SimDevice> tied(3);
    for (auto& dev : tied)
    {
        setupDevice(dev, 10 * MS);
        dev.status.getNextPollDueUs(0, dueUs);
    }
    std::vector<int32_t> order;
    for (int i = 0; i < 3; i++)
    {
        int32_t idx = scheduler.findEarliestDue(tied, simDeviceStatus, 10 * MS);
        DevicePollingInfo pollInfo;
        tied[idx].status.getPendingIdentPollInfo(10 * MS, pollInfo);
        scheduler.markPolled(idx);
        order.push_back(idx);
    }
    TEST_ASSERT((order[0] != order[1]) && (order[1] != order[2]) && (order[0] != order[2]), "ties take turns");

    // A record the bus skips (e.g. offline) is never chosen, and doesn't count for the earliest due time
    devices[2].online = false;
    TEST_ASSERT(scheduler.findEarliestDue(devices, simDeviceStatus, 35 * MS) == 1, "skipped record not chosen");
    TEST_ASSERT(DevicePollScheduler::getEarliestDueUs(devices, simDeviceStatus, 0, dueUs) && (dueUs == 20 * MS),
                "skipped record has no due time");
    for (auto& dev : devices)
        dev.online = false;
    TEST_ASSERT(!DevicePollScheduler::getEarliestDueUs(devices, simDeviceStatus, 0, dueUs), "none polled");
}

// A poll split by a pause keeps its full-poll rate
static void testSplitPollRate()
{
    printf("Running testSplitPollRate...\n");
    std::vector<SimDevice> devices(2);
    setupDevice(devices[0], 50 * MS, 5);
    setupDevice(devices[1], 20 * MS);
    SimBus bus(devices, 3 * MS, 300, 4);
    bus.run(0, 5000 * MS);
    TEST_ASSERT(withinPct(meanIntervalUs(devices[0].pollStartsUs), 50 * MS, 0.2), "split poll rate");
    TEST_ASSERT(devices[0].stepStartsUs.size() + 1 >= devices[0].pollStartsUs.size(), "every poll has its second step");
    TEST_ASSERT(withinPct(meanIntervalUs(devices[1].pollStartsUs), 20 * MS, 0.2), "other device rate");
}

int main()
{
    testFixedPhase();
    testIntervalClampAndChange();
    testSplitPollTiming();
    testNoDrift();
    testEarliestDueFirstSharesPolls();
    testOverloadIsFair();
    testEarliestDueOrder();
    testSplitPollRate();
    return testSummary("test_device_poll_scheduler");
}
