/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Bus Address Record
//
// Rob Dobson 2025
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "BusAddrRecord.h"

// #define DEBUG_BUS_ADDR_RECORD_FOR_ADDRESS 0x310

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle device responding information
/// @param isResponding true if device is responding
/// @param flagSpuriousRecord (out) true if this is a spurious record
/// @param okMax max number of successful responses before declaring online
/// @param failMax max number of failed responses before declaring offline
/// @return true if status has changed
bool BusAddrRecord::handleResponding(bool isResponding, bool &flagSpuriousRecord, uint32_t okMax, uint32_t failMax)
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
                isChange = true;
                count = 0;
                onlineState = DeviceOnlineState::ONLINE;

#ifdef DEBUG_BUS_ADDR_RECORD_FOR_ADDRESS
                if (address == DEBUG_BUS_ADDR_RECORD_FOR_ADDRESS)
                {
                    LOG_I("BusAddrRecord", "handleResponding CHANGE TO ONLINE address %04x isResponding %d onlineState %s count %d recordIsChange %d return TRUE", 
                            address, isResponding, BusAddrStatus::getOnlineStateStr(onlineState), count, isChange);
                }
#endif
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
                    isChange = true;
                onlineState = DeviceOnlineState::OFFLINE;

#ifdef DEBUG_BUS_ADDR_RECORD_FOR_ADDRESS
                if (address == DEBUG_BUS_ADDR_RECORD_FOR_ADDRESS)
                { 
                    LOG_I("BusAddrRecord", "handleResponding CHANGE TO OFFLINE address %04x isResponding %d onlineState %s count %d recordIsChange %d return TRUE", 
                            address, isResponding, BusAddrStatus::getOnlineStateStr(onlineState), count, isChange);
                }
#endif

                return true;
            }
        }
    }

#ifdef DEBUG_BUS_ADDR_RECORD_FOR_ADDRESS
    if (address == DEBUG_BUS_ADDR_RECORD_FOR_ADDRESS)
    {
        LOG_I("BusAddrRecord", "handleResponding NO CHANGE address %04x isResponding %d onlineState %s count %d recordIsChange %d return FALSE", 
                address, isResponding, BusAddrStatus::getOnlineStateStr(onlineState), count, isChange);
    }
#endif
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get JSON for device status
/// @return JSON string
String BusAddrRecord::getJson() const
{
    // Create JSON
    char jsonStr[128];
    snprintf(jsonStr, sizeof(jsonStr), 
        "{\"a\":\"%s%x\",\"s\":\"%c%c\",\"t\":%u}", 
        RAFT_BUS_ADDR_PREFIX,
        (unsigned int)address, 
        BusAddrStatus::getOnlineStateStr(onlineState)[0],
        isNewlyIdentified ? 'N' : 'X',
        (unsigned int)deviceStatus.deviceTypeIndex
    );
    return jsonStr;
}
