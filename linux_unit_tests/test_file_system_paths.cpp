/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// FileSystem path tests
//
// A filename names a file system only at its start ("local/...", "/sd/..."): it then picks the path and the lock.
// "logs/sd/x.csv" is a file in a folder called sd on whichever file system was given.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include "testhdr.h"
#include "FileSystem.h"

static bool namedIs(const char* filename, const char* expected)
{
    const char* pNamed = FileSystem::fileSystemNamedIn(filename);
    if (!expected)
        return pNamed == nullptr;
    return pNamed && (strcmp(pNamed, expected) == 0);
}

static void testNamedAtStart()
{
    printf("Running testNamedAtStart...\n");
    TEST_ASSERT(namedIs("local/a.txt", "local"), "local/a.txt");
    TEST_ASSERT(namedIs("/local/web/index.html", "local"), "/local/...");
    TEST_ASSERT(namedIs("sd/a.txt", "sd"), "sd/a.txt");
    TEST_ASSERT(namedIs("/sd/images/x.jpg", "sd"), "/sd/...");
    TEST_ASSERT(namedIs("/local", "local") && namedIs("sd", "sd"), "the name alone");
}

static void testNotNamed()
{
    printf("Running testNotNamed...\n");
    TEST_ASSERT(namedIs("logs/sd/x.csv", nullptr), "sd/ in the middle");
    TEST_ASSERT(namedIs("/data/local/x", nullptr), "local/ in the middle");
    TEST_ASSERT(namedIs("backup/nonlocal/x", nullptr), "nonlocal/");
    TEST_ASSERT(namedIs("sdcard/x", nullptr) && namedIs("/localdata/x", nullptr), "names starting with a file system name");
    TEST_ASSERT(namedIs("a.txt", nullptr) && namedIs("/a.txt", nullptr) && namedIs("", nullptr), "no file system");
}

int main()
{
    testNamedAtStart();
    testNotNamed();
    return testSummary("test_file_system_paths");
}
