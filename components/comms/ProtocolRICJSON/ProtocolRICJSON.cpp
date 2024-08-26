/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ProtocolRICJSON
// Protocol packet contains JSON with no additional overhead
//
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "ProtocolRICJSON.h"
#include "CommsChannelMsg.h"
#include "RaftJsonIF.h"

// Debug
// #define DEBUG_PROTOCOL_RIC_JSON

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ProtocolRICJSON::ProtocolRICJSON(uint32_t channelID, RaftJsonIF& config, const char* pConfigPrefix, 
                CommsChannelOutboundHandleMsgFnType msgTxCB, 
                CommsChannelInboundHandleMsgFnType msgRxCB, 
                CommsChannelInboundCanAcceptFnType readyToRxCB) :
    ProtocolBase(channelID, msgTxCB, msgRxCB, readyToRxCB)
{
    // Debug
    // LOG_I(MODULE_PREFIX, "constructed");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ProtocolRICJSON::~ProtocolRICJSON()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// addRxData
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ProtocolRICJSON::addRxData(const uint8_t* pData, uint32_t dataLen)
{
    // Check callback is valid
    if (!_msgRxCB)
        return;

    // Check validity of frame length
    if (dataLen < 2)
        return;

    // Debug
#ifdef DEBUG_PROTOCOL_RIC_JSON
    LOG_I(MODULE_PREFIX, "addRxData len %d", dataLen);
#endif

    // Convert to CommsChannelMsg
    CommsChannelMsg endpointMsg;
    endpointMsg.setFromBuffer(_channelID, MSG_PROTOCOL_RAWCMDFRAME, 0, MSG_TYPE_COMMAND, pData, dataLen);
    _msgRxCB(endpointMsg);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Decode
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ProtocolRICJSON::decodeParts(const uint8_t* pData, uint32_t dataLen, uint32_t& msgNumber,
                uint32_t& msgProtocolCode, uint32_t& msgTypeCode, uint32_t& payloadStartPos)
{
    // Extract message type
    msgNumber = 0;
    msgProtocolCode = 0;
    msgTypeCode = 0;
    payloadStartPos = 0;
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Encode
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef IMPLEMENT_NO_PSRAM_FOR_RIC_JSON
void ProtocolRICJSON::encode(CommsChannelMsg& msg, std::vector<uint8_t>& outMsg)
#else
void ProtocolRICJSON::encode(CommsChannelMsg& msg, std::vector<uint8_t, SpiramAwareAllocator<uint8_t>>& outMsg)
#endif
{
    // Create the message
    outMsg.assign(msg.getCmdVector().begin(), msg.getCmdVector().end());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Encode and send
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ProtocolRICJSON::encodeTxMsgAndSend(CommsChannelMsg& msg)
{
    // Valid
    if (!_msgTxCB)
    {
        #ifdef DEBUG_PROTOCOL_RIC_JSON
            // Debug
            LOG_I(MODULE_PREFIX, "encodeTxMsgAndSend NO TX CB");
        #endif
        return;
    }

    // Encode
#ifdef IMPLEMENT_NO_PSRAM_FOR_RIC_JSON
    std::vector<uint8_t> ricJSONMsg;
#else
    std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> ricJSONMsg;
#endif
    encode(msg, ricJSONMsg);
    msg.setFromBuffer(ricJSONMsg.data(), ricJSONMsg.size());

#ifdef DEBUG_PROTOCOL_RIC_JSON
    // Debug
    LOG_I(MODULE_PREFIX, "encodeTxMsgAndSend, encoded len %d", msg.getBufLen());
#endif

    // Send
    _msgTxCB(msg);
}
