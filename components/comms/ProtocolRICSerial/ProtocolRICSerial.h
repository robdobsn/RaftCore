/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ProtocolRICSerial
// Protocol wrapper implementing RICSerial
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Logger.h"
#include "ProtocolBase.h"

class MiniHDLC;
class RaftJsonIF;

class ProtocolRICSerial : public ProtocolBase
{
public:
    ProtocolRICSerial(uint32_t channelID, RaftJsonIF& config, const char* pConfigPrefix, 
                        CommsChannelOutboundHandleMsgFnType msgTxCB, 
                        CommsChannelInboundHandleMsgFnType msgRxCB, 
                        CommsChannelInboundCanAcceptFnType readyToRxCB);
    virtual ~ProtocolRICSerial();
    
    // Create instance
    static ProtocolBase* createInstance(uint32_t channelID, RaftJsonIF& config, const char* pConfigPrefix, 
                        CommsChannelOutboundHandleMsgFnType msgTxCB, 
                        CommsChannelInboundHandleMsgFnType msgRxCB, 
                        CommsChannelInboundCanAcceptFnType readyToRxCB)
    {
        return new ProtocolRICSerial(channelID, config, pConfigPrefix, msgTxCB, msgRxCB, readyToRxCB);
    }

    virtual void addRxData(const uint8_t* pData, uint32_t dataLen) override final;
    virtual void encodeTxMsgAndSend(CommsChannelMsg& msg) override final;

    virtual const char* getProtocolName() override final
    {
        return getProtocolNameStatic();
    }

    static const char* getProtocolNameStatic()
    {
        return "RICSerial";
    }

    static bool decodeIntoCommsChannelMsg(uint32_t channelID, const uint8_t* pFrame, int frameLen, CommsChannelMsg& msg);

private:
    // HDLC
    MiniHDLC* _pHDLC;

    // Debug
    uint32_t _debugLastInReportMs = 0;
    uint32_t _debugNumBytesRx = 0;

    // Helpers
    void hdlcFrameRxCB(const uint8_t* pFrame, int frameLen);

    // Debug
    static constexpr const char* MODULE_PREFIX = "RICSerial";    
};
