/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Bus Address Status
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "BusAddrStatus.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle device responding information
/// @param isResponding true if device is responding
/// @param flagSpuriousRecord (out) true if this is a spurious record
/// @param okMax max number of successful responses before declaring online
/// @param failMax max number of failed responses before declaring offline
/// @return true if status has changed
bool BusAddrStatus::handleResponding(bool isResponding, bool &flagSpuriousRecord, uint32_t okMax, uint32_t failMax)
{
    // Handle is responding or not
    if (isResponding)
    {
        // If not already online then count upwards
        if (onlineState != DeviceOnlineState::ONLINE)
        {
            // Check if we've reached the threshold for online
            count = (count < okMax) ? count + 1 : count;
            if (count >= okMax)
            {
                // Now online
                isChange = !isChange;
                count = 0;
                onlineState = DeviceOnlineState::ONLINE;
                return true;
            }
        }
    }
    else
    {
        // Not responding - check for change to offline/spurious
        // Only count if we're online or still in initial state (not already offline)
        if (onlineState != DeviceOnlineState::OFFLINE)
        {
            // Count down to offline/spurious threshold
            count = (count < -failMax) ? count : count - 1;
            if (count <= -failMax)
            {
                // Now offline/spurious
                count = 0;
                if (onlineState == DeviceOnlineState::INITIAL)
                    flagSpuriousRecord = true;
                else
                    isChange = !isChange;
                onlineState = DeviceOnlineState::OFFLINE;
                return true;
            }
        }
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get JSON for device status
/// @return JSON string
String BusAddrStatus::getJson() const
{
    // Create JSON
    char jsonStr[128];
    snprintf(jsonStr, sizeof(jsonStr), 
        "{\"a\":\"0x%04X\",\"s\":\"%c%c\"}", 
        (int)address, 
        onlineState == DeviceOnlineState::ONLINE ? 'O' : (onlineState == DeviceOnlineState::OFFLINE ? 'F' : 'I'),
        isNewlyIdentified ? 'N' : 'X'
    );
    return jsonStr;
}
