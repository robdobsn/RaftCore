/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ProtocolRICFrame
// Protocol wrapper implementing RICFrame
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "ProtocolRICFrame.h"
#include "CommsChannelMsg.h"
#include "RaftJsonIF.h"

// Debug
// #define DEBUG_PROTOCOL_RIC_FRAME
// #define DEBUG_CONSTRUCTOR

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ProtocolRICFrame::ProtocolRICFrame(uint32_t channelID, RaftJsonIF& config, const char* pConfigPrefix, 
                CommsChannelOutboundHandleMsgFnType msgTxCB, 
                CommsChannelInboundHandleMsgFnType msgRxCB, 
                CommsChannelInboundCanAcceptFnType readyToRxCB) :
    ProtocolBase(channelID, msgTxCB, msgRxCB, readyToRxCB)
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ProtocolRICFrame::~ProtocolRICFrame()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// addRxData
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ProtocolRICFrame::addRxData(const uint8_t* pData, uint32_t dataLen)
{
    // Debug
    // LOG_V(MODULE_PREFIX, "addRxData len %d", dataLen);

    // Check callback is valid
    if (!_msgRxCB)
        return;

    // Check validity of frame length
    if (dataLen < 2)
        return;

    // Extract message type
    uint32_t msgNumber = pData[0];
    uint32_t msgProtocolCode = pData[1] & 0x3f;
    uint32_t msgTypeCode = pData[1] >> 6;

    // Debug
#ifdef DEBUG_PROTOCOL_RIC_FRAME
    LOG_I(MODULE_PREFIX, "addRxData len %d msgNum %d protocolCode %d msgTypeCode %d", 
                    dataLen, msgNumber, msgProtocolCode, msgTypeCode);
#endif

    // Convert to CommsChannelMsg
    CommsChannelMsg endpointMsg;
    endpointMsg.setFromBuffer(_channelID, (CommsMsgProtocol)msgProtocolCode, msgNumber, (CommsMsgTypeCode)msgTypeCode, pData+2, dataLen-2);
    _msgRxCB(endpointMsg);

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Decode
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ProtocolRICFrame::decodeParts(const uint8_t* pData, uint32_t dataLen, uint32_t& msgNumber, 
                uint32_t& msgProtocolCode, uint32_t& msgTypeCode, uint32_t& payloadStartPos)
{
    // Check validity of frame length
    if (dataLen < 2)
        return false;

    // Extract message type
    msgNumber = pData[0];
    msgProtocolCode = pData[1] & 0x3f;
    msgTypeCode = pData[1] >> 6;
    payloadStartPos = 2;
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Encode (standard buffer)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ProtocolRICFrame::encode(CommsChannelMsg& msg, std::vector<uint8_t>& outMsg)
{
    // Create the message
    outMsg.reserve(msg.getBufLen()+2);
    outMsg.push_back(msg.getMsgNumber());
    uint8_t protocolTypeByte = ((msg.getMsgTypeCode() & 0x03) << 6) + (msg.getProtocol() & 0x3f);
    outMsg.push_back(protocolTypeByte);
    outMsg.insert(outMsg.end(), msg.getCmdVector().begin(), msg.getCmdVector().end());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Encode (PSRAM buffer)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ProtocolRICFrame::encode(CommsChannelMsg& msg, SpiramAwareUint8Vector& outMsg)
{
    // Create the message
    outMsg.reserve(msg.getBufLen()+2);
    outMsg.push_back(msg.getMsgNumber());
    uint8_t protocolTypeByte = ((msg.getMsgTypeCode() & 0x03) << 6) + (msg.getProtocol() & 0x3f);
    outMsg.push_back(protocolTypeByte);
    outMsg.insert(outMsg.end(), msg.getCmdVector().begin(), msg.getCmdVector().end());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Encode and send
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ProtocolRICFrame::encodeTxMsgAndSend(CommsChannelMsg& msg)
{
    // Valid
    if (!_msgTxCB)
    {
        #ifdef DEBUG_PROTOCOL_RIC_FRAME
            // Debug
            LOG_I(MODULE_PREFIX, "encodeTxMsgAndSend NO TX CB");
        #endif
        return;
    }

    // Encode
#ifdef IMPLEMENT_NO_PSRAM_FOR_RIC_FRAME_ENCODING
    std::vector<uint8_t> ricFrameMsg;
#else
    SpiramAwareUint8Vector ricFrameMsg;
#endif
    encode(msg, ricFrameMsg);
    msg.setFromBuffer(ricFrameMsg.data(), ricFrameMsg.size());

#ifdef DEBUG_PROTOCOL_RIC_FRAME
    // Debug
    LOG_I(MODULE_PREFIX, "encodeTxMsgAndSend, encoded len %d", msg.getBufLen());
#endif

    // Send
    _msgTxCB(msg);
}
