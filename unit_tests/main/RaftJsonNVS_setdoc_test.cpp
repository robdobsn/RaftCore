/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Unit tests of RaftJsonNVS setJsonDoc - stored before being applied, unchanged documents not rewritten
//
// Rob Dobson 2017-2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "unity.h"
#include "Logger.h"
#include "RaftJsonNVS.h"
#include "RaftArduino.h"

// Namespace used for this test (left as an empty document at the end)
static const char* TEST_NVS_NAMESPACE = "RJNVSSetTest";

TEST_CASE("test_raftjsonnvs_setjsondoc", "[raftjsonnvs]")
{
    // Start from a known document
    RaftJsonNVS nvsJson(TEST_NVS_NAMESPACE);
    TEST_ASSERT_TRUE_MESSAGE(nvsJson.setJsonDoc(R"({"a":1})"), "setInitial");
    int numCallbacks = 0;
    nvsJson.registerChangeCallback([&numCallbacks]() { numCallbacks++; });

    // A changed document is stored and applied and the callbacks are called
    TEST_ASSERT_TRUE_MESSAGE(nvsJson.setJsonDoc(R"({"a":2})"), "setChanged");
    TEST_ASSERT_EQUAL_MESSAGE(2, nvsJson.getLong("a", -1), "setChangedApplied");
    TEST_ASSERT_EQUAL_MESSAGE(1, numCallbacks, "setChangedCallback");
    {
        RaftJsonNVS reread(TEST_NVS_NAMESPACE);
        TEST_ASSERT_EQUAL_MESSAGE(2, reread.getLong("a", -1), "setChangedStored");
    }

    // An unchanged document succeeds without calling the callbacks
    TEST_ASSERT_TRUE_MESSAGE(nvsJson.setJsonDoc(R"({"a":2})"), "setUnchanged");
    TEST_ASSERT_EQUAL_MESSAGE(1, numCallbacks, "setUnchangedNoCallback");

    // A document too long for an NVS string is rejected and nothing changes
    String longDoc = R"({"s":")";
    for (int i = 0; i < 4000; i++)
        longDoc += 'x';
    longDoc += R"("})";
    TEST_ASSERT_FALSE_MESSAGE(nvsJson.setJsonDoc(longDoc.c_str()), "setTooLongForNVS");
    TEST_ASSERT_EQUAL_MESSAGE(2, nvsJson.getLong("a", -1), "setTooLongUnchanged");
    TEST_ASSERT_EQUAL_MESSAGE(1, numCallbacks, "setTooLongNoCallback");
    {
        RaftJsonNVS reread(TEST_NVS_NAMESPACE);
        TEST_ASSERT_EQUAL_MESSAGE(2, reread.getLong("a", -1), "setTooLongNotStored");
    }

    // A document longer than the object's maximum is rejected
    {
        RaftJsonNVS limited(TEST_NVS_NAMESPACE, 10);
        TEST_ASSERT_FALSE_MESSAGE(limited.setJsonDoc(R"({"a":12345678})"), "setOverMaxLen");
        TEST_ASSERT_EQUAL_MESSAGE(2, limited.getLong("a", -1), "setOverMaxLenUnchanged");
    }

    // Null is rejected
    TEST_ASSERT_FALSE_MESSAGE(nvsJson.setJsonDoc(nullptr), "setNull");

    // If storing fails (here because the namespace name is too long for NVS) the document in RAM is
    // unchanged
    {
        RaftJsonNVS badNamespace("NamespaceTooLongForNVS");
        TEST_ASSERT_FALSE_MESSAGE(badNamespace.setJsonDoc(R"({"b":3})"), "setStoreFails");
        TEST_ASSERT_EQUAL_MESSAGE(-1, badNamespace.getLong("b", -1), "setStoreFailsUnchanged");
    }

    // Clean up
    TEST_ASSERT_TRUE_MESSAGE(nvsJson.setJsonDoc("{}"), "setCleanUp");
}
