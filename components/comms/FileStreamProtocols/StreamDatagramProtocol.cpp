/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Stream Datagram Protocol
// Used for streaming audio, etc
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "StreamDatagramProtocol.h"
#include "Logger.h"
#include "RICRESTMsg.h"
#include "RaftUtils.h"
#include "FileStreamBlock.h"

// Debug
// #define DEBUG_STREAM_DATAGRAM_PROTOCOL
// #define DEBUG_STREAM_DATAGRAM_PROTOCOL_DETAIL

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

StreamDatagramProtocol::StreamDatagramProtocol(FileStreamBlockWriteFnType fileBlockWrite, 
            FileStreamBlockReadFnType fileBlockRead,
            FileStreamGetCRCFnType fileGetCRC,
            FileStreamCancelEndFnType fileCancelEnd,
            CommsCoreIF *pCommsCore,
            FileStreamBase::FileStreamContentType fileStreamContentType, 
            FileStreamBase::FileStreamFlowType fileStreamFlowType,
            uint32_t streamID,
            uint32_t fileStreamLength,
            const char* fileStreamName) :
    FileStreamBase(fileBlockWrite, fileBlockRead, fileGetCRC, fileCancelEnd, 
            pCommsCore,
            fileStreamContentType, fileStreamFlowType, 
            streamID, fileStreamLength, fileStreamName)
{
    _streamPos = 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void StreamDatagramProtocol::loop()
{
    // Nothing to do
}

void StreamDatagramProtocol::resetCounters(uint32_t fileStreamLength){
    _fileStreamLength = fileStreamLength;
    _streamPos = 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle command frame
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode StreamDatagramProtocol::handleCmdFrame(FileStreamBase::FileStreamMsgType fsMsgType,
                const RICRESTMsg& ricRESTReqMsg, String& respMsg, 
                const CommsChannelMsg &endpointMsg)
{
    // Response
    char extraJson[100];
    snprintf(extraJson, sizeof(extraJson), R"("streamID":%d)", (int)_streamID);
    Raft::setJsonResult(ricRESTReqMsg.getReq().c_str(), respMsg, true, nullptr, extraJson);

    // Debug
#ifdef DEBUG_STREAM_DATAGRAM_PROTOCOL
    LOG_I(MODULE_PREFIX, "handleCmdFrame req %s resp %s", ricRESTReqMsg.debugMsg().c_str(), respMsg.c_str());
#endif
    return RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle data frame (file/stream block)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode StreamDatagramProtocol::handleDataFrame(const RICRESTMsg& ricRESTReqMsg, String& respMsg)
{
    // Check valid CB
    if (!_fileStreamBlockWrite)
        return RAFT_INVALID_OBJECT;

    // Handle the upload block
    uint32_t filePos = ricRESTReqMsg.getBufferPos();
    const uint8_t* pBuffer = ricRESTReqMsg.getBinBuf();
    uint32_t bufferLen = ricRESTReqMsg.getBinLen();
    uint32_t streamID = ricRESTReqMsg.getStreamID();

#ifdef DEBUG_STREAM_DATAGRAM_PROTOCOL_DETAIL
    LOG_I(MODULE_PREFIX, "handleDataFrame %s dataLen %d msgPos %d expectedPos %d streamID %d", 
            _streamPos == filePos ? "OK" : "BAD STREAM POS",
            bufferLen, filePos, _streamPos, streamID);
#endif
#ifdef DEBUG_STREAM_DATAGRAM_PROTOCOL_DETAIL
    LOG_I(MODULE_PREFIX, "handleDataFrame %s", ricRESTReqMsg.debugMsg(MAX_DEBUG_BIN_HEX_LEN, true).c_str());
#endif

    // Process the frame
    RaftRetCode rslt = RAFT_POS_MISMATCH;
    bool isFinalBlock = (_fileStreamLength != 0) && (filePos + bufferLen >= _fileStreamLength);

    bool isFirstBlock = (filePos == 0) && !_continuingStream;
    _continuingStream = false;

    // Check position - we'll accept any packet that's ahead of the last added packet
    if (filePos >= _streamPos)
    {
        FileStreamBlock fileStreamBlock(_fileStreamName.c_str(), 
                            _fileStreamLength, 
                            filePos, 
                            pBuffer, 
                            bufferLen, 
                            isFinalBlock,
                            0,
                            false,
                            _fileStreamLength,
                            _fileStreamLength != 0,
                            isFirstBlock
                            );

        // Call the callback
        rslt = _fileStreamBlockWrite(fileStreamBlock);

        // Update stream position
        _streamPos = filePos + bufferLen;
    }

    // Check end of stream
    if (isFinalBlock)
    {
        // Send a SOKTO to indicate the end received
        char ackJson[100];
        snprintf(ackJson, sizeof(ackJson), "\"streamID\":%d,\"sokto\":%d", (int)streamID, (int)_streamPos);
        Raft::setJsonBoolResult(ricRESTReqMsg.getReq().c_str(), respMsg, true, ackJson);
#ifdef DEBUG_STREAM_DATAGRAM_PROTOCOL
        LOG_I(MODULE_PREFIX, "handleDataFrame: ENDRX streamID %d, streamPos %d, sokto %d", streamID, _streamPos, _streamPos);
#endif
    }

    // This is a better never than late stream where packets may be dropped, but still tell the central of any issues
    if ((rslt == RAFT_BUSY) || (rslt == RAFT_POS_MISMATCH))
    {
        // Send a SOKTO which indicates where the stream was received up to
        char ackJson[100];
        snprintf(ackJson, sizeof(ackJson), "\"streamID\":%d,\"sokto\":%d,\"reason\":\"%s\"", 
                                (int)streamID, (int)_streamPos,
                                Raft::getRetCodeStr(rslt));
        Raft::setJsonBoolResult(ricRESTReqMsg.getReq().c_str(), respMsg, true, ackJson);
#ifdef DEBUG_STREAM_DATAGRAM_PROTOCOL
        LOG_I(MODULE_PREFIX, "handleDataFrame: %s streamID %d streamPos %d sokto %d retc %s", 
                    rslt == RAFT_BUSY ? "BUSY" : "POS_MISMATCH", streamID, _streamPos, _streamPos,
                    Raft::getRetCodeStr(rslt));
#endif
    }
    else if (rslt != RAFT_OK)
    {
        // Failure of the stream
        char errorMsg[100];
        snprintf(errorMsg, sizeof(errorMsg), "\"streamID\":%d,\"reason\":\"%s\"", 
                    (int)streamID, Raft::getRetCodeStr(rslt));
        Raft::setJsonBoolResult(ricRESTReqMsg.getReq().c_str(), respMsg, false, errorMsg);
#ifdef DEBUG_STREAM_DATAGRAM_PROTOCOL
        LOG_I(MODULE_PREFIX, "handleDataFrame: FAIL streamID %d streamPos %d sokto %d retc %s", streamID, _streamPos, _streamPos,
                                Raft::getRetCodeStr(rslt));
#endif
    }

    // Result
    return rslt;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get debug str
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String StreamDatagramProtocol::getDebugJSON(bool includeBraces)
{
    return "{}";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Is active
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool StreamDatagramProtocol::isActive()
{
    return true;
}
