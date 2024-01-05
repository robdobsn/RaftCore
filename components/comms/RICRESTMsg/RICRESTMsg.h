/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RICRESTMsg
// Message encapsulation for REST message
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include "Logger.h"
#include "RaftArduino.h"
#include "SpiramAwareAllocator.h"

static const uint32_t RICREST_ELEM_CODE_POS = 0;
static const uint32_t RICREST_HEADER_PAYLOAD_POS = 1;
static const uint32_t RICREST_HEADER_MIN_MSG_LEN = 4;
static const uint32_t RICREST_BODY_BUFFER_POS = 1;
static const uint32_t RICREST_BODY_TOTAL_POS = 5;
static const uint32_t RICREST_BODY_PAYLOAD_POS = 9;
static const uint32_t RICREST_COMMAND_FRAME_PAYLOAD_POS = 1;
static const uint32_t RICREST_FILEBLOCK_CHANNEL_POS = 0;
static const uint32_t RICREST_FILEBLOCK_FILEPOS_POS = 1;
static const uint32_t RICREST_FILEBLOCK_FILEPOS_POS_BYTES = 4;
static const uint32_t RICREST_FILEBLOCK_PAYLOAD_POS = 5;

class CommsChannelMsg;

class RICRESTMsg
{
public:
    enum RICRESTElemCode
    {
        RICREST_ELEM_CODE_URL,
        RICREST_ELEM_CODE_CMDRESPJSON,
        RICREST_ELEM_CODE_BODY,
        RICREST_ELEM_CODE_COMMAND_FRAME,
        RICREST_ELEM_CODE_FILEBLOCK
    };

    static const char* getRICRESTElemCodeStr(RICRESTElemCode elemCode)
    {
        switch (elemCode)
        {
            case RICREST_ELEM_CODE_URL: return "URL";
            case RICREST_ELEM_CODE_CMDRESPJSON: return "CMDRESPJSON";
            case RICREST_ELEM_CODE_BODY: return "BODY";
            case RICREST_ELEM_CODE_COMMAND_FRAME: return "COMMAND_FRAME";
            case RICREST_ELEM_CODE_FILEBLOCK: return "FILEBLOCK";
            default: return "UNKNOWN";
        }
    }

    // Maximum REST message lengths
    static const uint32_t MAX_REST_BODY_SIZE_NO_PSRAM = 5000;
    static const uint32_t MAX_REST_BODY_SIZE_PSRAM = 200000;
    static uint32_t _maxRestBodySize;

    // Constructor
    RICRESTMsg();

    // Get maximum REST body size
    static uint32_t getMaxRestBodySize()
    {
        // Ensure max length has been determined
        computeMaxRestBodySize();
        return _maxRestBodySize;
    }

    // Decode
    bool decode(const uint8_t* pBuf, uint32_t len);

    // Encode
    static void encode(const String& payload, CommsChannelMsg& endpointMsg, RICRESTElemCode elemCode);
    static void encode(const uint8_t* pBuf, uint32_t len, CommsChannelMsg& endpointMsg, RICRESTElemCode elemCode);
    static void encodeFileBlock(uint32_t filePos, const uint8_t* pBuf, uint32_t len, 
                CommsChannelMsg& endpointMsg);

    const String& getReq() const
    {
        return _req;
    }
    const String& getPayloadJson() const
    {
        return _payloadJson;
    }
    const uint8_t* getBinBuf() const
    {
        return _binaryData.data();
    }
    uint32_t getBinLen() const
    {
        return _binaryData.size();
    }
    uint32_t getBufferPos() const
    {
        return _bufferPos;
    }
    uint32_t getStreamID() const
    {
        return _streamID;
    }
    uint32_t getTotalBytes() const
    {
        return _totalBytes;
    }
    RICRESTElemCode getElemCode() const
    {
        return _RICRESTElemCode;
    }
    void setElemCode(RICRESTElemCode elemCode)
    {
        _RICRESTElemCode = elemCode;
    }

    // Debug
    String debugMsg(uint32_t maxBytesLen, bool includePayload) const;
    static String debugResp(const CommsChannelMsg& resp, uint32_t maxBytesLen, bool includePayload);

private:
    // Debug binary
    String debugBinaryMsg(uint32_t maxBytesLen, bool includePayload) const;

    // RICRESTElemCode
    RICRESTElemCode _RICRESTElemCode = RICREST_ELEM_CODE_URL;

    // Parameters
    String _req;
    String _payloadJson;
    uint32_t _bufferPos = 0;
    uint32_t _totalBytes = 0;
    uint32_t _streamID = 0;
    std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> _binaryData;

    // Helpers
    static void computeMaxRestBodySize();
};
