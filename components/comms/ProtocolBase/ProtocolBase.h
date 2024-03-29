/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ProtocolBase
// Base class for protocol translations
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <functional>
#include "CommsCoreIF.h"

class CommsChannelMsg;
class ProtocolBase;
class RaftJsonIF;

// Put byte callback function type
typedef std::function<void(uint8_t ch)> ProtocolBasePutByteCBType;
// Received frame callback function type
typedef std::function<void(const uint8_t *framebuffer, int framelength)> ProtocolBaseFrameCBType;
// Create protocol instance
typedef std::function<ProtocolBase* (uint32_t channelID, RaftJsonIF& config, const char* pConfigPrefix, 
                CommsChannelOutboundHandleMsgFnType msgTxCB, 
                CommsChannelInboundHandleMsgFnType msgRxCB, 
                CommsChannelInboundCanAcceptFnType readyToRxCB)> ProtocolCreateFnType;

class ProtocolBase
{
public:
    ProtocolBase(uint32_t channelID, CommsChannelOutboundHandleMsgFnType msgTxCB, 
                    CommsChannelInboundHandleMsgFnType msgRxCB, 
                    CommsChannelInboundCanAcceptFnType readyToRxCB)
            : _channelID(channelID), _msgTxCB(msgTxCB), _msgRxCB(msgRxCB), _readyToRxCB(readyToRxCB)
    {
    }
    virtual ~ProtocolBase()
    {
    }

    // Add received data to be decoded
    virtual void addRxData(const uint8_t* pData, uint32_t dataLen) = 0;

    // Encode a message and send
    virtual void encodeTxMsgAndSend(CommsChannelMsg& msg) = 0;

    // Check if ready for received data
    virtual bool readyForRxData()
    {
        if (_readyToRxCB)
            return _readyToRxCB();
        return true;
    }

    // Protocol codec name
    virtual const char* getProtocolName()
    {
        return "BASE";
    }

    // Get channel ID
    uint32_t getChannelID()
    {
        return _channelID;
    }

protected:
    // The channelID for messages handled by this protocol handler
    uint32_t _channelID;

    // Callbacks
    CommsChannelOutboundHandleMsgFnType _msgTxCB;
    CommsChannelInboundHandleMsgFnType _msgRxCB;
    CommsChannelInboundCanAcceptFnType _readyToRxCB;
};
