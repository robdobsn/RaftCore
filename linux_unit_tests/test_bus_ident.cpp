/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Bus element identification state tests (Stage 7)
//
// - The retry policy: Deferred at a fixed interval; NoMatch backing off from 3 s to 60 s (and due at once when the
//   catalogue changes); BusError/InitFailed backing off from 100 ms to 10 s.
// - Identity-change events: Identified, TypeChanged and Lost derived from the type last reported, as a record
//   reports them through a sequence of identifications, re-identifications and going offline.
// - BusAddrStatus from a producer that doesn't report identity changes defaults to Identified.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "testhdr.h"
#include "ArduinoWString.h"
#include "BusAddrRecord.h"

static void testRetryPolicy()
{
    printf("Running testRetryPolicy...\n");
    TEST_ASSERT(BusIdentSchedule::retryDelayMs(BusIdentState::Deferred, 1) == 3000, "deferred 3 s");
    TEST_ASSERT(BusIdentSchedule::retryDelayMs(BusIdentState::Deferred, 20) == 3000, "deferred stays 3 s");
    const uint32_t noMatch[] = {3000, 6000, 12000, 24000, 48000, 60000, 60000};
    for (uint32_t i = 0; i < sizeof(noMatch) / sizeof(noMatch[0]); i++)
        TEST_ASSERT(BusIdentSchedule::retryDelayMs(BusIdentState::NoMatch, i + 1) == noMatch[i], "noMatch backoff");
    TEST_ASSERT(BusIdentSchedule::retryDelayMs(BusIdentState::NoMatch, 255) == 60000, "noMatch cap");
    TEST_ASSERT(BusIdentSchedule::retryDelayMs(BusIdentState::BusError, 1) == 100, "bus error first");
    TEST_ASSERT(BusIdentSchedule::retryDelayMs(BusIdentState::BusError, 4) == 800, "bus error backoff");
    TEST_ASSERT(BusIdentSchedule::retryDelayMs(BusIdentState::InitFailed, 50) == 10000, "init failed cap");
    TEST_ASSERT(BusIdentSchedule::retryDelayMs(BusIdentState::Identified, 1) == 0, "identified: nothing to retry");

    // Due
    TEST_ASSERT(BusIdentSchedule::isDue(BusIdentState::Pending, 1000, 1000, false), "pending due at its time");
    TEST_ASSERT(!BusIdentSchedule::isDue(BusIdentState::Pending, 1001, 1000, false), "pending not due before");
    TEST_ASSERT(!BusIdentSchedule::isDue(BusIdentState::Identified, 0, 1000, true), "identified never due");
    TEST_ASSERT(!BusIdentSchedule::isDue(BusIdentState::NoMatch, 5000, 1000, false), "noMatch waits");
    TEST_ASSERT(BusIdentSchedule::isDue(BusIdentState::NoMatch, 5000, 1000, true), "noMatch due when catalogue changes");
    TEST_ASSERT(!BusIdentSchedule::isDue(BusIdentState::Deferred, 5000, 1000, true), "deferred keeps its interval");
    TEST_ASSERT(BusIdentSchedule::isDue(BusIdentState::BusError, 0xfffffffau, 50, false), "due across the time wrap");
    TEST_ASSERT(!BusIdentSchedule::isDue(BusIdentState::BusError, 50, 0xfffffffau, false), "not due across the time wrap");
}

static void testDeriveEvent()
{
    printf("Running testDeriveEvent...\n");
    const DeviceTypeIndexType NONE = DEVICE_TYPE_INDEX_INVALID;
    TEST_ASSERT(BusIdentSchedule::deriveEvent(NONE, 5) == BusIdentEvent::Identified, "identified");
    TEST_ASSERT(BusIdentSchedule::deriveEvent(5, 5) == BusIdentEvent::Identified, "re-identified as the same type");
    TEST_ASSERT(BusIdentSchedule::deriveEvent(5, 7) == BusIdentEvent::TypeChanged, "type changed");
    TEST_ASSERT(BusIdentSchedule::deriveEvent(5, NONE) == BusIdentEvent::Lost, "lost");
    TEST_ASSERT(BusIdentSchedule::deriveEvent(NONE, NONE) == BusIdentEvent::None, "nothing");
}

static void testProducerDefault()
{
    printf("Running testProducerDefault...\n");
    BusAddrStatus fromBle(0x10, DeviceOnlineState::ONLINE, true, true, 4);
    TEST_ASSERT(fromBle.identEvent == BusIdentEvent::Identified, "newly identified defaults to Identified");
    TEST_ASSERT(fromBle.prevDeviceTypeIndex == DEVICE_TYPE_INDEX_INVALID, "no previous type");
    BusAddrStatus plain(0x10, DeviceOnlineState::ONLINE, true, false, 4);
    TEST_ASSERT(plain.identEvent == BusIdentEvent::None, "not newly identified: None");
    BusAddrStatus noType(0x10, DeviceOnlineState::ONLINE, true, true);
    TEST_ASSERT(noType.identEvent == BusIdentEvent::None, "newly identified without a type: None");
}

// Identify a record as a type (as BusStatusMgr::setIdentOutcome does) or record a failed attempt
static void identifyAs(BusAddrRecord& rec, DeviceTypeIndexType idx)
{
    rec.deviceStatus.clear();
    rec.deviceStatus.deviceTypeIndex = idx;
    rec.isNewlyIdentified = idx != DEVICE_TYPE_INDEX_INVALID;
    rec.identChangePending = true;
}

static void testRecordEvents()
{
    printf("Running testRecordEvents...\n");
    BusAddrRecord rec(0x48, DeviceOnlineState::ONLINE, true, false);

    // Online, not yet identified: an online change with no identity event
    BusAddrStatus st = rec.takeStatusChange();
    TEST_ASSERT(st.isChange && !st.isNewlyIdentified && (st.identEvent == BusIdentEvent::None), "online");
    TEST_ASSERT(!rec.hasStatusChange(), "nothing more to report");

    // An attempt that finds nothing reports nothing
    identifyAs(rec, DEVICE_TYPE_INDEX_INVALID);
    st = rec.takeStatusChange();
    TEST_ASSERT(!st.isChange && !st.isNewlyIdentified && (st.identEvent == BusIdentEvent::None), "no match: nothing");

    // Identified
    identifyAs(rec, 5);
    st = rec.takeStatusChange();
    TEST_ASSERT(st.isNewlyIdentified && (st.identEvent == BusIdentEvent::Identified) && (st.deviceTypeIndex == 5) &&
                (st.prevDeviceTypeIndex == DEVICE_TYPE_INDEX_INVALID), "identified");

    // Re-verified (re-initialised): Identified with the same type, not newly identified
    rec.identChangePending = true;
    st = rec.takeStatusChange();
    TEST_ASSERT(!st.isNewlyIdentified && (st.identEvent == BusIdentEvent::Identified) && (st.prevDeviceTypeIndex == 5),
                "re-verified");

    // Re-identified as another type
    identifyAs(rec, 7);
    st = rec.takeStatusChange();
    TEST_ASSERT(st.isNewlyIdentified && (st.identEvent == BusIdentEvent::TypeChanged) && (st.deviceTypeIndex == 7) &&
                (st.prevDeviceTypeIndex == 5), "type changed");

    // Cleared and found nothing: Lost
    identifyAs(rec, DEVICE_TYPE_INDEX_INVALID);
    st = rec.takeStatusChange();
    TEST_ASSERT(!st.isNewlyIdentified && (st.identEvent == BusIdentEvent::Lost) && (st.prevDeviceTypeIndex == 7), "lost");

    // Found again as the type it had before it was lost: Identified (not TypeChanged - Lost was reported)
    identifyAs(rec, 5);
    st = rec.takeStatusChange();
    TEST_ASSERT((st.identEvent == BusIdentEvent::Identified) && (st.prevDeviceTypeIndex == DEVICE_TYPE_INDEX_INVALID),
                "identified after lost");

    // Goes offline: Lost, with the type it had
    rec.onlineState = DeviceOnlineState::PENDING_DELETION;
    rec.isChange = true;
    st = rec.takeStatusChange();
    TEST_ASSERT(st.isChange && (st.identEvent == BusIdentEvent::Lost) && (st.prevDeviceTypeIndex == 5) &&
                (st.onlineState == DeviceOnlineState::PENDING_DELETION), "offline: lost");

    // An unidentified device going offline: no identity event
    BusAddrRecord rec2(0x49, DeviceOnlineState::ONLINE, true, false);
    rec2.takeStatusChange();
    rec2.onlineState = DeviceOnlineState::PENDING_DELETION;
    rec2.isChange = true;
    st = rec2.takeStatusChange();
    TEST_ASSERT(st.isChange && (st.identEvent == BusIdentEvent::None), "unidentified offline: no identity event");
}

static void testReIdentScope()
{
    printf("Running testReIdentScope...\n");
    const DeviceTypeIndexType NONE = DEVICE_TYPE_INDEX_INVALID;
    std::vector<DeviceTypeIndexType> candidates = {9, 4, 7};   // tried in this order at the address
    BusReIdentRequest req;
    req.scope = BusReIdentScope::All;
    TEST_ASSERT(BusIdentSchedule::reIdentScopeIncludes(req, 0x29, 4, candidates), "all: identified");
    TEST_ASSERT(BusIdentSchedule::reIdentScopeIncludes(req, 0x29, NONE, candidates), "all: unidentified");
    req.scope = BusReIdentScope::Unidentified;
    TEST_ASSERT(!BusIdentSchedule::reIdentScopeIncludes(req, 0x29, 4, candidates), "unidentified: not identified ones");
    TEST_ASSERT(BusIdentSchedule::reIdentScopeIncludes(req, 0x29, NONE, candidates), "unidentified");
    req.scope = BusReIdentScope::Address;
    req.address = 0x129;
    TEST_ASSERT(BusIdentSchedule::reIdentScopeIncludes(req, 0x129, 4, candidates), "address");
    TEST_ASSERT(!BusIdentSchedule::reIdentScopeIncludes(req, 0x29, 4, candidates), "address: other slot");
    req.scope = BusReIdentScope::TypeIndex;
    req.deviceTypeIndex = 4;
    TEST_ASSERT(BusIdentSchedule::reIdentScopeIncludes(req, 0x29, 4, candidates), "type: identified as it");
    TEST_ASSERT(!BusIdentSchedule::reIdentScopeIncludes(req, 0x29, 7, candidates), "type: other type");
    TEST_ASSERT(!BusIdentSchedule::reIdentScopeIncludes(req, 0x29, NONE, candidates), "type: unidentified");
    req.scope = BusReIdentScope::Outranked;
    req.deviceTypeIndex = 4;
    TEST_ASSERT(BusIdentSchedule::reIdentScopeIncludes(req, 0x29, 7, candidates), "outranked: tried before its type");
    TEST_ASSERT(!BusIdentSchedule::reIdentScopeIncludes(req, 0x29, 9, candidates), "outranked: not before a stronger one");
    TEST_ASSERT(!BusIdentSchedule::reIdentScopeIncludes(req, 0x29, 4, candidates), "outranked: not itself");
    TEST_ASSERT(BusIdentSchedule::reIdentScopeIncludes(req, 0x29, 12, candidates), "outranked: own type no longer a candidate");
    req.deviceTypeIndex = 20;
    TEST_ASSERT(!BusIdentSchedule::reIdentScopeIncludes(req, 0x29, 12, candidates), "outranked: type not a candidate here");
}

int main()
{
    testRetryPolicy();
    testDeriveEvent();
    testProducerDefault();
    testRecordEvents();
    testReIdentScope();
    return testSummary("test_bus_ident");
}
