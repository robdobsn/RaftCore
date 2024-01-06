/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Unit tests of RaftJson
//
// Rob Dobson 2017-2022
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "unity.h"
#include "Logger.h"
#include "RaftJsonNVS.h"
#include "RaftJsonPrefixed.h"
#include "RaftArduino.h"

static const char* MODULE_PREFIX = "RaftJsonPrefixed_unit_test";

static bool testGetString(const char* pathPrefix,
            const char* dataPath, const char* expectedStr, const char* pSourceStr)
{
    // Create ConfigBase
    RaftJson testConfigBase = pSourceStr;
    RaftJsonPrefixed testConfig(testConfigBase, pathPrefix);

    // Get string
    String rsltStr = testConfig.getString(dataPath, "<<NOT_FOUND>>");
    // LOG_I(MODULE_PREFIX, "testGetString dataPath %s expected %s", dataPath, expectedStr);
    if (!rsltStr.equals(expectedStr))
    {
        LOG_E(MODULE_PREFIX, "testGetString dataPath %s expected %s != %s", dataPath, expectedStr, rsltStr.c_str());
        return false;
    }
    return true;
}

#define SYSTEM_NAME "FirmwareESP32"
#define SYSTEM_VERSION "0.0.3"
const char* testJSONConfigBase =
    R"({)"
    R"("SystemName":")" SYSTEM_NAME R"(",)"
    R"("SystemVersion":")" SYSTEM_VERSION R"(",)"
    R"("IDFVersion":")" IDF_VER R"(",)"
    R"("SysManager":{"monitorPeriodMs":10000,"reportList":["NetMan","RobotCtrl"]},)"
    R"("NetMan":{"WiFiEnabled":1, "defaultHostname":"Marty", "logLevel":"D"},)"
    R"("NTPClient":{"enable":1,"NTPServer":"pool.ntp.org", "GMTOffsetSecs":0, "DSTOffsetSecs":0},)"
    R"("MQTTManager":{"enable":0},)"
    R"("ESPOTAUpdate":{"enable":1,"OTADirectEnabled":0,"server":"192.168.86.235","port":5076,)"
            R"("sysName":")" SYSTEM_NAME R"(","sysVers":")" SYSTEM_VERSION R"("},)"
    R"("FileManager":{"SPIFFSEnabled":1,"SPIFFSFormatIfCorrupt":1,"SDEnabled":0,"CacheFileList":0},)"
    R"("WebServer":{"enable":1,"webServerPort":80,"allowOriginAll":1,"apiPrefix":"api/","logLevel":"D"},)"
    R"("SerialConsole":{"enable":1,"uartNum":0,"baudRate":115200,"crlfOnTx":1,"logLevel":"D"},)"
    R"("CommandSerial":{"enable":1,"uartNum":1,"baudRate":912600,"rxBufSize":1024,"rxPin":35,"txPin":12,)"
            R"("protocol":"RICSerial","logLevel":"D"},)"
    R"("TelnetServer":{"enable":1,"port":23},)"
    R"("CommandSocket":{"enable":1,"socketPort":24,"protocol":"Marty1ShortCodes","logLevel":"D"})"
    R"(})"
    ;

TEST_CASE("test_getString", "[raftjsonprefixed]")
{
    // Debug
    // LOG_I(MODULE_PREFIX, "JSON input string\n%s", testJSONConfigBase);

    // Test getString
    struct TestElem
    {
        const char* prefix;
        const char* dataPath;
        const char* expStr;
    };
    TestElem getStringTests [] = {
        { "SysManager", "reportList", "[\"NetMan\",\"RobotCtrl\"]" },
        { "ESPOTAUpdate", "server", "192.168.86.235" },
        { "CommandSocket", "protocol", "Marty1ShortCodes" },
        { "CommandSocket", "logLevel", "D" },
    };
    for (int testIdx = 0; testIdx < sizeof(getStringTests)/sizeof(getStringTests[0]); testIdx++)
    {
        String tokStartStr = "testGetString testIdx=" + String(testIdx);
        TEST_ASSERT_MESSAGE(testGetString(getStringTests[testIdx].prefix,
                                        getStringTests[testIdx].dataPath,
                                        getStringTests[testIdx].expStr,
                                        testJSONConfigBase), tokStartStr.c_str());
    }
}

// TEST_CASE("test_getLong", "[raftjsonprefixed]")
// {
//     ConfigBase config(revisionSwitchedJson);

//     long result = config.getLong("IntSetting", -2);
//     TEST_ASSERT_EQUAL_INT(42, result);

//     result = config.getLong("ArraySetting/[0]/__value__[1]", -3);
//     TEST_ASSERT_EQUAL_INT(20, result);

//     result = config.getLong("NoValue", -4);
//     TEST_ASSERT_EQUAL_INT(-4, result);

//     result = config.getLong("BoolSetting", 1);
//     TEST_ASSERT_EQUAL_INT(0, result);

// }

// TEST_CASE("test_getDouble", "[raftjsonprefixed]")
// {
//     ConfigBase config(revisionSwitchedJson);

//     double result = config.getDouble("e", -0.5);
//     TEST_ASSERT_EQUAL_DOUBLE(2.718, result);

//     result = config.getDouble("pi/[0]/__value__", -0.25);
//     TEST_ASSERT_EQUAL_DOUBLE(3.1415, result);

//     result = config.getDouble("NoValue", -0.125);
//     TEST_ASSERT_EQUAL_DOUBLE(-0.125, result);
// }

// TEST_CASE("test_getArrayElems", "[raftjsonprefixed]")
// {
//     ConfigBase config(revisionSwitchedJson);
//     std::vector<String> result;
//     bool success;

//     {
//         success = config.getArrayElems("ArraySetting", result);
//         TEST_ASSERT_TRUE_MESSAGE(success, "Could not get array elems of 'ArraySetting'");
//         const char* resultArr[result.size()];
//         for (int i = 0; i < result.size(); ++i)
//             resultArr[i] = result[i].c_str();
//         const char* expectedArr[3] = {"10", "20", "30"};
//         TEST_ASSERT_EQUAL_STRING_ARRAY(expectedArr, resultArr, result.size());
//     }
//     {
//         success = config.getArrayElems("ArrayWithSwitchedElem", result);
//         TEST_ASSERT_TRUE_MESSAGE(success, "Could not get array elems of 'ArrayWithSwitchedElem'");

//         const char* resultArr[result.size()];
//         for (int i = 0; i < result.size(); ++i)
//             resultArr[i] = result[i].c_str();

//         const char* expectedArr[4] = {"normalElement1", "normalElement2", "", "normalElement3"};
//         expectedArr[2] = switchedElem.c_str();

//         TEST_ASSERT_EQUAL_STRING_ARRAY(expectedArr, resultArr, result.size());
//     }
//     success = config.getArrayElems("StringSetting", result);
//     TEST_ASSERT_FALSE_MESSAGE(success, "'StringSetting' misinterpreted as array");
// }

// TEST_CASE("test_getKeys", "[raftjsonprefixed]")
// {
//     ConfigBase config(revisionSwitchedJson);
//     std::vector<String> result;
//     bool success;

//     {
//         success = config.getKeys("ObjectSetting", result);
//         TEST_ASSERT_TRUE_MESSAGE(success, "Could not get keys of 'ObjectSetting'");
//         const char* resultArr[result.size()];
//         for (int i = 0; i < result.size(); ++i)
//             resultArr[i] = result[i].c_str();
//         const char* expectedArr[] = {"str", "int", "float", "bool"};
//         TEST_ASSERT_EQUAL_STRING_ARRAY(expectedArr, resultArr, result.size());
//     }
//     {
//         success = config.getKeys("", result);
//         TEST_ASSERT_TRUE_MESSAGE(success, "Could not get keys of ''");
//         const char* resultArr[result.size()];
//         for (int i = 0; i < result.size(); ++i)
//             resultArr[i] = result[i].c_str();
//         const char* expectedArr[] = {"StringSetting", "StringDefault", "pi", "e", "IntSetting", "BoolSetting",
//                                       "NoValue", "ArraySetting", "ObjectSetting", "ArrayWithSwitchedElem"};
//         TEST_ASSERT_EQUAL_STRING_ARRAY(expectedArr, resultArr, result.size());
//     }
//     {
//         success = config.getKeys("pi[0]", result);
//         TEST_ASSERT_TRUE_MESSAGE(success, "Could not get keys of 'pi[0]'");
//         const char* resultArr[result.size()];
//         for (int i = 0; i < result.size(); ++i)
//             resultArr[i] = result[i].c_str();
//         const char* expectedArr[2] = {"__hwRevs__", "__value__"};
//         TEST_ASSERT_EQUAL_STRING_ARRAY(expectedArr, resultArr, result.size());
//     }

//     // These should fail
//     success = config.getKeys("StringSetting", result);
//     TEST_ASSERT_FALSE_MESSAGE(success, "'StringSetting' misinterpreted as object");
//     success = config.getKeys("NoValue", result);
//     TEST_ASSERT_FALSE_MESSAGE(success, "'NoValue' misinterpreted as object");
//     success = config.getKeys("NONEXISTENT_KEY", result);
//     TEST_ASSERT_FALSE_MESSAGE(success, "'NONEXISTENT_KEY' misinterpreted as object");
// }