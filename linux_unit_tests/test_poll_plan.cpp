/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Poll plan tests (device identification plan Stage 4, review §5.1)
//
// - Getting a due poll (getPendingPollJob) makes no heap allocation: the job shares the device's poll plan. The
//   older getPendingIdentPollInfo, which copies the polling info, is measured for comparison.
// - Both give the same schedule (the same polls due at the same times) and the same requests.
// - A plan held by a poll stays valid when the device is identified again (a new plan is made).
// - Polling info filled in without making a plan gets one at the first poll; the requests stay visible.
// - The next step of a poll split by a pause is reported in the job.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <chrono>
#include <cstdlib>
#include <new>
#include "testhdr.h"
#include "ArduinoWString.h"
#include "DeviceTypeRecords.h"
#include "DeviceStatus.h"

// Count heap allocations while enabled
static size_t allocCount = 0;
static bool countAllocs = false;
void* operator new(size_t size)
{
    if (countAllocs)
        allocCount++;
    void* p = std::malloc(size ? size : 1);
    if (!p)
        throw std::bad_alloc();
    return p;
}
void* operator new[](size_t size) { return operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

static const uint32_t INTERVAL_US = 2000;

// A device status for a catalogue record, as identification fills it in
static bool makeStatus(DeviceTypeRecords& table, const char* key, BusElemAddrType addr, DeviceStatus& status, bool makePlan)
{
    DeviceTypeIndexType idx = DEVICE_TYPE_INDEX_INVALID;
    if (!table.getRecord(key, idx))
        return false;
    status.deviceTypeIndex = idx;
    table.getPollInfo(addr, idx, status.deviceIdentPolling);
    status.deviceIdentPolling.pollIntervalUs = INTERVAL_US;
    if (makePlan)
        status.buildPollPlan();
    return true;
}

static void testNoAllocationPerPoll(const char* key)
{
    printf("Running testNoAllocationPerPoll (%s)...\n", key);
    DeviceTypeRecords table;
    DeviceStatus jobStatus, copyStatus;
    TEST_ASSERT(makeStatus(table, key, 0x6a, jobStatus, true) && makeStatus(table, key, 0x6a, copyStatus, false),
                "device statuses made");
    uint32_t numReqs = jobStatus.getNumPollRequests();
    TEST_ASSERT((numReqs > 0) && (numReqs == copyStatus.getNumPollRequests()), "same requests");

    // Prime both (the first poll check starts the schedule)
    uint64_t dueUs = 0;
    jobStatus.getNextPollDueUs(0, dueUs);
    copyStatus.getNextPollDueUs(0, dueUs);

    // The same schedule, at irregular times; count allocations and time each path
    static const uint32_t NUM_STEPS = 20000;
    uint32_t polls = 0, mismatches = 0;
    size_t jobAllocs = 0, copyAllocs = 0;
    std::chrono::nanoseconds jobTime{0}, copyTime{0};
    uint64_t timeUs = 0;
    for (uint32_t step = 0; step < NUM_STEPS; step++)
    {
        timeUs += 300 + (step * 7919) % 1500;
        DevicePollJob job;
        allocCount = 0;
        countAllocs = true;
        auto t0 = std::chrono::steady_clock::now();
        bool jobDue = jobStatus.getPendingPollJob(timeUs, job);
        auto t1 = std::chrono::steady_clock::now();
        countAllocs = false;
        jobAllocs += allocCount;
        jobTime += t1 - t0;

        DevicePollingInfo pollInfo;
        allocCount = 0;
        countAllocs = true;
        auto t2 = std::chrono::steady_clock::now();
        bool copyDue = copyStatus.getPendingIdentPollInfo(timeUs, pollInfo);
        auto t3 = std::chrono::steady_clock::now();
        countAllocs = false;
        if (copyDue)
            copyAllocs += allocCount;
        copyTime += t3 - t2;

        if (jobDue != copyDue)
            mismatches++;
        if (jobDue && copyDue)
        {
            polls++;
            if (!job.pPlan || (job.pPlan->pollReqs.size() != pollInfo.pollReqs.size()) || (job.nextReqIdx != 0))
                mismatches++;
            else if (job.pPlan->pollReqs[0].getAddress() != pollInfo.pollReqs[0].getAddress())
                mismatches++;
        }
    }
    printf("  %u polls: job %zu allocations, %.3f us per check; copy %.1f allocations and %.3f us per poll\n",
                polls, jobAllocs, jobTime.count() / 1000.0 / NUM_STEPS, polls ? (double)copyAllocs / polls : 0.0,
                copyTime.count() / 1000.0 / NUM_STEPS);
    TEST_ASSERT(polls > 1000, "polls were due");
    TEST_ASSERT(mismatches == 0, "the same polls, at the same times, with the same requests");
    TEST_ASSERT(jobAllocs == 0, "getting a poll job allocates nothing");
    TEST_ASSERT(copyAllocs > polls, "the copy allocates (for comparison)");
}

static void testPlanLifetimeAndLazyPlan()
{
    printf("Running testPlanLifetimeAndLazyPlan...\n");
    DeviceTypeRecords table;
    DeviceStatus status;
    TEST_ASSERT(makeStatus(table, "LSM6DS3", 0x6a, status, false), "status made without a plan");
    uint32_t numReqs = status.getNumPollRequests();
    TEST_ASSERT(numReqs == 2, "LSM6DS3 has two poll requests");
    uint64_t dueUs = 0;
    status.getNextPollDueUs(0, dueUs);

    // The first poll makes the plan; the requests are still visible
    DevicePollJob job;
    TEST_ASSERT(status.getPendingPollJob(dueUs, job) && job.pPlan, "poll due with a plan");
    TEST_ASSERT((status.getNumPollRequests() == numReqs) && (status.deviceIdentPolling.pollReqs.size() == 0),
                "requests moved into the plan, and still counted");
    TEST_ASSERT(job.pPlan->pollReqs[1].hasDynamicReadLen(), "the FIFO read keeps its read length expression");

    // Identified again (as another type): a held plan stays valid, the next poll gets the new one
    std::shared_ptr<const PollPlan> pHeld = job.pPlan;
    DeviceTypeIndexType vlIdx;
    TEST_ASSERT(table.getRecord("VL53L0X", vlIdx), "VL53L0X record");
    table.getPollInfo(0x29, vlIdx, status.deviceIdentPolling);
    status.deviceIdentPolling.pollIntervalUs = INTERVAL_US;
    TEST_ASSERT(status.getNumPollRequests() == 3, "new requests visible before the next poll");
    // (filling in the polling info starts its schedule again)
    DevicePollJob job2;
    uint64_t t = dueUs + INTERVAL_US;
    status.getNextPollDueUs(t, t);
    bool due = status.getPendingPollJob(t, job2);
    TEST_ASSERT(due && job2.pPlan && (job2.pPlan != pHeld), "a new plan");
    TEST_ASSERT(job2.pPlan && (job2.pPlan->pollReqs.size() == 3) && (job2.pPlan->pollReqs[0].getAddress() == 0x29), "the new requests");
    TEST_ASSERT((pHeld->pollReqs.size() == 2) && (pHeld->pollReqs[0].getAddress() == 0x6a), "the held plan is unchanged");

    // clear() drops the plan
    status.clear();
    TEST_ASSERT(!status.pPollPlan && (status.getNumPollRequests() == 0), "cleared");
}

static void testSplitPollStep()
{
    printf("Running testSplitPollStep...\n");
    DeviceTypeRecords table;
    DeviceStatus status;
    TEST_ASSERT(makeStatus(table, "LSM6DS3", 0x6a, status, true), "status made");
    status.deviceIdentPolling.pollBusHz = 400000;
    status.deviceIdentPolling.pollBusHzSlotMask = 0x10;
    uint64_t dueUs = 0;
    status.getNextPollDueUs(0, dueUs);
    DevicePollJob job;
    TEST_ASSERT(status.getPendingPollJob(dueUs, job) && (job.nextReqIdx == 0), "poll start");
    TEST_ASSERT((job.pollBusHz == 400000) && (job.pollBusHzSlotMask == 0x10), "bus speed settings in the job");

    // The first request paused for 5 ms: the next step is due then, and starts at request 1
    std::vector<uint8_t> partial = {1, 2, 3};
    status.deviceIdentPolling.recordPartialPollResult(1, dueUs, partial, 5);
    TEST_ASSERT(!status.getPendingPollJob(dueUs + 4000, job), "not due during the pause");
    TEST_ASSERT(status.getPendingPollJob(dueUs + 5000, job) && (job.nextReqIdx == 1), "next step");
}

int main()
{
    testNoAllocationPerPoll("LSM6DS3");
    testNoAllocationPerPoll("VL53L0X");
    testPlanLifetimeAndLazyPlan();
    testSplitPollStep();
    return testSummary("test_poll_plan");
}
