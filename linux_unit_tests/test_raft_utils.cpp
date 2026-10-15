/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftUtils tests
//
// Integer list parsing (used for device addresses) and hex string conversion (used for device type
// record write and check values).
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <string>
#include <vector>
#include "testhdr.h"
#include "ArduinoWString.h"
#include "RaftUtils.h"

static void testParseIntList()
{
    printf("Running testParseIntList...\n");
    struct TestCase
    {
        const char* inputStr;
        const char* listSep;
        const char* rangeSep;
        uint32_t maxNum;
        std::vector<int> expectedOutput;
        bool expectedResult;
    };
    const std::vector<TestCase> testCases = {
        // Basic lists and ranges
        {"1,2,3,4", ",", "-", 10, {1, 2, 3, 4}, true},
        {"1-3,5", ",", "-", 10, {1, 2, 3, 5}, true},
        // Hex, as used for device addresses
        {"0x18-0x1a,0x20", ",", "-", 10, {0x18, 0x19, 0x1a, 0x20}, true},
        // Limited by maxNum
        {"1,2,3,4,5", ",", "-", 3, {1, 2, 3}, false},
        {"1-5", ",", "-", 3, {1, 2, 3}, false},
        // Custom separators
        {"1;2;3-5;6", ";", "-", 10, {1, 2, 3, 4, 5, 6}, true},
        {"1to3;4", ";", "to", 10, {1, 2, 3, 4}, true},
        // Empty input
        {"", ",", "-", 10, {}, true},
    };
    for (size_t i = 0; i < testCases.size(); ++i)
    {
        const TestCase& tc = testCases[i];
        std::vector<int> output;
        bool result = Raft::parseIntList(tc.inputStr, output, tc.listSep, tc.rangeSep, tc.maxNum);
        std::string name = std::string("parseIntList \"") + tc.inputStr + "\"";
        TEST_ASSERT(result == tc.expectedResult, (name + " result").c_str());
        TEST_ASSERT(output == tc.expectedOutput, (name + " values").c_str());
    }
}

static void testHexConversion()
{
    printf("Running testHexConversion...\n");

    // Hex string to bytes, with and without a 0x prefix, either case
    uint8_t buf[4] = {};
    TEST_ASSERT(Raft::getBytesFromHexStr("0x12aBcD", buf, sizeof(buf)) == 3, "hexToBytesPrefixedLen");
    TEST_ASSERT(buf[0] == 0x12 && buf[1] == 0xab && buf[2] == 0xcd, "hexToBytesPrefixed");
    TEST_ASSERT(Raft::getBytesFromHexStr("ff00", buf, sizeof(buf)) == 2, "hexToBytesUnprefixedLen");
    TEST_ASSERT(buf[0] == 0xff && buf[1] == 0x00, "hexToBytesUnprefixed");

    // Output is limited by the buffer size, and an odd trailing digit is ignored
    TEST_ASSERT(Raft::getBytesFromHexStr("0102030405", buf, 2) == 2, "hexToBytesLimitedByBuffer");
    TEST_ASSERT(Raft::getBytesFromHexStr("123", buf, sizeof(buf)) == 1 && buf[0] == 0x12, "hexToBytesOddLength");

    // There is no validation: a non-hex character decodes as some digit ('x' as 8). Callers must
    // decide a value is hex before decoding it - see test_device_type_records for the bug this caused
    TEST_ASSERT(Raft::getBytesFromHexStr("xx", buf, sizeof(buf)) == 1 && buf[0] == 0x88, "hexToBytesNoValidation");

    // Vector form
    std::vector<uint8_t> bytes = Raft::getBytesFromHexStr("0x0a0b", 10);
    TEST_ASSERT(bytes.size() == 2 && bytes[0] == 0x0a && bytes[1] == 0x0b, "hexToBytesVector");

    // Bytes to hex string
    const uint8_t data[] = { 0x00, 0x7f, 0xab };
    String hexStr;
    Raft::getHexStrFromBytes(data, sizeof(data), hexStr);
    TEST_ASSERT(hexStr == "007fab", "bytesToHex");
}

int main()
{
    testParseIntList();
    testHexConversion();
    return testSummary("test_raft_utils");
}
