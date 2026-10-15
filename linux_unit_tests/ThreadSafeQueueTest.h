#pragma once

#include <stdio.h>
#include <string>
#include <thread>
#include <atomic>
#include "ThreadSafeQueue.h"
#include "SpiramAwareAllocator.h"

/// @brief Tests of ThreadSafeQueue - order, limits, peek/pop/clear, many elements and two threads
class ThreadSafeQueueTest
{
public:
    // Element similar to a comms message (owns a buffer)
    struct TestMsg
    {
        uint32_t seq = 0;
        SpiramAwareUint8Vector data;
    };

    int runTests()
    {
        int failCount = 0;
        failCount += testOrderAndLimits();
        failCount += testPeekPopClear();
        failCount += testManyElements();
        failCount += testTwoThreads();
        if (failCount == 0)
            printf("ThreadSafeQueueTest all tests passed\n");
        else
            printf("ThreadSafeQueueTest FAILED %d tests\n", failCount);
        return failCount;
    }

private:
    static int check(bool cond, const char* pMsg)
    {
        if (!cond)
            printf("ThreadSafeQueueTest failed %s\n", pMsg);
        return cond ? 0 : 1;
    }

    static TestMsg makeMsg(uint32_t seq)
    {
        TestMsg msg;
        msg.seq = seq;
        msg.data.assign(seq % 64 + 1, (uint8_t)seq);
        return msg;
    }

    static bool msgValid(const TestMsg& msg, uint32_t seq)
    {
        return (msg.seq == seq) && (msg.data.size() == seq % 64 + 1) && (msg.data[0] == (uint8_t)seq);
    }

    int testOrderAndLimits()
    {
        int fails = 0;
        ThreadSafeQueue<TestMsg> queue(3);
        fails += check(queue.count() == 0 && queue.canAcceptData(), "emptyQueue");
        fails += check(queue.put(makeMsg(1)) && queue.put(makeMsg(2)) && queue.put(makeMsg(3)), "putToMax");
        fails += check(!queue.put(makeMsg(4)), "putOverMaxRejected");
        fails += check(queue.count() == 3 && !queue.canAcceptData(), "fullQueue");
        TestMsg msg;
        for (uint32_t i = 1; i <= 3; i++)
            fails += check(queue.get(msg) && msgValid(msg, i), "getInOrder");
        fails += check(!queue.get(msg), "getEmpty");

        // Raising the limit
        queue.setMaxLen(5);
        for (uint32_t i = 10; i < 15; i++)
            fails += check(queue.put(makeMsg(i)), "putAfterSetMaxLen");
        fails += check(queue.count() == 5 && queue.maxLen() == 5, "countAfterSetMaxLen");

        // String elements
        ThreadSafeQueue<std::string> strQueue(10);
        fails += check(strQueue.put("first") && strQueue.put(std::string(200, 'x')), "putStrings");
        std::string str;
        fails += check(strQueue.get(str) && str == "first", "getString1");
        fails += check(strQueue.get(str) && str == std::string(200, 'x'), "getString2");
        return fails;
    }

    int testPeekPopClear()
    {
        int fails = 0;
        ThreadSafeQueue<TestMsg> queue(10);
        TestMsg msg;
        fails += check(!queue.peek(msg) && !queue.pop(), "peekPopEmpty");
        fails += check(queue.put(makeMsg(7)) && queue.put(makeMsg(8)), "putTwo");
        fails += check(queue.peek(msg) && msgValid(msg, 7), "peekFront");
        fails += check(queue.count() == 2, "peekDoesNotRemove");
        fails += check(queue.pop() && queue.count() == 1, "popFront");
        fails += check(queue.get(msg) && msgValid(msg, 8), "getAfterPop");
        for (uint32_t i = 0; i < 5; i++)
            fails += check(queue.put(makeMsg(i)), "putForClear");
        fails += check(queue.clear() && queue.count() == 0 && !queue.get(msg), "clear");
        fails += check(queue.put(makeMsg(99)) && queue.get(msg) && msgValid(msg, 99), "usableAfterClear");
        return fails;
    }

    int testManyElements()
    {
        // Enough elements to span many deque blocks, filled and drained twice
        int fails = 0;
        const uint32_t NUM = 3000;
        ThreadSafeQueue<TestMsg> queue(NUM);
        for (int pass = 0; pass < 2; pass++)
        {
            for (uint32_t i = 0; i < NUM; i++)
            {
                if (!queue.put(makeMsg(i)))
                {
                    fails += check(false, "manyPut");
                    break;
                }
            }
            fails += check(queue.count() == NUM, "manyCount");
            TestMsg msg;
            for (uint32_t i = 0; i < NUM; i++)
            {
                if (!queue.get(msg) || !msgValid(msg, i))
                {
                    fails += check(false, "manyGetInOrder");
                    break;
                }
            }
            fails += check(queue.count() == 0, "manyDrained");
        }
        return fails;
    }

    int testTwoThreads()
    {
        // Producer and consumer on different threads - every element arrives once, in order
        const uint32_t NUM = 20000;
        ThreadSafeQueue<TestMsg> queue(50);
        std::atomic<bool> orderOk{true};
        std::atomic<uint32_t> received{0};
        std::thread consumer([&]()
        {
            TestMsg msg;
            uint32_t expected = 0;
            while (expected < NUM)
            {
                if (queue.get(msg, 100))
                {
                    if (!msgValid(msg, expected))
                        orderOk = false;
                    expected++;
                }
            }
            received = expected;
        });
        for (uint32_t i = 0; i < NUM; i++)
        {
            while (!queue.put(makeMsg(i), 100))
                std::this_thread::yield();
        }
        consumer.join();
        int fails = 0;
        fails += check(received == NUM, "twoThreadsAllReceived");
        fails += check(orderOk, "twoThreadsOrder");
        fails += check(queue.count() == 0, "twoThreadsDrained");
        return fails;
    }
};
