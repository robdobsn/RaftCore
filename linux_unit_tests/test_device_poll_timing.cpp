/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device poll timing statistics tests
//
// DevicePollTimingStats on its own, and as used by DeviceStatus::getPendingIdentPollInfo() (which records a
// poll start only at the start of a full poll, not at the later steps of a poll split by a pause).
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "testhdr.h"
#include "ArduinoWString.h"
#include "RaftJson.h"
#include "DevicePollTimingStats.h"
#include "DeviceStatus.h"

static const uint32_t MS = 1000;

static void testIntervals()
{
    printf("Running testIntervals...\n");
    DevicePollTimingStats stats;
    TEST_ASSERT(stats.getIntervalCount() == 0, "starts empty");

    // First start gives no interval; then 50ms intervals, one late by 3ms, one early by 1.5ms
    uint64_t t = 1000000;
    stats.recordPollStart(t, 50 * MS);
    TEST_ASSERT(stats.getIntervalCount() == 0, "first poll start has no interval");
    for (int i = 0; i < 8; i++)
    {
        t += 50 * MS;
        stats.recordPollStart(t, 50 * MS);
    }
    t += 53 * MS;
    stats.recordPollStart(t, 50 * MS);
    t += 48500;
    stats.recordPollStart(t, 50 * MS);
    TEST_ASSERT(stats.getIntervalCount() == 10, "ten intervals");

    RaftJson json(stats.getJson());
    TEST_ASSERT(json.getLong("cfgUs", 0) == 50000, "configured interval reported");
    TEST_ASSERT(json.getLong("n", 0) == 10, "count reported");
    TEST_ASSERT(json.getLong("minUs", 0) == 48500, "min interval");
    TEST_ASSERT(json.getLong("maxUs", 0) == 53000, "max interval");
    TEST_ASSERT(json.getLong("meanUs", 0) == 50150, "mean interval");
    TEST_ASSERT(json.getLong("missed", -1) == 0, "nothing missed");

    // Histogram: 8 on time (bucket [0,500)), 1 at +3ms (bucket [2000,4000)), 1 at -1.5ms (bucket [-2000,-1000))
    std::vector<String> hist;
    json.getArrayElems("errHist", hist);
    TEST_ASSERT(hist.size() == DevicePollTimingStats::NUM_ERR_BUCKETS, "histogram size");
    if (hist.size() == DevicePollTimingStats::NUM_ERR_BUCKETS)
    {
        TEST_ASSERT(hist[DevicePollTimingStats::bucketFor(0)].toInt() == 8, "on-time bucket");
        TEST_ASSERT(hist[DevicePollTimingStats::bucketFor(3000)].toInt() == 1, "late bucket");
        TEST_ASSERT(hist[DevicePollTimingStats::bucketFor(-1500)].toInt() == 1, "early bucket");
    }

    // 95th percentile: 9 of 10 are at or below +500us, the 10th is in [2000,4000) - so p95 upper bound is 4000
    TEST_ASSERT(stats.percentileErrUpperUs(95) == 4000, "p95 upper bound");
    TEST_ASSERT(stats.percentileErrUpperUs(80) == 500, "p80 upper bound");

    // Rate: 10 intervals over 501.5ms
    TEST_ASSERT(json.getDouble("rateHz", 0) > 19.9 && json.getDouble("rateHz", 0) < 20.0, "rate");
}

static void testBusTime()
{
    printf("Running testBusTime...\n");
    DevicePollTimingStats stats;
    TEST_ASSERT(stats.getMeanBusTimePerPollUs() == 0, "no bus time before a poll");
    // Two polls: one of 1500us, one split into steps of 1000us and 600us
    stats.recordPollStart(0, 50 * MS);
    stats.recordBusTimeUs(1500);
    stats.recordPollStart(50 * MS, 50 * MS);
    stats.recordBusTimeUs(1000);
    stats.recordBusTimeUs(600);
    TEST_ASSERT(stats.getMeanBusTimePerPollUs() == 1550, "mean bus time per poll covers all steps");
    RaftJson json(stats.getJson());
    TEST_ASSERT(json.getLong("busUs", 0) == 1550, "bus time reported");
    stats.reset();
    TEST_ASSERT(stats.getMeanBusTimePerPollUs() == 0, "reset clears bus time");
}

static void testMissedAndBuckets()
{
    printf("Running testMissedAndBuckets...\n");
    DevicePollTimingStats stats;
    stats.recordPollStart(0, 10 * MS);
    stats.recordPollStart(25 * MS, 10 * MS);    // 25ms: a whole period missed
    stats.recordPollStart(35 * MS, 10 * MS);
    RaftJson json(stats.getJson());
    TEST_ASSERT(json.getLong("missed", 0) == 1, "missed period counted");

    // Bucket boundaries
    TEST_ASSERT(DevicePollTimingStats::bucketFor(-1000000) == 0, "very early in first bucket");
    TEST_ASSERT(DevicePollTimingStats::bucketFor(-1) == DevicePollTimingStats::bucketFor(-500), "just early");
    TEST_ASSERT(DevicePollTimingStats::bucketFor(0) != DevicePollTimingStats::bucketFor(-1), "zero starts a bucket");
    TEST_ASSERT(DevicePollTimingStats::bucketFor(1000000) == DevicePollTimingStats::NUM_ERR_BUCKETS - 1, "very late in last bucket");

    // Reset
    stats.reset();
    TEST_ASSERT(stats.getIntervalCount() == 0, "reset clears");
    stats.recordPollStart(100 * MS, 10 * MS);
    TEST_ASSERT(stats.getIntervalCount() == 0, "first start after reset has no interval");
}

// DeviceStatus records a poll start at the start of each full poll only
static void testDeviceStatusRecordsFullPollStarts()
{
    printf("Running testDeviceStatusRecordsFullPollStarts...\n");
    DeviceStatus deviceStatus;
    deviceStatus.deviceTypeIndex = 1;
    deviceStatus.deviceIdentPolling.pollIntervalUs = 50 * MS;
    deviceStatus.deviceIdentPolling.pollReqs.push_back(BusRequestInfo("", 0x68));
    deviceStatus.deviceIdentPolling.pollReqs.push_back(BusRequestInfo("", 0x68));
    DevicePollingInfo pollInfo;

    // First call starts the schedule; the poll is due one interval later
    uint64_t t = 1000 * MS;
    TEST_ASSERT(!deviceStatus.getPendingIdentPollInfo(t, pollInfo), "not due at first call");
    TEST_ASSERT(deviceStatus.getPendingIdentPollInfo(t + 50 * MS, pollInfo), "due after interval");
    TEST_ASSERT(deviceStatus.pollTimingStats.getIntervalCount() == 0, "first poll start has no interval");

    // A poll split by a 10ms pause: the second step must not count as a poll start
    std::vector<uint8_t> data = {1};
    deviceStatus.deviceIdentPolling.recordPartialPollResult(1, t + 50 * MS, data, 10);
    TEST_ASSERT(deviceStatus.getPendingIdentPollInfo(t + 60 * MS, pollInfo), "second step due after pause");
    TEST_ASSERT(deviceStatus.pollTimingStats.getIntervalCount() == 0, "poll step is not a poll start");
    std::vector<uint8_t> result;
    deviceStatus.deviceIdentPolling.getPartialPollResultsAndClear(result);

    // Next full poll - due on the schedule, and its interval is measured from the previous poll start
    TEST_ASSERT(deviceStatus.getPendingIdentPollInfo(t + 100 * MS, pollInfo), "next full poll due");
    TEST_ASSERT(deviceStatus.pollTimingStats.getIntervalCount() == 1, "one interval");
    RaftJson json(deviceStatus.pollTimingStats.getJson());
    TEST_ASSERT(json.getLong("meanUs", 0) == 50000, "interval is start to start");

    // Clearing the device status resets the statistics
    deviceStatus.clear();
    TEST_ASSERT(deviceStatus.pollTimingStats.getIntervalCount() == 0, "clear resets poll timing");
}

int main()
{
    testIntervals();
    testMissedAndBuckets();
    testBusTime();
    testDeviceStatusRecordsFullPollStarts();
    return testSummary("test_device_poll_timing");
}
