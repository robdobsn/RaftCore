#include <stdio.h>
#include <string>
#include <vector>
#define DEBUG_DEVICE_TYPE_RECORDS 1
#include "../components/core/DeviceTypes/DeviceTypeRecords.h"

// Test wrapper class
class DeviceTypeRecordsTestWrapper {
public:
    static bool extractCheckInfoFromHexStr(const String& readStr, 
                                          std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>& checkValues,
                                          bool maskToZeros) {
        return DeviceTypeRecords::extractCheckInfoFromHexStr(readStr, checkValues, maskToZeros);
    }
};

int main() {
    // Test single simple case
    printf("Testing single legacy format: MAX30101\n");
    printf("Detection string: '0xff=0x15'\n\n");
    
    std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> checkValues;
    bool result = DeviceTypeRecordsTestWrapper::extractCheckInfoFromHexStr(String("0xff=0x15"), checkValues, false);
    
    printf("Parse result: %s\n", result ? "SUCCESS" : "FAILED");
    printf("Number of check pairs: %zu\n", checkValues.size());
    
    if (!checkValues.empty()) {
        printf("\nParsed values:\n");
        for (size_t i = 0; i < checkValues.size(); i++) {
            printf("  Pair %zu:\n", i);
            printf("    Mask: ");
            for (auto b : checkValues[i].first) printf("0x%02x ", b);
            printf("\n    Expected: ");
            for (auto b : checkValues[i].second) printf("0x%02x ", b);
            printf("\n");
        }
    }
    
    return result ? 0 : 1;
}
