// Minimal C++ stubs for platform-independent symbols that are not compiled
// into the linux_unit_tests build but are referenced in object files that are.
// Add stubs here as needed rather than pulling in their full translation units.

#include "DeviceManager.h"

RaftDevice* DeviceManager::getDevice(const String& deviceStr, bool tryConfigName) const
{
    return nullptr;
}
