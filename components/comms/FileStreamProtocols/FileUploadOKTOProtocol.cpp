/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// File Upload OKTO Protocol
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftJson.h"
#include "FileUploadOKTOProtocol.h"
#include "RICRESTMsg.h"
#include "FileSystem.h"
#include "CommsChannelMsg.h"
#include "CommsCoreIF.h"

// #define DEBUG_SHOW_FILE_UPLOAD_PROGRESS
// #define DEBUG_RICREST_FILEUPLOAD
// #define DEBUG_RICREST_FILEUPLOAD_FIRST_AND_LAST_BLOCK
// #define DEBUG_FILE_STREAM_BLOCK_DETAIL
// #define DEBUG_RICREST_FILEUPLOAD_BLOCK_ACK
// #define DEBUG_RICREST_FILEUPLOAD_BLOCK_ACK_DETAIL
// #define DEBUG_RICREST_HANDLE_CMD_FRAME

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileUploadOKTOProtocol::FileUploadOKTOProtocol(FileStreamBlockWriteFnType fileBlockWrite, 
            FileStreamBlockReadFnType fileBlockRead,
            FileStreamGetCRCFnType fileGetCRC,
            FileStreamCancelEndFnType fileCancelEnd,
            CommsCoreIF* pCommsCore,
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
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle command frame
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileUploadOKTOProtocol::handleCmdFrame(FileStreamBase::FileStreamMsgType fsMsgType,
                const RICRESTMsg& ricRESTReqMsg, String& respMsg, 
                const CommsChannelMsg &endpointMsg)
{
#ifdef DEBUG_RICREST_HANDLE_CMD_FRAME
    LOG_I(MODULE_PREFIX, "handleCmdFrame req %s", ricRESTReqMsg.getReq().c_str());
#endif

    // Handle message
    switch (fsMsgType)
    {
        case FILE_STREAM_MSG_TYPE_UPLOAD_START:
            return handleStartMsg(ricRESTReqMsg, respMsg, endpointMsg.getChannelID());
        case FILE_STREAM_MSG_TYPE_UPLOAD_END:
            return handleEndMsg(ricRESTReqMsg, respMsg);
        case FILE_STREAM_MSG_TYPE_UPLOAD_CANCEL:
            return handleCancelMsg(ricRESTReqMsg, respMsg);
        default:
            return RaftRetCode::RAFT_INVALID_OPERATION;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle data frame (file/stream block)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileUploadOKTOProtocol::handleDataFrame(const RICRESTMsg& ricRESTReqMsg, String& respMsg)
{
#ifdef DEBUG_FILE_STREAM_BLOCK_DETAIL
    LOG_I(MODULE_PREFIX, "handleDataFrame isUploading %d msgLen %d expectedPos %d", 
            _isUploading, ricRESTReqMsg.getBinLen(), _expectedFilePos);
#endif

    // Check if transfer has been cancelled
    if (!_isUploading)
    {
        LOG_W(MODULE_PREFIX, "handleFileBlock called when not transferring");
        transferCancel("failBlockUnexpected");
        return RAFT_NOT_XFERING;
    }

    // Handle the block
    uint32_t filePos = ricRESTReqMsg.getBufferPos();
    const uint8_t* pBuffer = ricRESTReqMsg.getBinBuf();
    uint32_t bufferLen = ricRESTReqMsg.getBinLen();

    // Validate received block and update state machine
    bool blockValid = false;
    bool isFinalBlock = false;
    bool isFirstBlock = false;
    bool genAck = false;
    validateRxBlock(filePos, bufferLen, blockValid, isFirstBlock, isFinalBlock, genAck);

    // Debug
    if (isFinalBlock)
    {
        LOG_I(MODULE_PREFIX, "handleFileBlock isFinal %d", isFinalBlock);
    }

#ifdef DEBUG_RICREST_FILEUPLOAD_FIRST_AND_LAST_BLOCK
    if (isFinalBlock || (_expectedFilePos == 0))
    {
        LOG_I(MODULE_PREFIX, "handleFileBlock %s", ricRESTReqMsg.debugMsg(100, true).c_str());
    }
#endif

    // Check if time to generate an ACK
    if (genAck)
    {
        // block returns true when an acknowledgement is required - so send that ack
        char ackJson[100];
        snprintf(ackJson, sizeof(ackJson), "\"okto\":%d", (int)getOkTo());
        Raft::setJsonBoolResult(ricRESTReqMsg.getReq().c_str(), respMsg, true, ackJson);

#ifdef DEBUG_RICREST_FILEUPLOAD_BLOCK_ACK
        LOG_I(MODULE_PREFIX, "handleFileBlock BatchOK Sending OkTo %d rxBlockFilePos %d len %d batchCount %d resp %s type %d", 
                getOkTo(), filePos, bufferLen, _batchBlockCount, respMsg.c_str(), _fileStreamContentType);
#endif
    }
    else
    {
        // Just another block - don't ack yet
#ifdef DEBUG_RICREST_FILEUPLOAD_BLOCK_ACK_DETAIL
        LOG_I(MODULE_PREFIX, "handleFileBlock filePos %d len %d batchCount %d resp %s heapSpace %d", 
                filePos, bufferLen, _batchBlockCount, respMsg.c_str(),
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#endif
    }

    // Check valid
    RaftRetCode rslt = RAFT_OK;
    if (blockValid && _fileStreamBlockWrite)
    {
        FileStreamBlock fileStreamBlock(_fileName.c_str(), 
                            _fileSize, 
                            filePos, 
                            pBuffer, 
                            bufferLen, 
                            isFinalBlock,
                            _expCRC16,
                            _expCRC16Valid,
                            _fileSize,
                            true,
                            isFirstBlock
                            );            

        // If this is the first block of a firmware update then there will be a long delay
        rslt = _fileStreamBlockWrite(fileStreamBlock);

        // Check result
        if (rslt != RAFT_OK)
        {
            if (_fileStreamContentType == FileUploadOKTOProtocol::FILE_STREAM_CONTENT_TYPE_FIRMWARE)
            {
                Raft::setJsonBoolResult(ricRESTReqMsg.getReq().c_str(), respMsg, false, R"("cmdName":"ufStatus","reason":"OTAWriteFailed")");
                if (isFirstBlock)
                    transferCancel("failOTAStart");
                else
                    transferCancel("failOTAWrite");
            }
            else
            {
                Raft::setJsonBoolResult(ricRESTReqMsg.getReq().c_str(), respMsg, false, R"("cmdName":"ufStatus","reason":"FileWriteFailed")");
                transferCancel("failFileWrite");
            }
        }
    }
    return rslt;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service file transfer
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileUploadOKTOProtocol::loop()
{
#ifdef DEBUG_SHOW_FILE_UPLOAD_PROGRESS
    // Stats display
    if (debugStatsReady())
    {
        LOG_I(MODULE_PREFIX, "fileUploadStats %s", debugStatsStr().c_str());
    }
#endif
    // Check active
    if (!_isUploading)
        return;
    
    // Handle transfer activity
    bool genBatchAck = false;
    transferService(genBatchAck);
    if (genBatchAck)
    {
        char ackJson[100];
        snprintf(ackJson, sizeof(ackJson), "\"okto\":%d", (int)getOkTo());
        String respMsg;
        Raft::setJsonBoolResult("ufBlock", respMsg, true, ackJson);

        // Send the response back
        CommsChannelMsg endpointMsg;
        RICRESTMsg::encode(respMsg, endpointMsg, RICRESTMsg::RICREST_ELEM_CODE_CMDRESPJSON);
        endpointMsg.setAsResponse(_commsChannelID, MSG_PROTOCOL_RICREST, 
                    0, MSG_TYPE_RESPONSE);

        // Debug
#ifdef DEBUG_RICREST_FILEUPLOAD
        LOG_I(MODULE_PREFIX, "loop Sending OkTo %d batchCount %d", 
                getOkTo(), _batchBlockCount);
#endif

        // Send message on the appropriate channel
        if (_pCommsCore)
            _pCommsCore->outboundHandleMsg(endpointMsg);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get debug str
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String FileUploadOKTOProtocol::getDebugJSON(bool includeBraces)
{
    if (includeBraces)
        return "{" + debugStatsStr() + "}";
    return debugStatsStr();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File transfer start
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileUploadOKTOProtocol::handleStartMsg(const RICRESTMsg& ricRESTReqMsg, String& respMsg, uint32_t channelID)
{
    // Get params
    RaftJson cmdFrame = ricRESTReqMsg.getPayloadJson();
    uint32_t fileLen = cmdFrame.getLong("fileLen", 0);
    String fileName = cmdFrame.getString("fileName", "");
    String fileType = cmdFrame.getString("fileType", "");
    String crc16Str = cmdFrame.getString("CRC16", "");
    int blockSizeFromHost = cmdFrame.getLong("batchMsgSize", -1);
    int batchAckSizeFromHost = cmdFrame.getLong("batchAckSize", -1);
    // CRC handling
    uint32_t crc16 = 0;
    bool crc16Valid = false;
    if (crc16Str.length() > 0)
    {
        crc16Valid = true;
        crc16 = strtoul(crc16Str.c_str(), nullptr, 0);
    }

    // Validate the start message
    String errorMsg;
    bool startOk = validateFileStreamStart(fileName, fileLen,  
                channelID, errorMsg, crc16, crc16Valid);

    // Check ok
    if (startOk)
    {
        // If we are sent batchMsgSize and/or batchAckSize then we use these values
        if ((blockSizeFromHost != -1) && (blockSizeFromHost > 0))
            _blockSize = blockSizeFromHost;
        else
            _blockSize = FILE_BLOCK_SIZE_DEFAULT;
        if ((batchAckSizeFromHost != -1) && (batchAckSizeFromHost > 0))
            _batchAckSize = batchAckSizeFromHost;
        else
            _batchAckSize = BATCH_ACK_SIZE_DEFAULT;

        // Check block sizes against channel maximum
        uint32_t chanBlockMax = 0;
        if (_pCommsCore)
        {
            chanBlockMax = _pCommsCore->inboundMsgBlockMax(channelID, FILE_BLOCK_SIZE_DEFAULT);
            _blockSize = Raft::clamp(_blockSize, FILE_BLOCK_SIZE_MIN, chanBlockMax > 0 ? chanBlockMax * 2 / 3 : _blockSize);
        }

        // Check maximum total bytes in batch
        uint32_t totalBytesInBatch = _blockSize * _batchAckSize;
        if (totalBytesInBatch > MAX_TOTAL_BYTES_IN_BATCH)
        {
            _batchAckSize = MAX_TOTAL_BYTES_IN_BATCH / _blockSize;
            if (_batchAckSize == 0)
                _batchAckSize = 1;
        }

#ifdef DEBUG_RICREST_HANDLE_CMD_FRAME
        LOG_I(MODULE_PREFIX, "handleStartMsg filename %s fileLen %d fileType %s streamID %d errorMsg %s", 
                    _fileName.c_str(), 
                    _fileSize,
                    fileType.c_str(),
                    _streamID,
                    errorMsg.c_str());
        LOG_I(MODULE_PREFIX, "handleStartMsg blockSize %d chanBlockMax %d defaultBlockSize %d batchAckSize %d defaultBatchAckSize %d crc16 %s crc16Valid %d",
                    _blockSize,
                    chanBlockMax,
                    FILE_BLOCK_SIZE_DEFAULT,
                    (int)_batchAckSize,
                    BATCH_ACK_SIZE_DEFAULT,
                    crc16Str.c_str(),
                    crc16Valid);
#endif
    }
    else
    {
        LOG_W(MODULE_PREFIX, "handleStartMsg FAIL streamID %d errorMsg %s", 
                    (int)_streamID,
                    errorMsg.c_str());
    }
    // Response
    char extraJson[100];
    snprintf(extraJson, sizeof(extraJson), R"("batchMsgSize":%d,"batchAckSize":%d,"streamID":%d)", 
                (int)_blockSize, (int)_batchAckSize, (int)_streamID);
    Raft::setJsonResult(ricRESTReqMsg.getReq().c_str(), respMsg, startOk, errorMsg.c_str(), extraJson);
    return RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File transfer ended normally
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode  FileUploadOKTOProtocol::handleEndMsg(const RICRESTMsg& ricRESTReqMsg, String& respMsg)
{
    // Extract params
    RaftJson cmdFrame = ricRESTReqMsg.getPayloadJson();

    // Handle file end
#ifdef DEBUG_RICREST_FILEUPLOAD
    uint32_t blocksSent = cmdFrame.getLong("blockCount", 0);
#endif

    // Callback to indicate end of activity
    if (_fileStreamCancelEnd)
        _fileStreamCancelEnd(true);

    // Response
    Raft::setJsonBoolResult(ricRESTReqMsg.getReq().c_str(), respMsg, true);

    // Debug
#ifdef DEBUG_RICREST_FILEUPLOAD
    String fileName = cmdFrame.getString("fileName", "");
    String fileType = cmdFrame.getString("fileType", "");
    uint32_t fileLen = cmdFrame.getLong("fileLen", 0);
    LOG_I(MODULE_PREFIX, "handleEndMsg fileName %s fileType %s fileLen %d blocksSent %d", 
                fileName.c_str(), 
                fileType.c_str(), 
                fileLen, 
                blocksSent);
#endif

    // End transfer
    transferEnd();
    return RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cancel file transfer
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode  FileUploadOKTOProtocol::handleCancelMsg(const RICRESTMsg& ricRESTReqMsg, String& respMsg)
{
    // Handle file cancel
    RaftJson cmdFrame = ricRESTReqMsg.getPayloadJson();
    String fileName = cmdFrame.getString("fileName", "");
    String reason = cmdFrame.getString("reason", "");

    // Cancel transfer
    transferCancel(reason.c_str());

    // Response
    Raft::setJsonBoolResult(ricRESTReqMsg.getReq().c_str(), respMsg, true);

    // Debug
    LOG_I(MODULE_PREFIX, "handleCancelMsg fileName %s", fileName.c_str());

    // Debug
#ifdef DEBUG_RICREST_FILEUPLOAD
    LOG_I(MODULE_PREFIX, "handleCancelMsg fileName %s", 
                fileName.c_str());
#endif
    
    return RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// State machine start
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileUploadOKTOProtocol::validateFileStreamStart(const String& fileName, uint32_t fileSize, 
        uint32_t channelID, String& respInfo, uint32_t crc16, bool crc16Valid)
{
    // Check if already in progress
    if (_isUploading && (_expectedFilePos > 0))
    {
        respInfo = "transferInProgress";
        return false;
    }
    

    // File params
    _fileName = fileName;
    _fileSize = fileSize;
    _commsChannelID = channelID;
    _expCRC16 = crc16;
    _expCRC16Valid = crc16Valid;

    // Status
    _isUploading = true;
    
    // Timing
    _startMs = millis();
    _lastMsgMs = millis();

    // Stats
    _blockCount = 0;
    _bytesCount = 0;
    _blocksInWindow = 0;
    _bytesInWindow = 0;
    _statsWindowStartMs = millis();
    _fileUploadStartMs = millis();

    // Debug
    _debugLastStatsMs = millis();
    _debugFinalMsgToSend = false;

    // Batch handling
    _expectedFilePos = 0;
    _batchBlockCount = 0;
    _batchBlockAckRetry = 0;
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// State machine service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileUploadOKTOProtocol::transferService(bool& genAck)
{
    unsigned long nowMillis = millis();
    genAck = false;
    // Check valid
    if (!_isUploading)
        return;
    
    // At the start of ESP firmware update there is a long delay (3s or so)
    // This occurs after reception of the first block
    // So need to ensure that there is a long enough timeout
    if (Raft::isTimeout(nowMillis, _lastMsgMs, _blockCount < 2 ? FIRST_MSG_TIMEOUT_MS : BLOCK_MSGS_TIMEOUT_MS))
    {
        _batchBlockAckRetry++;
        if (_batchBlockAckRetry < MAX_BATCH_BLOCK_ACK_RETRIES)
        {
            LOG_W(MODULE_PREFIX, "transferService blockMsgs timeOut - okto ack needed bytesRx %d lastOkTo %d lastMsgMs %d curMs %d blkCount %d blkSize %d batchSize %d retryCount %d",
                        _bytesCount, getOkTo(), _lastMsgMs, (int)nowMillis, _blockCount, _blockSize, _batchAckSize, _batchBlockAckRetry);
            _lastMsgMs = nowMillis;
            genAck = true;
            return;
        }
        else
        {
            LOG_W(MODULE_PREFIX, "transferService blockMsgs ack failed after retries");
            transferCancel("failRetries");
        }
    }

    // Check for overall time-out
    if (Raft::isTimeout(nowMillis, _startMs, UPLOAD_FAIL_TIMEOUT_MS))
    {
        LOG_W(MODULE_PREFIX, "transferService overall time-out startMs %d nowMs %d maxMs %d",
                    _startMs, (int)nowMillis, UPLOAD_FAIL_TIMEOUT_MS);
        transferCancel("failTimeout");
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Validate received block
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileUploadOKTOProtocol::validateRxBlock(uint32_t filePos, uint32_t blockLen, bool& blockValid, 
            bool& isFirstBlock, bool& isFinalBlock, bool& genAck)
{
    // Check active
    if (!_isUploading)
        return;

    // Returned vals
    blockValid = false;
    isFinalBlock = false;
    isFirstBlock = false;
    genAck = false;

    // Add to batch
    _batchBlockCount++;
    _lastMsgMs = millis();

    // Check
    if (filePos != _expectedFilePos)
    {
#ifdef DEBUG_RICREST_FILEUPLOAD_BLOCK_ACK
        LOG_W(MODULE_PREFIX, "validateRxBlock unexpected %d != %d, blockCount %d batchBlockCount %d, batchAckSize %d", 
                    filePos, _expectedFilePos, _blockCount, _batchBlockCount, _batchAckSize);
#endif
    }
    else
    {
        // Valid block so bump values
        blockValid = true;
        _expectedFilePos += blockLen;
        _blockCount++;
        _bytesCount += blockLen;
        _blocksInWindow++;
        _bytesInWindow += blockLen;
        isFirstBlock = filePos == 0;

        // Check if this is the final block
        isFinalBlock = checkFinalBlock(filePos, blockLen);

#ifdef DEBUG_RICREST_FILEUPLOAD_BLOCK_ACK
        LOG_I(MODULE_PREFIX, "validateRxBlock ok blockCount %d batchBlockCount %d, batchAckSize %d",
                    _blockCount, _batchBlockCount, _batchAckSize);
#endif
    }

    // Generate an ack on the first block received and on completion of each batch
    bool batchComplete = (_batchBlockCount == _batchAckSize) || (_blockCount == 1) || isFinalBlock;
    if (batchComplete)
        _batchBlockCount = 0;
    _batchBlockAckRetry = 0;
    genAck = batchComplete;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// State machine transfer cancel
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileUploadOKTOProtocol::transferCancel(const char* reasonStr)
{
    // End transfer state machine
    transferEnd();

    // Callback to indicate cancellation
    if (_fileStreamCancelEnd)
        _fileStreamCancelEnd(false);

    // Check if we need to send back a reason
    if (reasonStr != nullptr)
    {
        // Form cancel message
        String cancelMsg;
        char tmpStr[50];
        snprintf(tmpStr, sizeof(tmpStr), R"("cmdName":"ufCancel","reason":"%s")", reasonStr);
        Raft::setJsonBoolResult("", cancelMsg, true, tmpStr);

        // Send status message
        CommsChannelMsg endpointMsg;
        RICRESTMsg::encode(cancelMsg, endpointMsg, RICRESTMsg::RICREST_ELEM_CODE_CMDRESPJSON);
        endpointMsg.setAsResponse(_commsChannelID, MSG_PROTOCOL_RICREST, 
                    0, MSG_TYPE_RESPONSE);

        // Debug
#ifdef DEBUG_RICREST_FILEUPLOAD
        LOG_W(MODULE_PREFIX, "transferCancel ufCancel reason %s", reasonStr);
#endif

        // Send message on the appropriate channel
        if (_pCommsCore)
            _pCommsCore->outboundHandleMsg(endpointMsg);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Upload end
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileUploadOKTOProtocol::transferEnd()
{
    _isUploading = false;
    _debugFinalMsgToSend = true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get OkTo
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t FileUploadOKTOProtocol::getOkTo()
{
    return _expectedFilePos;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get block info
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

double FileUploadOKTOProtocol::getBlockRate()
{
    uint32_t elapsedMs = millis() - _startMs;
    if (elapsedMs > 0)
        return 1000.0*_blockCount/elapsedMs;
    return 0;
}
bool FileUploadOKTOProtocol::checkFinalBlock(uint32_t filePos, uint32_t blockLen)
{
    return filePos + blockLen >= _fileSize;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug and Stats
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileUploadOKTOProtocol::debugStatsReady()
{
    return _debugFinalMsgToSend || (_isUploading && Raft::isTimeout(millis(), _debugLastStatsMs, DEBUG_STATS_MS));
}

String FileUploadOKTOProtocol::debugStatsStr()
{
    char outStr[200];
    snprintf(outStr, sizeof(outStr), 
            R"("actv":%d,"msgRate":%.1f,"dataBps":%.1f,"bytes":%d,"blks":%d,"blkSize":%d,"strmID":%d,"name":"%s")",
            _isUploading,
            statsFinalMsgRate(), 
            statsFinalDataRate(), 
            (int)_bytesCount,
            (int)_blockCount, 
            (int)_blockSize,
            (int)_streamID,
            _fileName.c_str());
    statsEndWindow();
    _debugLastStatsMs = millis();
    _debugFinalMsgToSend = false;
    return outStr;
}

double FileUploadOKTOProtocol::statsMsgRate()
{
    uint32_t winMs = millis() - _statsWindowStartMs;
    if (winMs == 0)
        return 0;
    return 1000.0 * _blocksInWindow / winMs;
}

double FileUploadOKTOProtocol::statsDataRate()
{
    uint32_t winMs = millis() - _statsWindowStartMs;
    if (winMs == 0)
        return 0;
    return 1000.0 * _bytesInWindow / winMs;
}

double FileUploadOKTOProtocol::statsFinalMsgRate()
{
    uint32_t winMs = _lastMsgMs - _startMs;
    if (winMs == 0)
        return 0;
    return 1000.0 * _blockCount / winMs;
}

double FileUploadOKTOProtocol::statsFinalDataRate()
{
    uint32_t winMs = _lastMsgMs - _startMs;
    if (winMs == 0)
        return 0;
    return 1000.0 * _bytesCount / winMs;
}

void FileUploadOKTOProtocol::statsEndWindow()
{
    _blocksInWindow = 0;
    _bytesInWindow = 0;
    _statsWindowStartMs = millis();
}

