/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Bus Consts
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>

typedef uint32_t BusElemAddrType;

/// @struct BusElemAddrAndStatus
/// @brief Address and status of a bus element
/// @note isChangeToOnline indicates that the device was either in an unknown state or offline and has now become online
/// @note isChangeToOffline indicates that the device was online has now become offline
/// @note neither is set if the device was in an unknown state and is now known to be offline
/// @note if device has just been identified then isNewlyIdentified is true and deviceTypeIndex is valid
struct BusElemAddrAndStatus
{
    BusElemAddrType address;
    bool isChangeToOnline:1;
    bool isChangeToOffline:1;
    bool isNewlyIdentified:1;
    uint16_t deviceTypeIndex;
};

enum BusOperationStatus
{
    BUS_OPERATION_UNKNOWN,
    BUS_OPERATION_OK,
    BUS_OPERATION_FAILING
};
