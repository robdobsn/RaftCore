/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Minimal test harness for the Linux unit tests
//
// Each test program includes this header, checks conditions with TEST_ASSERT and returns
// testSummary("<program name>") from main(). That prints the result and gives a non-zero exit code
// if anything failed, so "make check" (and CI) fails too.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdio.h>

inline int testFailCount = 0;
inline int testCheckCount = 0;

// Check a condition, naming the check so a failure can be found in the source
#define TEST_ASSERT(cond, name) \
    do { \
        testCheckCount++; \
        if (!(cond)) \
        { \
            printf("FAIL %s (%s:%d)\n", (const char*)(name), __FILE__, __LINE__); \
            testFailCount++; \
        } \
    } while (0)

// Record a failure with a message (for checks that build their own description)
inline void testFail(const char* context, const char* msg)
{
    printf("FAIL %s: %s\n", context, msg);
    testFailCount++;
}

// Print the result and return the process exit code
inline int testSummary(const char* programName)
{
    if (testFailCount > 0)
    {
        printf("%s FAILED %d of %d checks\n", programName, testFailCount, testCheckCount);
        return 1;
    }
    printf("%s all %d checks passed\n", programName, testCheckCount);
    return 0;
}
