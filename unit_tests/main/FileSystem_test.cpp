/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Unit test of FileSystem
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "unity.h"
#include "Logger.h"
#include "RaftArduino.h"
#include "RaftUtils.h"
#include "FileSystem.h"

static const char* MODULE_PREFIX = "FileSystemTest";

TEST_CASE("file_system_test", "[filesystem]")
{
    fileSystem.setup(FileSystem::LOCAL_FS_LITTLEFS, true, false, -1, -1, -1, -1, false, false);

    TEST_ASSERT_EQUAL_STRING("local", fileSystem.getDefaultFSRoot().c_str());

    String respStr;
    TEST_ASSERT_TRUE(fileSystem.getFilesJSON("", "", "/", respStr));

    const char* expectedFilesJson = "{\"req\":\"\",\"rslt\":\"ok\",\"fsName\":\"local\",\"fsBase\":\"/local\",\"diskSize\":524288,\"diskUsed\":8192,\"folder\":\"/local/\",\"files\":[]}";
    // LOG_I(MODULE_PREFIX, "file_system_test getFilesJson %s", respStr.c_str());
    TEST_ASSERT_EQUAL_STRING(expectedFilesJson, respStr.c_str());

    const char* testFileName = "testFile.txt";
    String testFileContents = "This is a test file\nAnd this is the second line\nand a third line\n";

    // Set file contents
    TEST_ASSERT_TRUE(fileSystem.setFileContents("local", testFileName, testFileContents));

    // Read file contents
    uint8_t* pFileContents = fileSystem.getFileContents("local", testFileName, 0);
    TEST_ASSERT_NOT_NULL(pFileContents);
    TEST_ASSERT_EQUAL_STRING(testFileContents.c_str(), (const char*)pFileContents);
    free(pFileContents);


    
}
