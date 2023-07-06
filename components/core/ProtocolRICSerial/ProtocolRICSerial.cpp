/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ProtocolRICSerial
// Protocol wrapper implementing RICSerial
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "ProtocolRICSerial.h"
#include <CommsChannelMsg.h>
#include <ConfigBase.h>
#include <MiniHDLC.h>
#include <ArduinoOrAlt.h>
#include <RaftUtils.h>

// Logging
static const char* MODULE_PREFIX = "RICSerial";

// Warn
#define WARN_ON_NO_HDLC_HANDLER
#define WARN_ON_ENCODED_MSG_LEN_MISMATCH

// Debug
// #define DEBUG_PROTOCOL_RIC_SERIAL_DECODE_IN
// #define DEBUG_PROTOCOL_RIC_SERIAL_DECODE_IN_DETAIL
// #define DEBUG_PROTOCOL_RIC_SERIAL_DECODE_FRAME
// #define DEBUG_PROTOCOL_RIC_SERIAL_DECODE_FRAME_DETAIL
// #define DEBUG_PROTOCOL_RIC_SERIAL_ENCODE
// #define DEBUG_PROTOCOL_RIC_SERIAL_ENCODE_DETAIL

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ProtocolRICSerial::ProtocolRICSerial(uint32_t channelID, ConfigBase& config, const char* pConfigPrefix, 
                    CommsChannelMsgCB msgTxCB, CommsChannelMsgCB msgRxCB, CommsChannelReadyToRxCB readyToRxCB) :
    ProtocolBase(channelID, msgTxCB, msgRxCB, readyToRxCB)
{
    // Extract configuration
    _maxRxMsgLen = config.getLong("MaxRxMsgLen", DEFAULT_RIC_SERIAL_RX_MAX, pConfigPrefix);
    _maxTxMsgLen = config.getLong("MaxTxMsgLen", DEFAULT_RIC_SERIAL_TX_MAX, pConfigPrefix);
    unsigned frameBoundary = config.getLong("FrameBound", 0x7E, pConfigPrefix);
    unsigned controlEscape = config.getLong("CtrlEscape", 0x7D, pConfigPrefix);

    // New HDLC
    _pHDLC = new MiniHDLC(NULL, 
            std::bind(&ProtocolRICSerial::hdlcFrameRxCB, this, std::placeholders::_1, std::placeholders::_2),
            frameBoundary,
            controlEscape,
            _maxTxMsgLen, _maxRxMsgLen);

    // Debug
    LOG_I(MODULE_PREFIX, "constructor channelID %d maxRxMsgLen %d maxTxMsgLen %d frameBoundary %02x controlEscape %02x", 
            (int)_channelID, (int)_maxRxMsgLen, (int)_maxTxMsgLen, frameBoundary, controlEscape);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ProtocolRICSerial::~ProtocolRICSerial()
{
    // Clean up
    if (_pHDLC)
        delete _pHDLC;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// addRxData
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ProtocolRICSerial::addRxData(const uint8_t* pData, uint32_t dataLen)
{
#ifdef DEBUG_PROTOCOL_RIC_SERIAL_DECODE_IN
    // Debug
    _debugNumBytesRx += dataLen;
    if (Raft::isTimeout(millis(), _debugLastInReportMs, 100))
    {
        LOG_I(MODULE_PREFIX, "addRxData len %d", _debugNumBytesRx);
        _debugNumBytesRx = 0;
        _debugLastInReportMs = millis();
    }
#endif
#ifdef DEBUG_PROTOCOL_RIC_SERIAL_DECODE_IN_DETAIL
    String dataStr;
    Raft::getHexStrFromBytes(pData, dataLen, dataStr);
    LOG_I(MODULE_PREFIX, "addRxData %s", dataStr.c_str());
#endif

    // Valid?
    if (!_pHDLC)
    {
#ifdef WARN_ON_NO_HDLC_HANDLER
        LOG_W(MODULE_PREFIX, "addRxData no HDLC handler");
#endif
        return;
    }

    // Add data
    _pHDLC->handleBuffer(pData, dataLen);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// addRxData
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// #define USE_HDLC_BYTEWISE_SEND

void ProtocolRICSerial::encodeTxMsgAndSend(CommsChannelMsg& msg)
{
#ifdef DEBUG_PROTOCOL_RIC_SERIAL_ENCODE
    LOG_I(MODULE_PREFIX, "encodeTxMsgAndSend msgNum %d msgType %d protocol %d bufLen %d", 
                msg.getMsgNumber(), msg.getMsgTypeCode(), msg.getProtocol(), msg.getBufLen());
#endif
    // Add to HDLC
    if (_pHDLC)
    {
#ifdef USE_HDLC_BYTEWISE_SEND
        CommsChannelMsg& encodedMsg = msg;
        // Create the message
        std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> ricSerialMsg;
        ricSerialMsg.reserve(msg.getBufLen()+2);
        ricSerialMsg.push_back(msg.getMsgNumber());
        uint8_t protocolDirnByte = ((msg.getMsgTypeCode() & 0x03) << 6) + (msg.getProtocol() & 0x3f);
        ricSerialMsg.push_back(protocolDirnByte);
        ricSerialMsg.insert(ricSerialMsg.end(), msg.getCmdVector().begin(), msg.getCmdVector().end());
        _pHDLC->sendFrame(ricSerialMsg.data(), ricSerialMsg.size());
        msg.setFromBuffer(_pHDLC->getFrameTxBuf(), _pHDLC->getFrameTxLen());
        _pHDLC->clearTxBuf();
#else
        // Form the header
        uint8_t protocolByte = ((msg.getMsgTypeCode() & 0x03) << 6) + (msg.getProtocol() & 0x3f); 
        uint8_t ricSerialRec[] = {
            (uint8_t) msg.getMsgNumber(),
            protocolByte
        };

        // Get the exact size of encoded payload
        uint32_t encodedTotalLen = _pHDLC->calcEncodedPayloadLen(ricSerialRec, sizeof(ricSerialRec));
        LOG_I(MODULE_PREFIX, "encodeTxMsgAndSend encodedTotalLen %d", encodedTotalLen);
        encodedTotalLen += _pHDLC->calcEncodedPayloadLen(msg.getBuf(), msg.getBufLen());
        LOG_I(MODULE_PREFIX, "encodeTxMsgAndSend encodedTotalLen %d bufLen %d", encodedTotalLen, msg.getBufLen());
        encodedTotalLen += MiniHDLC::HDLC_OVERHEAD_BYTES;
        LOG_I(MODULE_PREFIX, "encodeTxMsgAndSend maxEncodedLen %d bufLen %d", encodedTotalLen, msg.getBufLen());

        // Create encoded message obtaining channel, etc from original message
        CommsChannelMsg encodedMsg(msg.getChannelID(), msg.getProtocol(), 
                        msg.getMsgNumber(), msg.getMsgTypeCode());
        encodedMsg.setBufferSize(encodedTotalLen);

        // Build the encoded message in parts
        uint8_t* pEncBuf = encodedMsg.getCmdVector().data();
        uint16_t fcs = 0;
        uint32_t curPos = _pHDLC->encodeFrameStart(pEncBuf, encodedMsg.getBufLen(), fcs);
        curPos = _pHDLC->encodeFrameAddPayload(pEncBuf, encodedMsg.getBufLen(), fcs, curPos, ricSerialRec, sizeof(ricSerialRec));
        curPos = _pHDLC->encodeFrameAddPayload(pEncBuf, encodedMsg.getBufLen(), fcs, curPos, msg.getBuf(), msg.getBufLen());
        curPos = _pHDLC->encodeFrameEnd(pEncBuf, encodedMsg.getBufLen(), fcs, curPos);
        // Check correct length
#ifdef WARN_ON_ENCODED_MSG_LEN_MISMATCH
        if (curPos != encodedTotalLen)
        {
            LOG_W(MODULE_PREFIX, "encodeTxMsgAndSend len mismatch %d != %d", curPos, encodedTotalLen);
        }
#endif
#endif

        // Debug
#ifdef DEBUG_PROTOCOL_RIC_SERIAL_ENCODE
        LOG_I(MODULE_PREFIX, "encodeTxMsgAndSend encoded len %d", encodedMsg.getBufLen());
#endif
#ifdef DEBUG_PROTOCOL_RIC_SERIAL_ENCODE_DETAIL
        {
        String outStr;
        Raft::getHexStrFromBytes(encodedMsg.getBuf(), encodedMsg.getBufLen(), outStr);
        LOG_I(MODULE_PREFIX, "encodeTxMsgAndSend %s", outStr.c_str());
        }
#endif

        // Send
        _msgTxCB(encodedMsg);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle a frame received from HDLC
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ProtocolRICSerial::hdlcFrameRxCB(const uint8_t* pFrame, int frameLen)
{
    // Check callback is valid
    if (!_msgRxCB)
        return;

    // Convert to CommsChannelMsg
    CommsChannelMsg endpointMsg;
    if (!decodeIntoCommsChannelMsg(_channelID, pFrame, frameLen, endpointMsg))
        return;

    // Send to callback
    _msgRxCB(endpointMsg);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Decode a frame into a CommsChannelMsg
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ProtocolRICSerial::decodeIntoCommsChannelMsg(uint32_t channelID, const uint8_t* pFrame, int frameLen, CommsChannelMsg& msg)
{
    // Check validity of frame length
    if (frameLen < 2)
        return false;

    // Extract message type
    uint32_t msgNumber = pFrame[0];
    uint32_t msgProtocolCode = pFrame[1] & 0x3f;
    uint32_t msgTypeCode = pFrame[1] >> 6;

    // Debug
#ifdef DEBUG_PROTOCOL_RIC_SERIAL_DECODE_FRAME
    LOG_I(MODULE_PREFIX, "hdlcFrameRxCB chanID %d len %d msgNum %d protocolCode %d msgTypeCode %d", 
                    (int)channelID, frameLen, (int)msgNumber, (int)msgProtocolCode, (int)msgTypeCode);
#endif
#ifdef DEBUG_PROTOCOL_RIC_SERIAL_DECODE_FRAME_DETAIL
    String dataStr;
    Raft::getHexStrFromBytes(pFrame, frameLen, dataStr);
    LOG_I(MODULE_PREFIX, "hdleFrameRxCB %s", dataStr.c_str());
#endif

    // Convert to CommsChannelMsg
    msg.setFromBuffer(channelID, (CommsMsgProtocol)msgProtocolCode, msgNumber, (CommsMsgTypeCode)msgTypeCode, pFrame+2, frameLen-2);
    return true;
}