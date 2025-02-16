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
/// @param address address of the bus element
/// @param isChangeToOnline true if the device has changed to online (from unknown or offline)
/// @param isChangeToOffline true if the device has changed to offline
/// @param isNewlyIdentified true if the device has just been identified
/// @param deviceTypeIndex index of the device type (if newly identified)
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
