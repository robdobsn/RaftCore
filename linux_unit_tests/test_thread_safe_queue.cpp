/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ThreadSafeQueue tests
//
// Order, limits, peek/pop/clear, moving elements in and out (and peekWith) without copying their buffers, many
// elements (spanning many deque blocks) and a producer and consumer on two threads. The queue's deque uses SpiramAwareAllocator, which falls back to the standard heap on Linux.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <string>
#include <thread>
#include <atomic>
#include "testhdr.h"
#include "ThreadSafeQueue.h"
#include "SpiramAwareAllocator.h"

// Element similar to a comms message (owns a buffer)
struct TestMsg
{
    uint32_t seq = 0;
    SpiramAwareUint8Vector data;
};

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

static void testOrderAndLimits()
{
    printf("Running testOrderAndLimits...\n");
    ThreadSafeQueue<TestMsg> queue(3);
    TEST_ASSERT(queue.count() == 0 && queue.canAcceptData(), "emptyQueue");
    TEST_ASSERT(queue.put(makeMsg(1)) && queue.put(makeMsg(2)) && queue.put(makeMsg(3)), "putToMax");
    TEST_ASSERT(!queue.put(makeMsg(4)), "putOverMaxRejected");
    TEST_ASSERT(queue.count() == 3 && !queue.canAcceptData(), "fullQueue");
    TestMsg msg;
    for (uint32_t i = 1; i <= 3; i++)
        TEST_ASSERT(queue.get(msg) && msgValid(msg, i), "getInOrder");
    TEST_ASSERT(!queue.get(msg), "getEmpty");

    // Raising the limit
    queue.setMaxLen(5);
    for (uint32_t i = 10; i < 15; i++)
        TEST_ASSERT(queue.put(makeMsg(i)), "putAfterSetMaxLen");
    TEST_ASSERT(queue.count() == 5 && queue.maxLen() == 5, "countAfterSetMaxLen");

    // String elements
    ThreadSafeQueue<std::string> strQueue(10);
    TEST_ASSERT(strQueue.put("first") && strQueue.put(std::string(200, 'x')), "putStrings");
    std::string str;
    TEST_ASSERT(strQueue.get(str) && str == "first", "getString1");
    TEST_ASSERT(strQueue.get(str) && str == std::string(200, 'x'), "getString2");
}

static void testPeekPopClear()
{
    printf("Running testPeekPopClear...\n");
    ThreadSafeQueue<TestMsg> queue(10);
    TestMsg msg;
    TEST_ASSERT(!queue.peek(msg) && !queue.pop(), "peekPopEmpty");
    TEST_ASSERT(queue.put(makeMsg(7)) && queue.put(makeMsg(8)), "putTwo");
    TEST_ASSERT(queue.peek(msg) && msgValid(msg, 7), "peekFront");
    TEST_ASSERT(queue.count() == 2, "peekDoesNotRemove");
    TEST_ASSERT(queue.pop() && queue.count() == 1, "popFront");
    TEST_ASSERT(queue.get(msg) && msgValid(msg, 8), "getAfterPop");
    for (uint32_t i = 0; i < 5; i++)
        TEST_ASSERT(queue.put(makeMsg(i)), "putForClear");
    TEST_ASSERT(queue.clear() && queue.count() == 0 && !queue.get(msg), "clear");
    TEST_ASSERT(queue.put(makeMsg(99)) && queue.get(msg) && msgValid(msg, 99), "usableAfterClear");
}

static void testMoves()
{
    printf("Running testMoves...\n");
    ThreadSafeQueue<TestMsg> queue(2);

    // Moved in: the buffer is the one put (not a copy), and moved out again on get
    TestMsg msg = makeMsg(40);
    const uint8_t* pData = msg.data.data();
    TEST_ASSERT(queue.put(std::move(msg)), "putMoved");
    TEST_ASSERT(msg.data.empty(), "movedFrom");
    TestMsg got;
    TEST_ASSERT(queue.get(got) && msgValid(got, 40) && (got.data.data() == pData), "getSameBuffer");

    // A put that fails leaves the element as it was
    TEST_ASSERT(queue.put(makeMsg(1)) && queue.put(makeMsg(2)), "fill");
    TestMsg rejected = makeMsg(3);
    TEST_ASSERT(!queue.put(std::move(rejected)) && msgValid(rejected, 3), "failedPutKeepsElement");

    // peekWith sees the front element in place
    uint32_t seenSeq = 0;
    const uint8_t* pSeen = nullptr;
    TEST_ASSERT(queue.peekWith([&](const TestMsg& m) { seenSeq = m.seq; pSeen = m.data.data(); }), "peekWith");
    TEST_ASSERT(seenSeq == 1 && queue.count() == 2, "peekWithFrontNotRemoved");
    TEST_ASSERT(queue.get(got) && msgValid(got, 1) && got.data.data() == pSeen, "peekWithSawStoredBuffer");
    TEST_ASSERT(queue.get(got) && !queue.peekWith([](const TestMsg&) {}), "peekWithEmpty");
}

static void testManyElements()
{
    printf("Running testManyElements...\n");
    // Enough elements to span many deque blocks, filled and drained twice
    const uint32_t NUM = 3000;
    ThreadSafeQueue<TestMsg> queue(NUM);
    for (int pass = 0; pass < 2; pass++)
    {
        for (uint32_t i = 0; i < NUM; i++)
        {
            if (!queue.put(makeMsg(i)))
            {
                TEST_ASSERT(false, "manyPut");
                break;
            }
        }
        TEST_ASSERT(queue.count() == NUM, "manyCount");
        TestMsg msg;
        for (uint32_t i = 0; i < NUM; i++)
        {
            if (!queue.get(msg) || !msgValid(msg, i))
            {
                TEST_ASSERT(false, "manyGetInOrder");
                break;
            }
        }
        TEST_ASSERT(queue.count() == 0, "manyDrained");
    }
}

static void testTwoThreads()
{
    printf("Running testTwoThreads...\n");
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
    TEST_ASSERT(received == NUM, "twoThreadsAllReceived");
    TEST_ASSERT(orderOk, "twoThreadsOrder");
    TEST_ASSERT(queue.count() == 0, "twoThreadsDrained");
}

int main()
{
    testOrderAndLimits();
    testPeekPopClear();
    testMoves();
    testManyElements();
    testTwoThreads();
    return testSummary("test_thread_safe_queue");
}
