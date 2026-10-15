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
    DevicePollingInfo pollInfo;

    // First call sets the time base; the poll becomes due just after 50ms later (Raft::isTimeout is strict)
    uint64_t t = 1000 * MS;
    TEST_ASSERT(!deviceStatus.getPendingIdentPollInfo(t, pollInfo), "not due at first call");
    TEST_ASSERT(!deviceStatus.getPendingIdentPollInfo(t + 50 * MS, pollInfo), "not due at exactly the interval");
    TEST_ASSERT(deviceStatus.getPendingIdentPollInfo(t + 50 * MS + 1, pollInfo), "due after interval");
    TEST_ASSERT(deviceStatus.pollTimingStats.getIntervalCount() == 0, "first poll start has no interval");

    // A poll split by a 10ms pause: the second step must not count as a poll start
    deviceStatus.deviceIdentPolling.partialPollNextReqIdx = 1;
    deviceStatus.deviceIdentPolling.partialPollPauseAfterSendMs = 10;
    TEST_ASSERT(deviceStatus.getPendingIdentPollInfo(t + 60 * MS + 2, pollInfo), "second step due after pause");
    TEST_ASSERT(deviceStatus.pollTimingStats.getIntervalCount() == 0, "poll step is not a poll start");
    deviceStatus.deviceIdentPolling.partialPollNextReqIdx = 0;

    // Next full poll - its interval is measured from the previous poll start
    uint64_t nextStart = t + 60 * MS + 2 + 50 * MS + 1;
    TEST_ASSERT(deviceStatus.getPendingIdentPollInfo(nextStart, pollInfo), "next full poll due");
    TEST_ASSERT(deviceStatus.pollTimingStats.getIntervalCount() == 1, "one interval");
    RaftJson json(deviceStatus.pollTimingStats.getJson());
    // Start to start: 60ms (the 10ms pause is followed by a full interval, as the current scheduler does)
    TEST_ASSERT(json.getLong("meanUs", 0) == (long)(nextStart - (t + 50 * MS + 1)), "interval is start to start");

    // Clearing the device status resets the statistics
    deviceStatus.clear();
    TEST_ASSERT(deviceStatus.pollTimingStats.getIntervalCount() == 0, "clear resets poll timing");
}

int main()
{
    testIntervals();
    testMissedAndBuckets();
    testDeviceStatusRecordsFullPollStarts();
    return testSummary("test_device_poll_timing");
}
