/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RICRESTMsg
// Message encapsulation for REST message
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RICRESTMsg.h"
#include "CommsChannelMsg.h"
#include "RaftUtils.h"
#include "PlatformUtils.h"
#include "RaftJson.h"

// #define DEBUG_RICREST_MSG

uint32_t RICRESTMsg::_maxRestBodySize = 0;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RICRESTMsg::RICRESTMsg()
{
    // Check if max length has been determined
    computeMaxRestBodySize();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Decode
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RICRESTMsg::decode(const uint8_t* pBuf, uint32_t len)
{
    // Debug
#ifdef DEBUG_RICREST_MSG
    String decodeMsgHex;
    Raft::getHexStrFromBytes(pBuf, len, decodeMsgHex);
    LOG_I(MODULE_PREFIX, "decode payloadLen %d payload %s", len, decodeMsgHex.c_str());
#endif

    // Check there is a RESTElementCode
    if (len <= RICREST_ELEM_CODE_POS)
        return false;

    // Extract RESTElementCode
    _RICRESTElemCode = (RICRESTElemCode)pBuf[RICREST_ELEM_CODE_POS];

#ifdef DEBUG_RICREST_MSG
    LOG_I(MODULE_PREFIX, "decode elemCode %d", _RICRESTElemCode);
#endif

    switch(_RICRESTElemCode)
    {
        case RICREST_ELEM_CODE_URL:
        {
            // Check valid
            if (len <= RICREST_HEADER_PAYLOAD_POS)
                return false;

            // Set request
            uint32_t contentLen = len - RICREST_HEADER_PAYLOAD_POS;
            if (contentLen > _maxRestBodySize)
                contentLen = _maxRestBodySize;
            _req = String(pBuf+RICREST_HEADER_PAYLOAD_POS, contentLen);
            _binaryData.clear();

#ifdef DEBUG_RICREST_MSG
                // Debug
                LOG_I(MODULE_PREFIX, "decode URL req %s", _req.c_str());
#endif
            break;
        }
        case RICREST_ELEM_CODE_CMDRESPJSON:
        {
            // Check valid
            if (len <= RICREST_HEADER_PAYLOAD_POS)
                return false;

            // Set payload
            uint32_t contentLen = len - RICREST_HEADER_PAYLOAD_POS;
            if (contentLen > _maxRestBodySize)
                contentLen = _maxRestBodySize;
            _payloadJson = String(pBuf+RICREST_HEADER_PAYLOAD_POS, contentLen);
            _binaryData.clear();

#ifdef DEBUG_RICREST_MSG
            String debugStr;
            Raft::getHexStrFromBytes(pBuf, len, debugStr);
            LOG_I(MODULE_PREFIX, "decode CMDRESPJSON data %s len %d string form %s", debugStr.c_str(), len, _payloadJson.c_str());
#endif
            _req = RaftJson::getStringIm(_payloadJson.c_str(), _payloadJson.c_str()+_payloadJson.length(), "reqStr", "resp");
            break;
        }
        case RICREST_ELEM_CODE_BODY:
        {
            // Extract params
            const uint8_t* pData = pBuf + RICREST_BODY_BUFFER_POS;
            const uint8_t* pEndStop = pBuf + len;
            _bufferPos = Raft::getBEUInt32AndInc(pData, pEndStop);
            _totalBytes = Raft::getBEUInt32AndInc(pData, pEndStop);
            if (_totalBytes > _maxRestBodySize)
                _totalBytes = _maxRestBodySize;
            if (_bufferPos > _totalBytes)
                _bufferPos = 0;
            if (pData <= pEndStop)
                _binaryData.assign(pData, pEndStop);
            _req = "elemBody";
            break;
        }
        case RICREST_ELEM_CODE_COMMAND_FRAME:
        {
            // Check valid
            if (len <= RICREST_COMMAND_FRAME_PAYLOAD_POS)
                return false;

            // Find any null-terminator within payload
            int terminatorFoundIdx = -1;
            for (uint32_t i = RICREST_COMMAND_FRAME_PAYLOAD_POS; i < len; i++)
            {
                if (pBuf[i] == 0)
                {
                    terminatorFoundIdx = i;
                    break;
                }
            }

            // Check length
            uint32_t contentLen = terminatorFoundIdx < 0 ? len-RICREST_COMMAND_FRAME_PAYLOAD_POS : terminatorFoundIdx-RICREST_COMMAND_FRAME_PAYLOAD_POS;
            if (contentLen > _maxRestBodySize)
                contentLen = _maxRestBodySize;
            _payloadJson = String(pBuf+RICREST_COMMAND_FRAME_PAYLOAD_POS, contentLen);

            // Check for any binary element
            if ((terminatorFoundIdx >= 0) && ((int)len > terminatorFoundIdx + 1))
                _binaryData.assign(pBuf + terminatorFoundIdx + 1, pBuf + len - terminatorFoundIdx - 1);

#ifdef DEBUG_RICREST_MSG
            LOG_I(MODULE_PREFIX, "RICREST_CMD_FRAME json %s binaryLen %d", _payloadJson.c_str(), _binaryLen);
#endif
            _req = RaftJson::getStringIm(_payloadJson.c_str(), _payloadJson.c_str()+_payloadJson.length(), "cmdName", "unknown");
            break;
        }
        case RICREST_ELEM_CODE_FILEBLOCK:
        {
            // Extract params
            const uint8_t* pData = pBuf + RICREST_FILEBLOCK_FILEPOS_POS;
            const uint8_t* pEndStop = pBuf + len;
            uint32_t streamIDAndBufferPos = Raft::getBEUInt32AndInc(pData, pEndStop);
            _bufferPos = streamIDAndBufferPos & 0xffffff;
            _streamID = streamIDAndBufferPos >> 24;
            if (pData <= pEndStop)
                _binaryData.assign(pData, pEndStop);
            _req = "ufBlock";
            break;
        }
        default:
        {
            _binaryData.clear();
            return false;
        }
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Encode
////////////////////////////////////////////////////////////////////////////////////////////////////

void RICRESTMsg::encode(const String& payload, CommsChannelMsg& endpointMsg, RICRESTElemCode elemCode)
{
    // Setup buffer for the RESTElementCode
    uint8_t msgPrefixBuf[RICREST_HEADER_PAYLOAD_POS];
    msgPrefixBuf[RICREST_ELEM_CODE_POS] = elemCode;

    // Set the response ensuring to include the string terminator
    endpointMsg.setBufferSize(RICREST_HEADER_PAYLOAD_POS + payload.length() + 1);
    endpointMsg.setPartBuffer(RICREST_ELEM_CODE_POS, msgPrefixBuf, sizeof(msgPrefixBuf));
    endpointMsg.setPartBuffer(RICREST_HEADER_PAYLOAD_POS, (uint8_t*)payload.c_str(), payload.length() + 1);
}

void RICRESTMsg::encode(const uint8_t* pBuf, uint32_t len, CommsChannelMsg& endpointMsg, RICRESTElemCode elemCode)
{
    // Setup buffer for the RESTElementCode
    uint8_t msgPrefixBuf[RICREST_HEADER_PAYLOAD_POS];
    msgPrefixBuf[RICREST_ELEM_CODE_POS] = elemCode;

    // Set the response
    endpointMsg.setBufferSize(RICREST_HEADER_PAYLOAD_POS + len);
    endpointMsg.setPartBuffer(RICREST_ELEM_CODE_POS, msgPrefixBuf, sizeof(msgPrefixBuf));
    endpointMsg.setPartBuffer(RICREST_HEADER_PAYLOAD_POS, pBuf, len);
}

void RICRESTMsg::encodeFileBlock(uint32_t filePos, const uint8_t* pBuf, uint32_t len, 
            CommsChannelMsg& endpointMsg)
{
    // Setup buffer for the RESTElementCode
    uint8_t msgPrefixBuf[RICREST_HEADER_PAYLOAD_POS + RICREST_FILEBLOCK_FILEPOS_POS_BYTES];
    msgPrefixBuf[RICREST_ELEM_CODE_POS] = RICREST_ELEM_CODE_FILEBLOCK;
    Raft::setBEUInt32(msgPrefixBuf, RICREST_FILEBLOCK_FILEPOS_POS, filePos);

    // Set the response
    endpointMsg.setBufferSize(sizeof(msgPrefixBuf) + len);
    endpointMsg.setPartBuffer(RICREST_ELEM_CODE_POS, msgPrefixBuf, sizeof(msgPrefixBuf));
    endpointMsg.setPartBuffer(sizeof(msgPrefixBuf), pBuf, len);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug
////////////////////////////////////////////////////////////////////////////////////////////////////

String RICRESTMsg::debugBinaryMsg(uint32_t maxBytesLen, bool includePayload) const
{
    // Check if binary data
    if (!includePayload)
        return " binLen: 0";

    // Get binary debug str
    String binaryStr;
    uint32_t debugLen = _binaryData.size() > maxBytesLen ? maxBytesLen : _binaryData.size();
    Raft::getHexStrFromBytes(_binaryData.data(), debugLen, binaryStr);
    if (_binaryData.size() > maxBytesLen)
        binaryStr += String("...");
    return " binLen: " + String(_binaryData.size()) + " bin: " + binaryStr;
}

String RICRESTMsg::debugMsg(uint32_t maxBytesLen, bool includePayload) const
{
    String debugStr;
    switch (_RICRESTElemCode)
    {
        case RICREST_ELEM_CODE_URL:
        {
            debugStr = "req: " + _req;
            break;
        }
        case RICREST_ELEM_CODE_CMDRESPJSON:
        {
            debugStr = "req: " + _req;
            if (includePayload)
                debugStr += " json: " + _payloadJson;
            break;
        }
        case RICREST_ELEM_CODE_BODY:
        {
            debugStr = "req: " + _req + 
                       " bufPos:" + String(_bufferPos) + 
                       " totalBytes: " + String(_totalBytes) + 
                       debugBinaryMsg(maxBytesLen, includePayload);
            break;
        }
        case RICREST_ELEM_CODE_COMMAND_FRAME:
        {
            String jsonStr;
            if ((_payloadJson.length() > 0) && includePayload)
                jsonStr = " json: " + _payloadJson;
            debugStr = "req: " + _req + 
                       jsonStr + 
                       debugBinaryMsg(maxBytesLen, includePayload);
            break;
        }
        case RICREST_ELEM_CODE_FILEBLOCK:
        {
            debugStr = "req: " + _req + 
                       " streamID: " + String(_streamID) + 
                       " bufPos:" + String(_bufferPos) + 
                       " totalBytes: " + String(_totalBytes) + 
                       debugBinaryMsg(maxBytesLen, includePayload);
            break;
        }
        default:
        {
            break;
        }
    }
    if (debugStr.length() == 0)
        debugStr = "unknown RICRESTElemCode " + String(_RICRESTElemCode);
    return debugStr;
}

String RICRESTMsg::debugResp(const CommsChannelMsg& endpointMsg, uint32_t maxBytesLen, bool includePayload)
{

    String debugStr;
    if (includePayload)
    {
        if (endpointMsg.getBufLen() > 0)
        {
            if (endpointMsg.getBufLen() > 1)
            {
                uint32_t debugLen = endpointMsg.getBufLen()-1 > maxBytesLen ? maxBytesLen : endpointMsg.getBufLen()-1;
                debugStr = String(endpointMsg.getBuf()+1, debugLen);
                if (endpointMsg.getBufLen()-1 > maxBytesLen)
                    debugStr += String("...");
            }
            else
            {
                debugStr = "NONE";
            }
            RICRESTElemCode elemCode = (RICRESTElemCode) (endpointMsg.getBuf()[0]);
            debugStr = " elemCode: " + String(RICRESTMsg::getRICRESTElemCodeStr(elemCode)) + " json: " + debugStr;
        }
        else
        {
            debugStr = " TOO SHORT (len = 0)";
        }
    }
    return "resp: " + String(endpointMsg.getProtocolAsString(endpointMsg.getProtocol())) +
           " type: " + String(endpointMsg.getMsgTypeAsString(endpointMsg.getMsgTypeCode())) +
           " len: " + String(endpointMsg.getBufLen()) +
           " msgNum: " + String(endpointMsg.getMsgNumber()) +
           " channelId: " + String(endpointMsg.getChannelID()) +
            debugStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compute maximum REST body size
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RICRESTMsg::computeMaxRestBodySize()
{
    if (_maxRestBodySize == 0)
    {
        // Check if PSRAM is available
        bool isPSRAM = utilsGetSPIRAMSize() > 0;
        _maxRestBodySize = isPSRAM ? MAX_REST_BODY_SIZE_PSRAM : MAX_REST_BODY_SIZE_NO_PSRAM;
    }
}
