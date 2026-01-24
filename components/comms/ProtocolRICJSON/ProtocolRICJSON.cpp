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
// #define DEBUG_ENCODE_TX_MSG_TIMING

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
void ProtocolRICJSON::encode(CommsChannelMsg& msg, SpiramAwareUint8Vector& outMsg)
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
#if defined(DEBUG_ENCODE_TX_MSG_TIMING) && defined(ESP_PLATFORM)
    static uint32_t encodeCycles = 0;
    static uint32_t setFromBufferCycles = 0;
    static uint32_t msgTxCBCycles = 0;
    static uint32_t lastReportMs = 0;
    static uint32_t callCount = 0;
    uint32_t startCycles = xthal_get_ccount();
#endif

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
    SpiramAwareUint8Vector ricJSONMsg;
#endif
    encode(msg, ricJSONMsg);

#if defined(DEBUG_ENCODE_TX_MSG_TIMING) && defined(ESP_PLATFORM)
    uint32_t afterEncodeCycles = xthal_get_ccount();
    encodeCycles += (afterEncodeCycles - startCycles);
#endif

    msg.setFromBuffer(ricJSONMsg.data(), ricJSONMsg.size());

#if defined(DEBUG_ENCODE_TX_MSG_TIMING) && defined(ESP_PLATFORM)
    uint32_t afterSetFromBufferCycles = xthal_get_ccount();
    setFromBufferCycles += (afterSetFromBufferCycles - afterEncodeCycles);
#endif

#ifdef DEBUG_PROTOCOL_RIC_JSON
    // Debug
    LOG_I(MODULE_PREFIX, "encodeTxMsgAndSend, encoded len %d", msg.getBufLen());
#endif

    // Send
    _msgTxCB(msg);

#if defined(DEBUG_ENCODE_TX_MSG_TIMING) && defined(ESP_PLATFORM)
    uint32_t afterMsgTxCBCycles = xthal_get_ccount();
    msgTxCBCycles += (afterMsgTxCBCycles - afterSetFromBufferCycles);
    callCount++;

    // Report every 5 seconds
    uint32_t nowMs = millis();
    if (nowMs - lastReportMs > 5000)
    {
        LOG_I(MODULE_PREFIX, "encodeTxMsgAndSend timing (us): encode=%d setFromBuffer=%d msgTxCB=%d calls=%d",
            encodeCycles / 240, setFromBufferCycles / 240, msgTxCBCycles / 240, callCount);
        encodeCycles = 0;
        setFromBufferCycles = 0;
        msgTxCBCycles = 0;
        callCount = 0;
        lastReportMs = nowMs;
    }
#endif
}
