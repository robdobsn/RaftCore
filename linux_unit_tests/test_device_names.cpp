// Standalone test runner for DeviceNames tests
// Build: make test_device_names
#include <stdio.h>
#include "ArduinoWString.h"
#include "RaftJson.h"
#include "DeviceNamesTest.h"

int main()
{
    DeviceNamesTest test;
    test.runTests();
    return 0;
}
