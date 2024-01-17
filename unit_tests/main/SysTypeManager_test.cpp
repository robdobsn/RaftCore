/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Unit test of SysTypeManager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "unity.h"
#include "Logger.h"
#include "RaftArduino.h"
#include "RaftUtils.h"
#include "SysTypeManager.h"

static const char* MODULE_PREFIX = "SysTypeManagerTest";

TEST_CASE("systype_manager_test", "[systypemanager]")
{
    static const std::vector<SysTypeInfoRec> sysTypeInfoRecs = {
        {
            "TestSysType1",
            "0",
            "{ \"name\": \"TestSysType1\", \"forVersion\": 0, \"configHw\": {\"gpioPinOne\":1, \"gpioPinTwo\":2} }"
        },
        {
            "TestSysType1",
            "1",
            "{ \"name\": \"TestSysType1\", \"forVersion\": 1, \"configHw\": {\"gpioPinOne\":15, \"gpioPinTwo\":22} }"
        },
        {
            "TestSysType2",
            "0",
            "{ \"name\": \"TestSysType2\", \"forVersion\": 0, \"configHw\": {\"gpioPinOne\":7, \"gpioPinTwo\":5} }"
        },
        {
            "TestSysType3",
            "1",
            "{ \"name\": \"TestSysType3\", \"forVersion\": 1, \"configHw\": {\"gpioPinOne\":17, \"gpioPinTwo\":11} }"
        },
        {
            "TestSysType3",
            "5",
            "{ \"name\": \"TestSysType3\", \"forVersion\": 5, \"configHw\": {\"gpioPinOne\":72, \"gpioPinTwo\":35} }"
        }
    };
    const char* FINAL_SYS_TYPE_SET_IN_TEST = "{ \"SysType\": \"TestSysType3\" }";
    RaftJsonNVS systemConfig("SysTypeManTest");
    RaftJson sysTypeConfig;
    SysTypeManager sysTypeManager(systemConfig, sysTypeConfig);
    sysTypeManager.setBaseSysTypes(&sysTypeInfoRecs);

    // Check if this test has been run before
    String sysTypeDoc = systemConfig.getJsonDoc();
    LOG_I(MODULE_PREFIX, "checkTestRunBefore %s sysTypeDoc %s", 
                sysTypeDoc.equals(FINAL_SYS_TYPE_SET_IN_TEST) ? "TEST HAS RUN BEFORE" : "LOOKS LIKE THIS IS THE FIRST RUN FOR THIS TEST ON THIS HARDWARE SINCE FLASH ERASE", 
                sysTypeDoc.c_str());

    // Ensure we go back to a known state
    sysTypeManager.setNonVolatileDocContents("{}");

    // Test list of systypes (since we have not set a base SysType version we should get a list of all unique SysTypes)
    String sysTypesJson = sysTypeManager.getBaseSysTypesListAsJson();
    LOG_I(MODULE_PREFIX, "getFullSysTypesList for hw sysTypesJson %s", sysTypesJson.c_str());
    TEST_ASSERT_EQUAL_STRING("[\"TestSysType1\",\"TestSysType2\",\"TestSysType3\"]", sysTypesJson.c_str());

    // Test systype name (whether the test has run before or not the default SysType is TestSysType1 - the first one in the list)
    String sysTypeName = sysTypeManager.getCurrentSysTypeName();
    LOG_I(MODULE_PREFIX, "sysTypeName %s", sysTypeName.c_str());
    TEST_ASSERT_EQUAL_STRING("TestSysType1", sysTypeName.c_str());

    // Test get systype info with different base SysType version
    sysTypeManager.setBaseSysTypeVersion("1");
    sysTypesJson = sysTypeManager.getBaseSysTypesListAsJson();
    LOG_I(MODULE_PREFIX, "sysTypesJson %s", sysTypesJson.c_str());
    TEST_ASSERT_EQUAL_STRING("[\"TestSysType1\",\"TestSysType3\"]", sysTypesJson.c_str());

    // Test systype name
    sysTypeName = sysTypeManager.getCurrentSysTypeName();
    LOG_I(MODULE_PREFIX, "sysTypeName %s", sysTypeName.c_str());
    TEST_ASSERT_EQUAL_STRING("TestSysType1", sysTypeName.c_str());

    // Test get systype info with different base SysType version
    sysTypeManager.setBaseSysTypeVersion("5");
    sysTypesJson = sysTypeManager.getBaseSysTypesListAsJson();
    LOG_I(MODULE_PREFIX, "sysTypesJson %s", sysTypesJson.c_str());
    TEST_ASSERT_EQUAL_STRING("[\"TestSysType3\"]", sysTypesJson.c_str());

    // Test systype name
    sysTypeName = sysTypeManager.getCurrentSysTypeName();
    LOG_I(MODULE_PREFIX, "sysTypeName %s", sysTypeName.c_str());
    TEST_ASSERT_EQUAL_STRING("TestSysType3", sysTypeName.c_str());

    // Get content of base systype
    String sysTypeContent;
    TEST_ASSERT_TRUE_MESSAGE(sysTypeManager.getBaseSysTypeContent("TestSysType3", sysTypeContent, false), "getBaseSysTypeContent failed");
    LOG_I(MODULE_PREFIX, "sysTypeContent %s", sysTypeContent.c_str());
    TEST_ASSERT_EQUAL_STRING("{ \"name\": \"TestSysType3\", \"forVersion\": 5, \"configHw\": {\"gpioPinOne\":72, \"gpioPinTwo\":35} }", sysTypeContent.c_str());

    // Check base SysType version
    sysTypeManager.setBaseSysTypeVersion("1");
    String sysTypeConfigContent = sysTypeConfig.getJsonDoc();
    LOG_I(MODULE_PREFIX, "sysTypeContent %s", sysTypeConfigContent.c_str());
    TEST_ASSERT_EQUAL_STRING("{ \"name\": \"TestSysType1\", \"forVersion\": 1, \"configHw\": {\"gpioPinOne\":15, \"gpioPinTwo\":22} }", sysTypeConfigContent.c_str());

    // Check the current non-volatile document is correct (i.e. empty)
    String currentSysTypeDoc = systemConfig.getJsonDoc();
    LOG_I(MODULE_PREFIX, "currentSysTypeDoc %s", currentSysTypeDoc.c_str());
    TEST_ASSERT_EQUAL_STRING("{}", currentSysTypeDoc.c_str());
    
    // Check the returned values are correct for gpioPinOne and gpioPinTwo
    // const RaftJsonIF* pChained = systemConfig.getChainedRaftJson();
    // LOG_I(MODULE_PREFIX, "================ pChained %p", pChained);

    int gpioPinOne = systemConfig.getLong("configHw/gpioPinOne", -1);
    int gpioPinTwo = systemConfig.getLong("configHw/gpioPinTwo", -1);
    LOG_I(MODULE_PREFIX, "TestSysType1 version 1 ... gpioPinOne %d gpioPinTwo %d", gpioPinOne, gpioPinTwo);
    TEST_ASSERT_EQUAL_INT(15, gpioPinOne);
    TEST_ASSERT_EQUAL_INT(22, gpioPinTwo);

    // Set the systype to TestSysType3 by setting the non-volatile document contents
    sysTypeManager.setNonVolatileDocContents(FINAL_SYS_TYPE_SET_IN_TEST);

    // Check by getting values from the document
    gpioPinOne = systemConfig.getLong("configHw/gpioPinOne", -1);
    gpioPinTwo = systemConfig.getLong("configHw/gpioPinTwo", -1);
    LOG_I(MODULE_PREFIX, "TestSysType3 version 1 ... gpioPinOne %d gpioPinTwo %d", gpioPinOne, gpioPinTwo);
    TEST_ASSERT_EQUAL_INT(17, gpioPinOne);
    TEST_ASSERT_EQUAL_INT(11, gpioPinTwo);

    // Set other values into the document and check they are retrieved correctly
    sysTypeManager.setNonVolatileDocContents("{ \"configHw\": {\"gpioPinOne\":123} }");
    gpioPinOne = systemConfig.getLong("configHw/gpioPinOne", -1);
    gpioPinTwo = systemConfig.getLong("configHw/gpioPinTwo", -1);
    LOG_I(MODULE_PREFIX, "TestSysType3 version 1 ... gpioPinOne %d gpioPinTwo %d", gpioPinOne, gpioPinTwo);
    TEST_ASSERT_EQUAL_INT(123, gpioPinOne);
    TEST_ASSERT_EQUAL_INT(22, gpioPinTwo);

    // Set the systype to TestSysType3 by setting the non-volatile document contents
    sysTypeManager.setNonVolatileDocContents(FINAL_SYS_TYPE_SET_IN_TEST);
}
