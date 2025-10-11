/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// File Download OKTO Protocol
// Rob Dobson 2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftJson.h"
#include "FileDownloadOKTOProtocol.h"
#include "RICRESTMsg.h"
#include "FileSystem.h"
#include "CommsChannelMsg.h"
#include "CommsCoreIF.h"
#include "FileStreamBlockOwned.h"

#define WARN_ON_TRANSFER_CANCEL

// #define DEBUG_SHOW_FILE_DOWNLOAD_PROGRESS
// #define DEBUG_RICREST_FILEDOWNLOAD
// #define DEBUG_RICREST_FILEDOWNLOAD_FIRST_AND_LAST_BLOCK
// #define DEBUG_FILE_STREAM_BLOCK_DETAIL
// #define DEBUG_RICREST_FILEDOWNLOAD_BLOCK_ACK
// #define DEBUG_RICREST_FILEDOWNLOAD_BLOCK_ACK_DETAIL
// #define DEBUG_RICREST_HANDLE_CMD_FRAME
// #define DEBUG_SEND_FILE_BLOCK

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileDownloadOKTOProtocol::FileDownloadOKTOProtocol(FileStreamBlockWriteFnType fileBlockWrite, 
            FileStreamBlockReadFnType fileBlockRead,
            FileStreamGetCRCFnType fileGetCRC,
            FileStreamCancelEndFnType fileCancelEnd,
            CommsCoreIF* pCommsCoreIF,
            FileStreamBase::FileStreamContentType fileStreamContentType, 
            FileStreamBase::FileStreamFlowType fileStreamFlowType,
            uint32_t streamID,
            uint32_t fileStreamLength,
            const char* fileStreamName) :
    FileStreamBase(fileBlockWrite, fileBlockRead, fileGetCRC, fileCancelEnd, 
            pCommsCoreIF, 
            fileStreamContentType, fileStreamFlowType, streamID, 
            fileStreamLength, fileStreamName)
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle command frame
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileDownloadOKTOProtocol::handleCmdFrame(FileStreamBase::FileStreamMsgType fsMsgType, 
                const RICRESTMsg& ricRESTReqMsg, String& respMsg, 
                const CommsChannelMsg &endpointMsg)
{
#ifdef DEBUG_RICREST_HANDLE_CMD_FRAME
    LOG_I(MODULE_PREFIX, "handleCmdFrame req %s", ricRESTReqMsg.getReq().c_str());
#endif
    // Handle message
    switch (fsMsgType)
    {
        case FILE_STREAM_MSG_TYPE_DOWNLOAD_START:
            return handleStartMsg(ricRESTReqMsg, respMsg, endpointMsg.getChannelID());
        case FILE_STREAM_MSG_TYPE_DOWNLOAD_END:
            return handleEndMsg(ricRESTReqMsg, respMsg);
        case FILE_STREAM_MSG_TYPE_DOWNLOAD_CANCEL:
            return handleCancelMsg(ricRESTReqMsg, respMsg);
        case FILE_STREAM_MSG_TYPE_DOWNLOAD_ACK:
            return handleAckMsg(ricRESTReqMsg, respMsg);
        default:
            return RaftRetCode::RAFT_INVALID_OPERATION;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle data frame (file/stream block)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileDownloadOKTOProtocol::handleDataFrame(const RICRESTMsg& ricRESTReqMsg, String& respMsg)
{
    // Shouldn't be receiving file blocks
    LOG_W(MODULE_PREFIX, "handleDataFrame unexpected");
    return RaftRetCode::RAFT_INVALID_OPERATION;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileDownloadOKTOProtocol::loop()
{
#ifdef DEBUG_SHOW_FILE_DOWNLOAD_PROGRESS
    // Stats display
    if (debugStatsReady())
    {
        LOG_I(MODULE_PREFIX, "fileDownloadStats %s", debugStatsStr().c_str());
    }
#endif
    // Check active
    if (!_isDownloading)
        return;

    // Service state-machine
    transferService();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get debug str
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String FileDownloadOKTOProtocol::getDebugJSON(bool includeBraces)
{
    if (includeBraces)
        return "{" + debugStatsStr() + "}";
    return debugStatsStr();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File transfer start
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileDownloadOKTOProtocol::handleStartMsg(const RICRESTMsg& ricRESTReqMsg, String& respMsg, 
            uint32_t channelID)
{
    // Get params
    RaftJson cmdFrame = ricRESTReqMsg.getPayloadJson();
    String fileName = cmdFrame.getString("fileName", "");
    String fileType = cmdFrame.getString("fileType", "");
    int blockSizeFromHost = cmdFrame.getLong("batchMsgSize", -1);
    int batchAckSizeFromHost = cmdFrame.getLong("batchAckSize", -1);

    // Validate the start message
    String errorMsg;
    uint32_t crc16 = 0;
    bool crc16Valid = false;
    bool startOk = validateFileStreamStart(fileName, channelID, errorMsg, crc16, crc16Valid, _fileSize);
    
    // Check ok
    if (startOk)
    {
        // Set up file stream length
        _fileStreamLength = _fileSize;
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
            chanBlockMax = _pCommsCore->outboundMsgBlockMax(channelID, FILE_BLOCK_SIZE_DEFAULT);
            _blockSize = Raft::clamp(_blockSize, FILE_BLOCK_SIZE_MIN, chanBlockMax > 0 ? chanBlockMax : _blockSize);
            LOG_I(MODULE_PREFIX, "handleStartMsg chanBlockMax %d blockSize %d", chanBlockMax, _blockSize);
        }

#ifdef DEBUG_RICREST_HANDLE_CMD_FRAME
        LOG_I(MODULE_PREFIX, "handleStartMsg filename %s fileLen %d fileType %s streamID %d errorMsg %s", 
                    _fileName.c_str(), 
                    _fileSize,
                    fileType.c_str(),
                    _streamID,
                    errorMsg.c_str());
        LOG_I(MODULE_PREFIX, "handleStartMsg blockSize %d chanBlockMax %d defaultBlockSize %d batchAckSize %d defaultBatchAckSize %d crc16 %04x crc16Valid %d",
                    _blockSize,
                    chanBlockMax,
                    FILE_BLOCK_SIZE_DEFAULT,
                    _batchAckSize,
                    BATCH_ACK_SIZE_DEFAULT,
                    crc16,
                    crc16Valid);
#endif
    }
    else
    {
        LOG_W(MODULE_PREFIX, "handleStartMsg FAIL streamID %d errorMsg %s", 
                    _streamID,
                    errorMsg.c_str());
    }
    // Response
    char extraJson[100];
    snprintf(extraJson, sizeof(extraJson), R"("batchMsgSize":%d,"batchAckSize":%d,"streamID":%d,"fileLen":%d)", 
                (int)_blockSize, (int)_batchAckSize, (int)_streamID, (int)_fileSize);
    String extraJsonStr = extraJson;
    if (crc16Valid)
    {
        char extraJsonCRC[20];
        snprintf(extraJsonCRC, sizeof(extraJsonCRC), R"(,"crc16":"%04x")", (int)crc16);
        extraJsonStr += extraJsonCRC;
    }
    Raft::setJsonResult(ricRESTReqMsg.getReq().c_str(), respMsg, startOk, errorMsg.c_str(), extraJsonStr.c_str());
    return RaftRetCode::RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File transfer ended normally
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileDownloadOKTOProtocol::handleEndMsg(const RICRESTMsg& ricRESTReqMsg, String& respMsg)
{
    // Handle file end
#ifdef DEBUG_RICREST_FILEDOWNLOAD
    RaftJson cmdFrame = ricRESTReqMsg.getPayloadJson();
    uint32_t blocksSent = cmdFrame.getLong("blockCount", 0);
#endif

    // Callback to indicate end of activity
    if (_fileStreamCancelEnd)
        _fileStreamCancelEnd(true);

    // Response
    Raft::setJsonBoolResult(ricRESTReqMsg.getReq().c_str(), respMsg, true);

    // Debug
#ifdef DEBUG_RICREST_FILEDOWNLOAD
    String fileName = cmdFrame.getString("fileName", "");
    String fileType = cmdFrame.getString("fileType", "");
    uint32_t fileLen = cmdFrame.getLong("fileLen", 0);
    LOG_I(MODULE_PREFIX, "handleEndMsg fileName %s fileType %s fileLen %d blocksSent %d", 
                fileName.c_str(), 
                fileType.c_str(), 
                fileLen, 
                blocksSent);
#endif

    // End
    transferEnd();
    return RaftRetCode::RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cancel file transfer
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileDownloadOKTOProtocol::handleCancelMsg(const RICRESTMsg& ricRESTReqMsg, String& respMsg)
{
    // Handle file cancel
    RaftJson cmdFrame = ricRESTReqMsg.getPayloadJson();
    String fileName = cmdFrame.getString("fileName", "");
    String reason = cmdFrame.getString("reason", "");

    // Cancel
    transferCancel(nullptr);

    // Response
    Raft::setJsonBoolResult(ricRESTReqMsg.getReq().c_str(), respMsg, true);

    // Debug
    LOG_I(MODULE_PREFIX, "handleCancelMsg fileName %s reason %s", fileName.c_str(), reason.c_str());

    // Debug
#ifdef DEBUG_RICREST_FILEDOWNLOAD
    LOG_I(MODULE_PREFIX, "handleCancelMsg fileName %s", 
                fileName.c_str());
#endif
    
    return RaftRetCode::RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle ack message
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileDownloadOKTOProtocol::handleAckMsg(const RICRESTMsg& ricRESTReqMsg, String& respMsg)
{
    // Handle ack
    RaftJson cmdFrame = ricRESTReqMsg.getPayloadJson();
    uint32_t oktoFilePos = cmdFrame.getLong("okto", 0);
    if ((oktoFilePos != 0) && (oktoFilePos > _oktoFilePos))
    {
        uint32_t newBytesReceived = oktoFilePos - _oktoFilePos;
        _bytesInWindow += newBytesReceived;
        _blocksInWindow += newBytesReceived / (_blockSize == 0 ? 1 : _blockSize);
        _oktoFilePos = oktoFilePos;
        _lastBatchAckRxOrRetryMs = millis();
        _blockCount = oktoFilePos / (_blockSize == 0 ? 1 : _blockSize);
        _bytesCount = oktoFilePos;
        _lastMsgMs = millis();

#ifdef DEBUG_RICREST_FILEDOWNLOAD_BLOCK_ACK
        LOG_I(MODULE_PREFIX, "handleAckMsg okto %d", oktoFilePos);
#endif
    }
    else
    {
        LOG_I(MODULE_PREFIX, "handleAckMsg no progress: okto %d prevOkto %d", 
                    oktoFilePos, _oktoFilePos);
    }

    return RaftRetCode::RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// State machine start
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileDownloadOKTOProtocol::validateFileStreamStart(const String& fileName,
        uint32_t channelID, String& respInfo, uint32_t& crc16, bool& crc16Valid, uint32_t& fileSize)
{
    // Check if already in progress
    if (_isDownloading && (_oktoFilePos > 0))
    {
        respInfo = "dowloadInProgress";
        return false;
    }
    
    // File params
    _fileName = fileName;
    _commsChannelID = channelID;

    // Get file CRC and length
    crc16Valid = false;
    if (_fileStreamGetCRC)
    {
        RaftRetCode retc = _fileStreamGetCRC(crc16, fileSize);
        if (retc == RaftRetCode::RAFT_OK)
            crc16Valid = true;
    }

    // Status
    _isDownloading = true;
    
    // Timing
    _startMs = millis();
    _lastMsgMs = millis();

    // Stats
    _blockCount = 0;
    _bytesCount = 0;
    _blocksInWindow = 0;
    _bytesInWindow = 0;
    _statsWindowStartMs = millis();
    _fileDownloadStartMs = millis();

    // Debug
    _debugLastStatsMs = millis();
    _debugFinalMsgToSend = false;

    // Batch handling
    _oktoFilePos = 0;
    _lastSentUptoFilePos = 0;
    _batchBlockSendRetryCount = 0;

    // Timing of blocks
    _lastBatchAckRxOrRetryMs = millis();

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// State machine service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileDownloadOKTOProtocol::transferService()
{
    // Check valid
    if (!_isDownloading || !_fileStreamBlockRead)
        return;
    
    // Check min time between blocks
    unsigned long nowMillis = millis();
    if (!Raft::isTimeout(millis(), _betweenBlocksMs, MIN_TIME_BETWEEN_BLOCKS_MS))
        return;
    _betweenBlocksMs = millis();

    // Check if we're finished
    if (_oktoFilePos >= _fileSize)
    {
#ifdef DEBUG_RICREST_FILEDOWNLOAD
        LOG_I(MODULE_PREFIX, "transferService finished successfully");
#endif        
        transferEnd();
        return;
    }

    // Check for overall time-out
    if (Raft::isTimeout(nowMillis, _startMs, DOWNLOAD_FAIL_TIMEOUT_MS))
    {
        LOG_W(MODULE_PREFIX, "transferService overall time-out startMs %d nowMs %d maxMs %d",
                    _startMs, (int)nowMillis, DOWNLOAD_FAIL_TIMEOUT_MS);
        transferCancel("failTimeout");
    }

    // Check for batch ack time-out
    if (Raft::isTimeout(nowMillis, _lastBatchAckRxOrRetryMs, BLOCK_MSGS_TIMEOUT_MS))
    {
        // Debug
        LOG_I(MODULE_PREFIX, "transferService batch ack time-out lastMsgMs %d nowMs %d maxMs %d",
                    _lastMsgMs, (int)nowMillis, BLOCK_MSGS_TIMEOUT_MS);

        // Check for retry
        if (_batchBlockSendRetryCount < MAX_BATCH_BLOCK_ACK_RETRIES)
        {
            // Retry
            _batchBlockSendRetryCount++;

            // Debug
            LOG_I(MODULE_PREFIX, "transferService batch ack time-out retry %d", _batchBlockSendRetryCount);

            // Go back to last okto position
            _lastSentUptoFilePos = _oktoFilePos;

            // Update timing
            _lastBatchAckRxOrRetryMs = millis();
        }
        else
        {
            // Cancel after too many retries
            transferCancel("batchAckTimeout");
            return;
        }
    }

    uint32_t blocksOutstanding = (_lastSentUptoFilePos + (_blockSize-1) - _oktoFilePos) / _blockSize;
    if ((_lastSentUptoFilePos >= _fileSize) || (blocksOutstanding >= _batchAckSize))
    {
#ifdef DEBUG_RICREST_FILEDOWNLOAD_BLOCK_ACK
        LOG_I(MODULE_PREFIX, "transferService waiting for batch ack - okto %d lastSentUpto %d blocksOutstanding %d fileSize %d", 
                    _oktoFilePos, _lastSentUptoFilePos, blocksOutstanding, _fileSize);
#endif
        return;
    }

    // Send a block
    FileStreamBlockOwned block;
    RaftRetCode retc = _fileStreamBlockRead(block, _lastSentUptoFilePos, _blockSize);
    if (retc == RaftRetCode::RAFT_OK)
    {
        // Send block
        sendBlock(block);

        // Update last sent pos
        _lastSentUptoFilePos = block.getFilePos() + block.getBlockLen();
    }
    else
    {
        // Cancel
        transferCancel("readError");
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cancel transfer
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileDownloadOKTOProtocol::transferCancel(const char* reasonStr)
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
        snprintf(tmpStr, sizeof(tmpStr), R"("cmdName":"dfCancel","reason":"%s")", reasonStr);
        Raft::setJsonBoolResult("", cancelMsg, true, tmpStr);

        // Send status message
        RICRESTMsg ricRESTRespMsg;
        CommsChannelMsg endpointMsg;
        ricRESTRespMsg.encode(cancelMsg, endpointMsg, RICRESTMsg::RICREST_ELEM_CODE_CMDRESPJSON);
        endpointMsg.setAsResponse(_commsChannelID, MSG_PROTOCOL_RICREST, 
                    0, MSG_TYPE_RESPONSE);

        // Debug
#ifdef WARN_ON_TRANSFER_CANCEL
        LOG_W(MODULE_PREFIX, "transferCancel dfCancel reason %s", reasonStr);
#endif

        // Send message on the appropriate channel
        if (_pCommsCore)
            _pCommsCore->outboundHandleMsg(endpointMsg);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Download end
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileDownloadOKTOProtocol::transferEnd()
{
    _isDownloading = false;
    _debugFinalMsgToSend = true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get OkTo
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t FileDownloadOKTOProtocol::getOkTo()
{
    return _oktoFilePos;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get block info
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

double FileDownloadOKTOProtocol::getBlockRate()
{
    uint32_t elapsedMs = millis() - _startMs;
    if (elapsedMs > 0)
        return 1000.0*_blockCount/elapsedMs;
    return 0;
}
bool FileDownloadOKTOProtocol::checkFinalBlock(uint32_t filePos, uint32_t blockLen)
{
    return filePos + blockLen >= _fileSize;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send block
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileDownloadOKTOProtocol::sendBlock(FileStreamBlockOwned& block)
{
    // Form block message
    CommsChannelMsg endpointMsg(_commsChannelID, MSG_PROTOCOL_RICREST, 0, MSG_TYPE_COMMAND);
    RICRESTMsg::encodeFileBlock(block.getFilePos(), 
                block.getBlockData(), block.getBlockLen(), 
                endpointMsg);

    // Send message
    if (_pCommsCore)
        _pCommsCore->outboundHandleMsg(endpointMsg);

#ifdef DEBUG_SEND_FILE_BLOCK
    // Debug
    LOG_I(MODULE_PREFIX, "sendBlock sent pos %d len %d", block.getFilePos(), block.getBlockLen());
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug and Stats
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FileDownloadOKTOProtocol::debugStatsReady()
{
    return _debugFinalMsgToSend || (_isDownloading && Raft::isTimeout(millis(), _debugLastStatsMs, DEBUG_STATS_MS));
}

String FileDownloadOKTOProtocol::debugStatsStr()
{
    char outStr[200];
    snprintf(outStr, sizeof(outStr), 
            R"("actv":%d,"msgRate":%.1f,"dataBps":%.1f,"bytes":%d,"blks":%d,"blkSize":%d,"strmID":%d,"name":"%s")",
            _isDownloading,
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

double FileDownloadOKTOProtocol::statsMsgRate()
{
    uint32_t winMs = millis() - _statsWindowStartMs;
    if (winMs == 0)
        return 0;
    return 1000.0 * _blocksInWindow / winMs;
}

double FileDownloadOKTOProtocol::statsDataRate()
{
    uint32_t winMs = millis() - _statsWindowStartMs;
    if (winMs == 0)
        return 0;
    return 1000.0 * _bytesInWindow / winMs;
}

double FileDownloadOKTOProtocol::statsFinalMsgRate()
{
    uint32_t winMs = _lastMsgMs - _startMs;
    if (winMs == 0)
        return 0;
    return 1000.0 * _blockCount / winMs;
}

double FileDownloadOKTOProtocol::statsFinalDataRate()
{
    uint32_t winMs = _lastMsgMs - _startMs;
    if (winMs == 0)
        return 0;
    return 1000.0 * _bytesCount / winMs;
}

void FileDownloadOKTOProtocol::statsEndWindow()
{
    _blocksInWindow = 0;
    _bytesInWindow = 0;
    _statsWindowStartMs = millis();
}

