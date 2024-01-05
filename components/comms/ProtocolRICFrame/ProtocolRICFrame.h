/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ProtocolRICFrame
// Protocol wrapper implementing RICFrame
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include "Logger.h"
#include "ProtocolBase.h"
#include "SpiramAwareAllocator.h"

class RaftJsonIF;

class ProtocolRICFrame : public ProtocolBase
{
public:
    ProtocolRICFrame(uint32_t channelID, RaftJsonIF& config, const char* pConfigPrefix, 
                            CommsChannelOutboundHandleMsgFnType msgTxCB, 
                            CommsChannelInboundHandleMsgFnType msgRxCB, 
                            CommsChannelInboundCanAcceptFnType readyToRxCB);
    virtual ~ProtocolRICFrame();
    
    // Create instance
    static ProtocolBase* createInstance(uint32_t channelID, RaftJsonIF& config, const char* pConfigPrefix, 
                CommsChannelOutboundHandleMsgFnType msgTxCB, 
                CommsChannelInboundHandleMsgFnType msgRxCB, 
                CommsChannelInboundCanAcceptFnType readyToRxCB)
    {
        return new ProtocolRICFrame(channelID, config, pConfigPrefix, msgTxCB, msgRxCB, readyToRxCB);
    }

    virtual void addRxData(const uint8_t* pData, uint32_t dataLen) override final;
    static bool decodeParts(const uint8_t* pData, uint32_t dataLen, uint32_t& msgNumber, 
                    uint32_t& msgProtocolCode, uint32_t& msgTypeCode, uint32_t& payloadStartPos);

    virtual void encodeTxMsgAndSend(CommsChannelMsg& msg) override final;
    static void encode(CommsChannelMsg& msg, std::vector<uint8_t>& outMsg);
    static void encode(CommsChannelMsg& msg, std::vector<uint8_t, SpiramAwareAllocator<uint8_t>>& outMsg);

    virtual const char* getProtocolName() override final
    {
        return getProtocolNameStatic();
    }

    static const char* getProtocolNameStatic()
    {
        return "RICFrame";
    }
};
