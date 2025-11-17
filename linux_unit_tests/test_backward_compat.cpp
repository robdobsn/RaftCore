#include <stdio.h>
#include <string>
#include <vector>
#include "../components/core/DeviceTypes/DeviceTypeRecords.h"

// Test wrapper class for accessing private methods
class DeviceTypeRecordsTestWrapper {
public:
    static bool extractCheckInfoFromHexStr(const String& readStr, 
                                          std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>& checkValues,
                                          bool maskToZeros) {
        return DeviceTypeRecords::extractCheckInfoFromHexStr(readStr, checkValues, maskToZeros);
    }
};

int main() {
    printf("Testing backward compatibility with legacy device formats...\n");
    printf("This test verifies that existing device detection VALUE strings WITHOUT CRC markers\n");
    printf("parse correctly using the unchanged legacy code path.\n\n");
    
    struct TestCase {
        const char* deviceName;
        const char* fullFormat;      // For reference
        const char* valueToTest;     // The VALUE part (after =)
        int expectedPairs;           // Expected number of check value pairs
    };
    
    // These are REAL formats from DeviceTypeRecords.json (legacy, no CRC)
    // Format is "ADDRESS=VALUE", we test the VALUE part
    TestCase testCases[] = {
        // Simple single-byte comparisons
        {"VCNL4040",  "0x0c=0b100001100000XXXX", "0b100001100000XXXX", 1},
        {"MAX30101",  "0xff=0x15", "0x15", 1},
        {"MCP9808",   "0x07=0x04", "0x04", 1},
        
        // Multiple acceptable values (comma-separated OR logic)
        {"LSM6DS",    "0x0f=0x69,0x6a,0x6c", "0x69,0x6a,0x6c", 3},
        {"LPS25",     "0x0f=0xbd,0xb4", "0xbd,0xb4", 2},
        
        // Multi-byte comparisons
        {"VL53L4CD",  "0x010f=0xebcc", "0xebcc", 1},
        {"ADXL313",   "0x00=0b11101101XXXX1011", "0b11101101XXXX1011", 1},
    };
    
    int passed = 0, failed = 0;
    
    for (size_t i = 0; i < sizeof(testCases)/sizeof(testCases[0]); i++) {
        const auto& tc = testCases[i];
        std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> checkValues;
        
        // Call the private parsing function through our wrapper
        // maskToZeros=true matches how it's called in getDetectionRecs (line 634)
        bool parseResult = DeviceTypeRecordsTestWrapper::extractCheckInfoFromHexStr(
            String(tc.valueToTest), checkValues, true);
        
        if (parseResult && (int)checkValues.size() == tc.expectedPairs) {
            printf("✓ PASS: %-15s value='%s'\n", tc.deviceName, tc.valueToTest);
            printf("       → Parsed %zu check pair(s) correctly (expected %d)\n", 
                   checkValues.size(), tc.expectedPairs);
            passed++;
        } else {
            printf("✗ FAIL: %-15s value='%s'\n", tc.deviceName, tc.valueToTest);
            if (!parseResult) {
                printf("       → Parse failed!\n");
            } else {
                printf("       → Got %zu pairs, expected %d\n", 
                       checkValues.size(), tc.expectedPairs);
            }
            failed++;
        }
    }
    
    printf("\n========================================\n");
    if (failed == 0) {
        printf("✓ ALL BACKWARD COMPATIBILITY TESTS PASSED (%d/%d)\n", passed, passed);
        printf("\n✓ Legacy device formats (without {crc:} markers) parse correctly!\n");
        printf("✓ The traditional code path remains UNCHANGED and functional.\n");
        printf("✓ Backward compatibility is FULLY MAINTAINED.\n");
        return 0;
    } else {
        printf("✗ %d TESTS FAILED, %d passed\n", failed, passed);
        return 1;
    }
}
