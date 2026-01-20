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

static bool testGetString(const char* pathPrefix,
            const char* dataPath, const char* expectedStr, const char* pSourceStr)
{
    // Config
    RaftJson testConfigBase = pSourceStr;
    RaftJsonPrefixed testConfig(testConfigBase, pathPrefix);

    // Get string
    String rsltStr = testConfig.getString(dataPath, "<<NOT_FOUND>>");
    LOG_I("RaftJsonPrefixed_unit_test", "testGetString prefix %s dataPath %s expected %s got %s", pathPrefix, dataPath, expectedStr, rsltStr.c_str());
    if (!rsltStr.equals(expectedStr))
    {
        LOG_E("RaftJsonPrefixed_unit_test", "testGetString dataPath %s expected %s != %s", dataPath, expectedStr, rsltStr.c_str());
        return false;
    }
    return true;
}

#define SYSTEM_NAME "FirmwareESP32"
const char* testJSONConfigBase =
    R"({)"
    R"("SystemName":")" SYSTEM_NAME R"(",)"
    R"("SystemVersion":"UnitTests",)"
    R"("IDFVersion":")" IDF_VER R"(",)"
    R"("SysManager":{"monitorPeriodMs":10000,"reportList":["NetMan","RobotCtrl"]},)"
    R"("NetMan":{"WiFiEnabled":1, "defaultHostname":"Marty", "logLevel":"D"},)"
    R"("NTPClient":{"enable":1,"NTPServer":"pool.ntp.org", "GMTOffsetSecs":0, "DSTOffsetSecs":0},)"
    R"("MQTTManager":{"enable":0},)"
    R"("ESPOTAUpdate":{"enable":1,"OTADirectEnabled":0,"server":"192.168.86.235","port":5076,)"
            R"("sysName":")" SYSTEM_NAME R"(","sysVers":"UnitTests"},)"
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
    LOG_I("RaftJsonPrefixed_unit_test", "JSON input string\n%s", testJSONConfigBase);

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
