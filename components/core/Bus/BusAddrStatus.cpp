/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Bus Address Status - Lightweight status change notification
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "BusAddrStatus.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get JSON for device status
/// @return JSON string
String BusAddrStatus::getJson() const
{
    // Create JSON
    char jsonStr[128];
    snprintf(jsonStr, sizeof(jsonStr), 
        "{\"a\":\"%s%x\",\"s\":\"%c%c\",\"t\":%u}", 
        RAFT_BUS_ADDR_PREFIX,
        (unsigned int)address, 
        BusAddrStatus::getOnlineStateStr(onlineState)[0],
        isNewlyIdentified ? 'N' : 'X',
        (unsigned int)deviceTypeIndex
    );
    return jsonStr;
}
