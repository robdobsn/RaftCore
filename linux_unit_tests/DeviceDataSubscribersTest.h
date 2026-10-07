#pragma once

#include <stdio.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include "DeviceDataSubscribers.h"

/// @brief Tests for DeviceDataSubscribers: several subscribers per device, by
/// ID and by type, per-device rate limits, and unsubscribe while a callback is
/// running on another thread (which must wait) or on this one (which must not)
class DeviceDataSubscribersTest
{
public:
    int runTests()
    {
        printf("Running DeviceDataSubscribersTest...\n");
        testTwoSubscribersSameDevice();
        testTypeAndIdSubscribers();
        testRateLimitPerDevice();
        testRemoveMatchesInfo();
        testRemoveWaitsForCallbackOnOtherThread();
        testSelfRemoveInsideCallback();
        testDispatchOverflow();
        if (_failCount == 0)
            printf("DeviceDataSubscribersTest all tests passed\n");
        else
            printf("DeviceDataSubscribersTest FAILED %d tests\n", _failCount);
        return _failCount;
    }

private:
    int _failCount = 0;
    void check(bool cond, const char* msg)
    {
        if (!cond)
        {
            printf("TEST_ASSERT failed %s\n", msg);
            _failCount++;
        }
    }
    static std::vector<uint8_t> sample(uint8_t v) { return std::vector<uint8_t>{v, (uint8_t)(v + 1), (uint8_t)(v + 2)}; }

    // The bug this replaces: a second subscriber silently displaced the first
    void testTwoSubscribersSameDevice()
    {
        DeviceDataSubscribers subs;
        RaftDeviceID dev(1, 0x29);
        int a = 0, b = 0;
        std::vector<uint8_t> seenA, seenB;
        subs.addForDevice(dev, [&](uint16_t, std::vector<uint8_t> d, const void*) { a++; seenA = d; }, 0, (const void*)1);
        subs.addForDevice(dev, [&](uint16_t, std::vector<uint8_t> d, const void*) { b++; seenB = d; }, 0, (const void*)2);
        const uint32_t n = subs.dispatch(dev, 6, sample(10), 1000);
        check(n == 2 && a == 1 && b == 1, "testTwoSubscribersSameDevice both called");
        check(seenA == sample(10) && seenB == sample(10), "testTwoSubscribersSameDevice both see the whole sample");
        check(subs.dispatch(RaftDeviceID(1, 0x30), 6, sample(1), 1000) == 0 && a == 1, "testTwoSubscribersSameDevice other device not delivered");
    }

    void testTypeAndIdSubscribers()
    {
        DeviceDataSubscribers subs;
        int byType = 0, byId = 0;
        subs.addForType(6, [&](uint16_t, std::vector<uint8_t>, const void*) { byType++; }, 0, (const void*)1);
        subs.addForDevice(RaftDeviceID(1, 0x29), [&](uint16_t, std::vector<uint8_t>, const void*) { byId++; }, 0, (const void*)2);
        subs.dispatch(RaftDeviceID(1, 0x29), 6, sample(1), 0);
        subs.dispatch(RaftDeviceID(2, 0x29), 6, sample(1), 0);
        check(byType == 2 && byId == 1, "testTypeAndIdSubscribers type covers both devices, ID only its own");
        subs.dispatch(RaftDeviceID(1, 0x29), DEVICE_TYPE_INDEX_INVALID, sample(1), 0);
        check(byType == 2 && byId == 2, "testTypeAndIdSubscribers unidentified device reaches ID subscribers only");
        check(subs.hasSubscriber(RaftDeviceID(3, 1), 6) && !subs.hasSubscriber(RaftDeviceID(3, 1), 7),
              "testTypeAndIdSubscribers hasSubscriber");
    }

    void testRateLimitPerDevice()
    {
        DeviceDataSubscribers subs;
        int fast = 0, slow = 0;
        subs.addForType(6, [&](uint16_t, std::vector<uint8_t>, const void*) { fast++; }, 0, (const void*)1);
        subs.addForType(6, [&](uint16_t, std::vector<uint8_t>, const void*) { slow++; }, 100, (const void*)2);
        RaftDeviceID d1(1, 1), d2(1, 2);
        subs.dispatch(d1, 6, sample(1), 1000);
        subs.dispatch(d2, 6, sample(1), 1010);     // other device: its own limit
        subs.dispatch(d1, 6, sample(1), 1050);     // too soon for the slow one
        subs.dispatch(d1, 6, sample(1), 1100);     // 100 ms after the first
        check(fast == 4, "testRateLimitPerDevice unlimited subscriber gets every sample");
        check(slow == 3, "testRateLimitPerDevice limited subscriber limited per device");
    }

    void testRemoveMatchesInfo()
    {
        DeviceDataSubscribers subs;
        RaftDeviceID dev(1, 0x29);
        int a = 0, b = 0;
        subs.addForDevice(dev, [&](uint16_t, std::vector<uint8_t>, const void*) { a++; }, 0, (const void*)1);
        subs.addForDevice(dev, [&](uint16_t, std::vector<uint8_t>, const void*) { b++; }, 0, (const void*)2);
        subs.addForType(6, [&](uint16_t, std::vector<uint8_t>, const void*) { b += 100; }, 0, (const void*)2);
        check(subs.removeForDevice(dev, (const void*)1) == 1, "testRemoveMatchesInfo removes one");
        check(subs.removeForDevice(dev, (const void*)1) == 0, "testRemoveMatchesInfo second remove finds nothing");
        subs.dispatch(dev, 6, sample(1), 0);
        check(a == 0 && b == 101, "testRemoveMatchesInfo the others still delivered");
        check(subs.removeForType(6, (const void*)2) == 1 && subs.count() == 1, "testRemoveMatchesInfo type removal leaves the ID one");
    }

    // A subscriber must be safe to destroy once remove() returns
    void testRemoveWaitsForCallbackOnOtherThread()
    {
        DeviceDataSubscribers subs;
        RaftDeviceID dev(1, 0x29);
        std::atomic<bool> entered{false}, finished{false};
        subs.addForDevice(dev, [&](uint16_t, std::vector<uint8_t>, const void*) {
            entered = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            finished = true;
        }, 0, (const void*)1);
        std::thread busTask([&]() { subs.dispatch(dev, 6, sample(1), 0); });
        while (!entered)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const auto t0 = std::chrono::steady_clock::now();
        const uint32_t removed = subs.removeForDevice(dev, (const void*)1);
        const bool finishedAtReturn = finished;
        const auto waitedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        busTask.join();
        check(removed == 1 && finishedAtReturn, "testRemoveWaitsForCallbackOnOtherThread remove returns after the callback");
        check(waitedMs >= 50, "testRemoveWaitsForCallbackOnOtherThread remove actually waited");
        check(subs.count() == 0, "testRemoveWaitsForCallbackOnOtherThread entry erased");
    }

    // Removing from inside a callback must not wait on itself
    void testSelfRemoveInsideCallback()
    {
        DeviceDataSubscribers subs;
        RaftDeviceID dev(1, 0x29);
        int calls = 0;
        subs.addForDevice(dev, [&](uint16_t, std::vector<uint8_t>, const void*) {
            calls++;
            subs.removeForDevice(dev, (const void*)1);
        }, 0, (const void*)1);
        const auto t0 = std::chrono::steady_clock::now();
        subs.dispatch(dev, 6, sample(1), 0);
        const auto tookMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        subs.dispatch(dev, 6, sample(1), 0);
        check(calls == 1 && tookMs < 100, "testSelfRemoveInsideCallback no self-wait, not called again");
        check(subs.count() == 0, "testSelfRemoveInsideCallback entry erased after dispatch");
    }

    void testDispatchOverflow()
    {
        DeviceDataSubscribers subs;
        RaftDeviceID dev(1, 0x29);
        int calls = 0;
        for (int i = 0; i < 9; i++)
            subs.addForDevice(dev, [&](uint16_t, std::vector<uint8_t>, const void*) { calls++; }, 0, (const void*)(uintptr_t)(i + 1));
        check(subs.dispatch(dev, 6, sample(1), 0) == 8 && calls == 8 && subs.dispatchOverflows() == 1,
              "testDispatchOverflow capped at MAX_PER_DISPATCH and counted");
    }
};
