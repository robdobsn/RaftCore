/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ProtocolRICJSON
// Protocol packet contains JSON with no additional overhead
//
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// TODO - determine if this has a positive impact on memory use or has some unknown negative impact
// #define IMPLEMENT_USE_PSRAM_FOR_RIC_JSON

#include <Logger.h>
#include <ProtocolBase.h>
#include <SpiramAwareAllocator.h>
#include <vector>

class ProtocolRICJSON : public ProtocolBase
{
public:
    ProtocolRICJSON(uint32_t channelID, ConfigBase& config, const char* pConfigPrefix, 
                    CommsChannelOutboundHandleMsgFnType msgTxCB, 
                    CommsChannelInboundHandleMsgFnType msgRxCB, 
                    CommsChannelInboundCanAcceptFnType readyToRxCB);
    virtual ~ProtocolRICJSON();

    // Create instance
    static ProtocolBase* createInstance(uint32_t channelID, ConfigBase& config, const char* pConfigPrefix, 
                    CommsChannelOutboundHandleMsgFnType msgTxCB, 
                    CommsChannelInboundHandleMsgFnType msgRxCB, 
                    CommsChannelInboundCanAcceptFnType readyToRxCB)
    {
        return new ProtocolRICJSON(channelID, config, pConfigPrefix, msgTxCB, msgRxCB, readyToRxCB);
    }

    virtual void addRxData(const uint8_t* pData, uint32_t dataLen) override final;
    static bool decodeParts(const uint8_t* pData, uint32_t dataLen, uint32_t& msgNumber,
                    uint32_t& msgProtocolCode, uint32_t& msgTypeCode, uint32_t& payloadStartPos);

    virtual void encodeTxMsgAndSend(CommsChannelMsg& msg) override final;
#ifdef IMPLEMENT_USE_PSRAM_FOR_RIC_JSON
    static void encode(CommsChannelMsg& msg, std::vector<uint8_t, SpiramAwareAllocator<uint8_t>>& outMsg);
#else
    static void encode(CommsChannelMsg& msg, std::vector<uint8_t>& outMsg);
#endif

    virtual const char* getProtocolName() override final
    {
        return getProtocolNameStatic();
    }

    static const char* getProtocolNameStatic()
    {
        return "RICJSON";
    }
};