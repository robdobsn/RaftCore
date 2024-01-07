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
#include "RaftArduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SYSTEM_NAME "FirmwareESP32"
#define SYSTEM_VERSION "0.0.3"
const char* testJsonDoc =
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

static void save_config_and_reset(void)
{
    printf("Writing JSON into NVS\n");
    // Create RaftJsonNVS
    RaftJsonNVS raftJsonNVS("test", 10000);

    // Set the JSON
    // This should write to NVS
    raftJsonNVS.setJsonDoc(testJsonDoc);

    printf("Restarting\n");
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // Reset to test that noinit memory is left intact.
    esp_restart();
}

static void check_nvs_contents(void)
{
    // Confirm that JSON is correct
    printf("Checking NVS contents\n");
    // Create RaftJsonNVS
    RaftJsonNVS raftJsonNVS("test", 10000);

    // Compare JSON with expected
    String jsonStr = raftJsonNVS.getJsonDoc();
    bool match = false;
    if (jsonStr.equals(testJsonDoc))
    {
        printf("JSON matches\n");
        match = true;
    }
    else
    {
        printf("JSON does not match\n");
        printf("Expected:\n%s\n", testJsonDoc);
        printf("Actual:\n%s\n", jsonStr.c_str());
    }
    TEST_ASSERT(match);
}

TEST_CASE_MULTIPLE_STAGES("RaftJsonNVS test", "[jsonnvs]", save_config_and_reset, check_nvs_contents);
