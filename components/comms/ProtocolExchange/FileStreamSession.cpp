/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// File Stream Session
//
// Rob Dobson 2021-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "FileStreamSession.h"
#include "RaftSysMod.h"
#include "FileUploadHTTPProtocol.h"
#include "FileUploadOKTOProtocol.h"
#include "FileDownloadOKTOProtocol.h"
#include "StreamDatagramProtocol.h"
#include "RestAPIEndpointManager.h"
#include "RICRESTMsg.h"
#include "MiniHDLC.h"
#include "SpiramAwareAllocator.h"

// Warn
#define WARN_ON_FW_UPDATE_FAILED
#define WARN_ON_STREAM_FAILED
#define WARN_ON_FILE_CHUNKER_START_FAILED

// Debug
// #define DEBUG_FILE_STREAM_START_END
// #define DEBUG_FILE_STREAM_BLOCK

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileStreamSession::FileStreamSession(const String& filename, uint32_t channelID,
                CommsCoreIF* pCommsCore, RaftSysMod* pFirmwareUpdater,
                FileStreamBase::FileStreamContentType fileStreamContentType, 
                FileStreamBase::FileStreamFlowType fileStreamFlowType,
                uint32_t streamID, const char* restAPIEndpointName,
                RestAPIEndpointManager* pRestAPIEndpointManager,
                uint32_t fileStreamLength) :
                _streamSourceInfo(channelID)
{
    _pFileStreamProtocolHandler = nullptr;
    _isActive = false;
    _sessionLastActiveMs = millis();
    _fileStreamName = filename;
    _channelID = channelID;
    _fileStreamContentType = fileStreamContentType;
    _fileStreamFlowType = fileStreamFlowType;
    _startTimeMs = millis();
    _totalWriteTimeUs = 0;
    _totalBytes = 0;
    _totalChunks = 0;
    _pFileChunker = nullptr;
    _pFirmwareUpdater = pFirmwareUpdater;
    _restAPIEndpointName = restAPIEndpointName;
    _pRestAPIEndpointManager = pRestAPIEndpointManager;
    _pStreamChunkCB = nullptr;
    _pStreamIsReadyCB = nullptr;

#ifdef DEBUG_FILE_STREAM_START_END
    LOG_I(MODULE_PREFIX, "constructor filename %s channelID %d streamID %d endpointName %s", 
                _fileStreamName.c_str(), _channelID, streamID, restAPIEndpointName);
#endif

    // For file handling use a FileSystemChunker to access the file
    if (_fileStreamContentType == FileStreamBase::FILE_STREAM_CONTENT_TYPE_FILE)
    {
        _pFileChunker = new FileSystemChunker();
        if (_pFileChunker)
        {
            _pFileChunker->start(filename, 0, false, FileStreamBase::isUploadFlowType(fileStreamFlowType), true, true);
        }
        if (!_pFileChunker || !_pFileChunker->isActive())
        {
#ifdef WARN_ON_FILE_CHUNKER_START_FAILED
            LOG_W(MODULE_PREFIX, "constructor failed to start file chunker");
#endif
            return;
        }
    }

    // Construct file/stream handling protocol
    switch(_fileStreamContentType)
    {
        case FileStreamBase::FILE_STREAM_CONTENT_TYPE_FILE:
        case FileStreamBase::FILE_STREAM_CONTENT_TYPE_FIRMWARE:
        {
            if (_fileStreamFlowType == FileStreamBase::FILE_STREAM_FLOW_TYPE_HTTP_UPLOAD)
            {
                _pFileStreamProtocolHandler = new FileUploadHTTPProtocol(
                            std::bind(&FileStreamSession::fileStreamBlockWrite, this, std::placeholders::_1),
                            std::bind(&FileStreamSession::fileStreamBlockRead, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                            std::bind(&FileStreamSession::fileStreamGetCRC, this, std::placeholders::_1, std::placeholders::_2),
                            std::bind(&FileStreamSession::fileStreamCancelEnd, this, std::placeholders::_1),
                            pCommsCore,
                            fileStreamContentType, 
                            fileStreamFlowType,
                            streamID,
                            fileStreamLength,
                            filename.c_str());
            }
            else if (_fileStreamFlowType == FileStreamBase::FILE_STREAM_FLOW_TYPE_RICREST_UPLOAD)
            {
                _pFileStreamProtocolHandler = new FileUploadOKTOProtocol(
                            std::bind(&FileStreamSession::fileStreamBlockWrite, this, std::placeholders::_1),
                            std::bind(&FileStreamSession::fileStreamBlockRead, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                            std::bind(&FileStreamSession::fileStreamGetCRC, this, std::placeholders::_1, std::placeholders::_2),
                            std::bind(&FileStreamSession::fileStreamCancelEnd, this, std::placeholders::_1),
                            pCommsCore,
                            fileStreamContentType, 
                            fileStreamFlowType,
                            streamID,
                            fileStreamLength,
                            filename.c_str());
            }
            else if (_fileStreamFlowType == FileStreamBase::FILE_STREAM_FLOW_TYPE_RICREST_DOWNLOAD)
            {
                fileStreamLength = _pFileChunker->getFileLen();
                _pFileStreamProtocolHandler = new FileDownloadOKTOProtocol(
                            std::bind(&FileStreamSession::fileStreamBlockWrite, this, std::placeholders::_1),
                            std::bind(&FileStreamSession::fileStreamBlockRead, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                            std::bind(&FileStreamSession::fileStreamGetCRC, this, std::placeholders::_1, std::placeholders::_2),
                            std::bind(&FileStreamSession::fileStreamCancelEnd, this, std::placeholders::_1),
                            pCommsCore,
                            fileStreamContentType, 
                            fileStreamFlowType,
                            streamID,
                            fileStreamLength,
                            filename.c_str());
            }
            break;
        }
        case FileStreamBase::FILE_STREAM_CONTENT_TYPE_RT_STREAM:
        {
            // Create protocol handler
            _pFileStreamProtocolHandler = new StreamDatagramProtocol(
                        std::bind(&FileStreamSession::fileStreamBlockWrite, this, std::placeholders::_1),
                        std::bind(&FileStreamSession::fileStreamBlockRead, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                            std::bind(&FileStreamSession::fileStreamGetCRC, this, std::placeholders::_1, std::placeholders::_2),
                        std::bind(&FileStreamSession::fileStreamCancelEnd, this, std::placeholders::_1),
                        pCommsCore,
                        fileStreamContentType, 
                        fileStreamFlowType,
                        streamID,
                        fileStreamLength,
                        filename.c_str());

            // Find endpoint
            RestAPIEndpoint* pRestAPIEndpoint = _pRestAPIEndpointManager->getEndpoint(_restAPIEndpointName.c_str());
            if (pRestAPIEndpoint && pRestAPIEndpoint->_callbackChunk)
            {
#ifdef DEBUG_FILE_STREAM_START_END
                LOG_I(MODULE_PREFIX, "constructor API %s filename %s channelID %d streamID %d endpointName %s", 
                            pRestAPIEndpoint->getEndpointName(), _fileStreamName.c_str(), _channelID, streamID, restAPIEndpointName);
#endif
                // Set endpoint callbacks
                _pStreamChunkCB = pRestAPIEndpoint->_callbackChunk;
                _pStreamIsReadyCB = pRestAPIEndpoint->_callbackIsReady;
            }

            break;
        }
        default:
        {
            // Not supported
            break;
        }
    }
    
    // Check active
    _isActive = _pFileStreamProtocolHandler != nullptr;

#ifdef DEBUG_FILE_STREAM_START_END
    LOG_I(MODULE_PREFIX, "constructor finished filename %s channelID %d streamID %d endpointName %s isActive %d contentType %d pSession %p", 
                _fileStreamName.c_str(), _channelID, streamID, restAPIEndpointName, _isActive, _fileStreamContentType, this);
#endif
}

FileStreamSession::~FileStreamSession()
{
    // Tidy up
    delete _pFileChunker;
    delete _pFileStreamProtocolHandler;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle command frame
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileStreamSession::loop()
{
    // Service file/stream protocol
    if (_pFileStreamProtocolHandler)
    {
        _pFileStreamProtocolHandler->loop();
        if (!_pFileStreamProtocolHandler->isActive())
        {
#ifdef DEBUG_FILE_STREAM_START_END
            LOG_I(MODULE_PREFIX, "loop handler-is-inactive filename %s channelID %d isActive %d pSession %p", 
                        _fileStreamName.c_str(), _channelID, _isActive, this);
#endif
            _isActive = false;
        }
    }

    // Check for timeouts
    if (_isActive && Raft::isTimeout(millis(), _sessionLastActiveMs, MAX_SESSION_IDLE_TIME_MS))
    {
#ifdef DEBUG_FILE_STREAM_START_END
        LOG_I(MODULE_PREFIX, "loop timeout filename %s channelID %d isActive %d", 
                    _fileStreamName.c_str(), _channelID, _isActive);
#endif
        _isActive = false;
    }
}

void FileStreamSession::resetCounters(uint32_t fileStreamLength){
    if (!_pFileStreamProtocolHandler) return;
        _pFileStreamProtocolHandler->resetCounters(fileStreamLength);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle command frame
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileStreamSession::handleCmdFrame(FileStreamBase::FileStreamMsgType fsMsgType, 
                const RICRESTMsg& ricRESTReqMsg, String& respMsg, 
                const CommsChannelMsg &endpointMsg)
{
    // Check handler exists
    if (!_pFileStreamProtocolHandler)
        return RAFT_INVALID_OBJECT;

    // Send to handler
    RaftRetCode rslt = _pFileStreamProtocolHandler->handleCmdFrame(fsMsgType, ricRESTReqMsg, respMsg, endpointMsg);

    // Session may now be finished
    if (!_pFileStreamProtocolHandler->isActive())
    {
        _isActive = false;
#ifdef DEBUG_FILE_STREAM_START_END
        LOG_I(MODULE_PREFIX, "handleCmdFrame handler inactive filename %s channelID %d isActive %d pSession %p", 
                    _fileStreamName.c_str(), _channelID, _isActive, this);
#endif
    }

    // Keep session alive while we're receiving
    _sessionLastActiveMs = millis();        
    return rslt;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle file/stream block message
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileStreamSession::handleDataFrame(const RICRESTMsg& ricRESTReqMsg, String& respMsg)
{
    if (!_pFileStreamProtocolHandler)
    {
        RaftRetCode rslt = RAFT_INVALID_OBJECT;
        char errorMsg[100];
        snprintf(errorMsg, sizeof(errorMsg), "\"reason\":\"%s\"", Raft::getRetCodeStr(rslt));
        Raft::setJsonBoolResult(ricRESTReqMsg.getReq().c_str(), respMsg, false, errorMsg);
        return rslt;
    }
    return _pFileStreamProtocolHandler->handleDataFrame(ricRESTReqMsg, respMsg);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// getDebugJSON
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String FileStreamSession::getDebugJSON() const
{
    // Build JSON
    if (_pFileStreamProtocolHandler)
        return _pFileStreamProtocolHandler->getDebugJSON(true);
    return "{}";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File/stream get CRC
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileStreamSession::fileStreamGetCRC(uint32_t& crc, uint32_t& fileLen)
{
    // Check that a file chunker is available and active
    if (!_pFileChunker || !_pFileChunker->isActive())
        return RaftRetCode::RAFT_NOT_XFERING;

    // Get CRC and file length
    fileLen = _pFileChunker->getFileLen();

    // Use chunker to get chunks and calculate CRC
    uint32_t crcValue = MiniHDLC::crcInitCCITT();

    // Reset chunker
    _pFileChunker->restart();

    // Get chunks and calculate CRC
    const uint32_t CRC_CHUNK_SIZE = SpiramAwareAllocator<uint8_t>::max_allocatable() > 500000 ? 2000 : 500;
    bool finalBlockRead = false;
    while (!finalBlockRead)
    {
        // Get next chunk
        auto chunk = _pFileChunker->nextRead(CRC_CHUNK_SIZE, finalBlockRead);

        // Check for error
        if (chunk.size() == 0)
            break;

        // Calculate CRC
        crcValue = MiniHDLC::crcUpdateCCITT(crcValue, chunk);
    }

    // Reset chunker again
    _pFileChunker->restart();

    // CRC value
    crc = crcValue;
    return RaftRetCode::RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File/stream block read
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileStreamSession::fileStreamBlockRead(FileStreamBlockOwned& fileStreamBlock,
            uint32_t filePos, uint32_t maxLen)
{
    // Check chunker is active
    if (!_pFileChunker || !_pFileChunker->isActive())
        return RaftRetCode::RAFT_NOT_XFERING;

    // Current file pos
    uint32_t curFilePos = _pFileChunker->getFilePos();

    // Check if at the required position
    if (curFilePos != filePos)
    {
        // Seek to required position
        if (!_pFileChunker->seek(filePos))
            return RaftRetCode::RAFT_NOT_XFERING;
    }

    // Get next chunk
    bool finalBlockRead = false;
    auto chunk = _pFileChunker->nextRead(maxLen, finalBlockRead);

    // Fill fileStreamBlock
    uint32_t fileLen = _pFileChunker->getFileLen();
    fileStreamBlock.set(_pFileChunker->getFileName().c_str(),
            fileLen,
            filePos,
            chunk.data(),
            chunk.size(),
            finalBlockRead,
            0, false,
            fileLen, true,
            filePos == 0);
    return chunk.size() != 0 ? RaftRetCode::RAFT_OK : RaftRetCode::RAFT_NOT_XFERING;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File/stream block write
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileStreamSession::fileStreamBlockWrite(FileStreamBlock& fileStreamBlock)
{
#ifdef DEBUG_FILE_STREAM_BLOCK
    {
        String lenStr = String(fileStreamBlock.blockLen);
        if (fileStreamBlock.fileLenValid)
            lenStr += " of " + String(fileStreamBlock.fileLen);
        LOG_I(MODULE_PREFIX, "fileStreamBlockWrite pos %d len %s fileStreamContentType %d isFirst %d isFinal %d name %s",
                fileStreamBlock.filePos, 
                lenStr.c_str(),
                _fileStreamContentType, 
                fileStreamBlock.firstBlock, 
                fileStreamBlock.finalBlock, 
                fileStreamBlock.filename);
    }
#endif

    // Keep session alive while we're receiving
    _sessionLastActiveMs = millis();

    // Handle file/stream types
    RaftRetCode handledOk = RAFT_INVALID_DATA;
    switch(_fileStreamContentType)
    {
        case FileStreamBase::FILE_STREAM_CONTENT_TYPE_FIRMWARE:
            handledOk = writeFirmwareBlock(fileStreamBlock);
            break;
        case FileStreamBase::FILE_STREAM_CONTENT_TYPE_FILE:
            handledOk = writeFileBlock(fileStreamBlock);
            break;
        case FileStreamBase::FILE_STREAM_CONTENT_TYPE_RT_STREAM:
            handledOk = writeRealTimeStreamBlock(fileStreamBlock);
            break;
        default:
        {
            _isActive = false;
#ifdef DEBUG_FILE_STREAM_START_END
            LOG_I(MODULE_PREFIX, "fileStreamBlockWrite invalid type %d isActive %d", _fileStreamContentType, _isActive);
#endif
            return RAFT_INVALID_DATA;
        }
    }
#ifdef DEBUG_FILE_STREAM_BLOCK
    LOG_I(MODULE_PREFIX, "fileStreamBlockWrite write finished, time %dms, handledOk: %s", (millis() - _sessionLastActiveMs), Raft::getRetCodeStr(handledOk));
#endif

    // Check handled ok
    if (handledOk == RAFT_OK)
    {
        // Check for first block
        if (fileStreamBlock.firstBlock)
            _startTimeMs = millis();

        // Check for final block
        if (fileStreamBlock.finalBlock)
        {
            _isActive = false;
#ifdef DEBUG_FILE_STREAM_START_END
            LOG_I(MODULE_PREFIX, "fileStreamBlockWrite final block received, time %dms, totalBytes %d, totalWriteTimeUs %dus, totalChunks %d isActive %d", 
                        (millis() - _startTimeMs), _totalBytes, _totalWriteTimeUs, _totalChunks, _isActive);
#endif
        }

        // Update stats
        _totalChunks++;
    }
    else if (handledOk != RAFT_BUSY)
    {
        // Not handled ok
        _isActive = false;
#ifdef DEBUG_FILE_STREAM_START_END
        LOG_I(MODULE_PREFIX, "fileStreamBlockWrite not handled ok, time %dms, handledOk: %s, isActive %d", 
                    (millis() - _startTimeMs), Raft::getRetCodeStr(handledOk), _isActive);
#endif
    }
    return handledOk;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Write firmware block
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileStreamSession::writeFirmwareBlock(FileStreamBlock& fileStreamBlock)
{
    // Firmware updater valid?
    if (!_pFirmwareUpdater)
        return RAFT_INVALID_OPERATION;

    // Check if this is the first block
    if (fileStreamBlock.firstBlock)
    {
        // Start OTA update
        // For ESP32 there will be a long delay at this point - around 4 seconds so
        // the block will not get acknowledged until after that
        if (!_pFirmwareUpdater->fileStreamStart(fileStreamBlock.filename, fileStreamBlock.fileLen))
        {
#ifdef WARN_ON_FW_UPDATE_FAILED
            LOG_W(MODULE_PREFIX, "writeFirmwareBlock start FAILED name %s len %d",
                            fileStreamBlock.filename, fileStreamBlock.fileLen);
#endif
            return RAFT_CANNOT_START;
        }
    }
    uint64_t startUs = micros();
    RaftRetCode fwRslt = _pFirmwareUpdater->fileStreamDataBlock(fileStreamBlock);
    _totalBytes += fileStreamBlock.blockLen;
    _totalWriteTimeUs += micros() - startUs;
    return fwRslt;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Write file block
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileStreamSession::writeFileBlock(FileStreamBlock& fileStreamBlock)
{
    // Write using the chunker
    if (!_pFileChunker)
        return RAFT_INVALID_OPERATION;

    uint32_t bytesWritten = 0; 
    uint64_t startUs = micros();
    bool chunkerRslt = _pFileChunker->nextWrite(fileStreamBlock.pBlock, fileStreamBlock.blockLen, 
                    bytesWritten, fileStreamBlock.finalBlock);
    _totalBytes += bytesWritten;
    _totalWriteTimeUs += micros() - startUs;
    return chunkerRslt ? RAFT_OK : RAFT_OTHER_FAILURE;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Write file block
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileStreamSession::writeRealTimeStreamBlock(FileStreamBlock& fileStreamBlock)
{
    // Check valid
    if (!_pStreamChunkCB)
        return RAFT_INVALID_OPERATION;

    // Write to stream
    return _pStreamChunkCB(_streamRequestStr, fileStreamBlock, _streamSourceInfo);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// file/stream cancel - callback from FileStreamBase
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileStreamSession::fileStreamCancelEnd(bool isNormalEnd)
{
    // Session no longer active
    _isActive = false;

#ifdef DEBUG_FILE_STREAM_START_END
    LOG_I(MODULE_PREFIX, "fileStreamCancelEnd filename %s channelID %d isActive %d pSession %p", 
                _fileStreamName.c_str(), _channelID, _isActive, this);
#endif

    // Check if we should cancel a firmware update
    if (_pFirmwareUpdater && (_fileStreamContentType == FileStreamBase::FILE_STREAM_CONTENT_TYPE_FIRMWARE))
    {
        _pFirmwareUpdater->fileStreamCancelEnd(isNormalEnd);
        return;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get file/stream message type
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileStreamBase::FileStreamMsgType FileStreamSession::getFileStreamMsgType(const RICRESTMsg& ricRESTReqMsg,
                const String& cmdName)
{
    // Check with Upload OKTO protocol
    FileStreamBase::FileStreamMsgType msgType = 
                    FileUploadOKTOProtocol::getFileStreamMsgType(ricRESTReqMsg, cmdName);
    if (msgType != FileStreamBase::FILE_STREAM_MSG_TYPE_NONE)
        return msgType;

    // Check with Download OKTO protocol
    msgType = FileDownloadOKTOProtocol::getFileStreamMsgType(ricRESTReqMsg, cmdName);
    if (msgType != FileStreamBase::FILE_STREAM_MSG_TYPE_NONE)
        return msgType;

    // Check with Stream Datagram protocol
    msgType = StreamDatagramProtocol::getFileStreamMsgType(ricRESTReqMsg, cmdName);
    if (msgType != FileStreamBase::FILE_STREAM_MSG_TYPE_NONE)
        return msgType;

    // Return none
    return FileStreamBase::FILE_STREAM_MSG_TYPE_NONE;
}