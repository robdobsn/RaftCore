/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CommsBridgeMsg
//
// Rob Dobson 2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <ArduinoOrAlt.h>
#include <vector>
#include <RdJson.h>
#include <stdint.h>

enum CommsBridgeIndex
{
    COMMS_BRIDGE_ID_COM_SERIAL_0 = 0,
};

class CommsBridgeMsg
{
public:
    static uint32_t getBridgeIdx(const uint8_t* pBuf, uint32_t bufLen)
    {
        if (bufLen < 1)
            return COMMS_BRIDGE_ID_COM_SERIAL_0;
        return pBuf[0];
    }

    static uint32_t getPayloadPos(const uint8_t* pBuf, uint32_t bufLen)
    {
        return 1;
    }
};
