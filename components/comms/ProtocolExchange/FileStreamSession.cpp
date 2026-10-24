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
#include "FileSystemWorker.h"
#include <algorithm>

// Warn
#define WARN_ON_FW_UPDATE_FAILED
#define WARN_ON_STREAM_FAILED
#define WARN_ON_FILE_CHUNKER_START_FAILED
#define WARN_ON_BACKGROUND_WRITE_FAILED

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
    RaftMutex_init(_writeMutex);

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
                _streamRequestStr = String(restAPIEndpointName) + "?streamID=" + String(streamID) + "&fileName=" + filename;

                // Notify the endpoint that the stream now exists. The callback
                // receives the allocated streamID in the request string so it can
                // attach state before the first data block arrives.
                if (pRestAPIEndpoint->_callbackMain)
                {
                    String streamStartResp;
                    RaftRetCode startRslt = pRestAPIEndpoint->_callbackMain(_streamRequestStr, streamStartResp, _streamSourceInfo);
                    if (startRslt != RAFT_OK)
                    {
                        delete _pFileStreamProtocolHandler;
                        _pFileStreamProtocolHandler = nullptr;
                        _pStreamChunkCB = nullptr;
                        _pStreamIsReadyCB = nullptr;
                    }
                }
            }
            else
            {
                // No endpoint to deliver the stream to - don't start the session (otherwise the start is
                // acknowledged, every block fails and the session stays active until it times out)
                delete _pFileStreamProtocolHandler;
                _pFileStreamProtocolHandler = nullptr;
            }

            break;
        }
        default:
        {
            // Not supported
            break;
        }
    }
    
    // File uploads are written on the file system worker, the upload protocol waiting for the writes before it
    // acknowledges blocks
    if (_pFileStreamProtocolHandler && (_fileStreamContentType == FileStreamBase::FILE_STREAM_CONTENT_TYPE_FILE) &&
                FileStreamBase::isUploadFlowType(_fileStreamFlowType))
    {
        _writeInBackground = true;
        _pFileStreamProtocolHandler->setWriteStatusFn(std::bind(&FileStreamSession::getWriteStatus, this));
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
    // A session is only removed once its background writes are finished (isActive()) - this is a safety net
    if (_writeJobOutstanding.load())
    {
        RaftMutex_lock(_writeMutex, RAFT_MUTEX_WAIT_FOREVER);
        _writeCancelled = true;
        RaftMutex_unlock(_writeMutex);
        uint32_t waitStartMs = millis();
        while (_writeJobOutstanding.load() && !Raft::isTimeout(millis(), waitStartMs, DESTRUCT_WRITE_WAIT_MAX_MS))
            RaftThread_sleep(1);
    }

    // Tidy up
    delete _pFileChunker;
    delete _pFileStreamProtocolHandler;
    RaftMutex_destroy(_writeMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle command frame
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileStreamSession::loop()
{
    // Background writes - checked before the protocol is serviced, so that if they are finished it has sent its
    // final acknowledgement before the session ends
    bool writesFinished = !_writeJobOutstanding.load();
    if (!writesFinished)
        _sessionLastActiveMs = millis();

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

    // The session ends when the final block is written
    if (_isActive && _finalBlockReceived && writesFinished)
    {
#ifdef DEBUG_FILE_STREAM_START_END
        LOG_I(MODULE_PREFIX, "loop final block written filename %s channelID %d", _fileStreamName.c_str(), _channelID);
#endif
        _isActive = false;
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
    SpiramAwareUint8Vector chunkBuf;
    const uint32_t CRC_CHUNK_SIZE = SpiramAwareAllocator<uint8_t>::max_allocatable() > 500000 ? 2000 : 500;
    chunkBuf.resize(CRC_CHUNK_SIZE);
    bool finalBlockRead = false;
    while (!finalBlockRead)
    {
        // Get next chunk
        uint32_t bytesRead = 0;
        bool readOk = _pFileChunker->nextRead(chunkBuf.data(), chunkBuf.size(), bytesRead, finalBlockRead);

        // Check for error
        if (!readOk)
            break;

        // Calculate CRC
        crcValue = MiniHDLC::crcUpdateCCITT(crcValue, chunkBuf.data(), bytesRead);
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

    // Allocate buffer
    SpiramAwareUint8Vector chunkBuf;
    chunkBuf.resize(maxLen);

    // Check allocation
    if (chunkBuf.size() == 0)
        return RaftRetCode::RAFT_INSUFFICIENT_RESOURCE;

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
    uint32_t bytesRead = 0;
    bool finalBlockRead = false;
    bool readOk = _pFileChunker->nextRead(chunkBuf.data(), chunkBuf.size(), bytesRead, finalBlockRead);

    // Fill fileStreamBlock
    uint32_t fileLen = _pFileChunker->getFileLen();
    fileStreamBlock.set(_pFileChunker->getFileName().c_str(),
            fileLen,
            filePos,
            chunkBuf.data(),
            bytesRead,
            finalBlockRead,
            0, false,
            fileLen, true,
            filePos == 0);
    return readOk ? RaftRetCode::RAFT_OK : RaftRetCode::RAFT_NOT_XFERING;
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

        // Check for final block (with background writes the session ends when it is written)
        if (fileStreamBlock.finalBlock)
        {
            if (_writeInBackground)
                _finalBlockReceived = true;
            else
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

    // Written here (not in the background)
    if (!_writeInBackground)
    {
        uint32_t bytesWritten = 0; 
        uint64_t startUs = micros();
        bool chunkerRslt = _pFileChunker->nextWrite(fileStreamBlock.pBlock, fileStreamBlock.blockLen, 
                        bytesWritten, fileStreamBlock.finalBlock);
        _totalBytes += bytesWritten;
        _totalWriteTimeUs += micros() - startUs;
        return chunkerRslt ? RAFT_OK : RAFT_OTHER_FAILURE;
    }

    // Add the block to the data waiting to be written
    RaftMutex_lock(_writeMutex, RAFT_MUTEX_WAIT_FOREVER);
    if (_writeError != RAFT_OK)
    {
        RaftRetCode rslt = _writeError;
        RaftMutex_unlock(_writeMutex);
        return rslt;
    }
    uint32_t newSize = _writePending.size() + fileStreamBlock.blockLen;
    if (newSize > _writePending.capacity())
    {
        // Check the memory is there before growing (with exceptions disabled a failed allocation would crash).
        // Grow geometrically, or to just what is needed when memory is short
        uint32_t maxAllocatable = SpiramAwareAllocator<uint8_t>::max_allocatable();
        uint32_t newCapacity = std::max(newSize, (uint32_t)_writePending.capacity() * 2);
        if (newCapacity + WRITE_HEAP_MARGIN_BYTES > maxAllocatable)
            newCapacity = newSize;
        if (newCapacity + WRITE_HEAP_MARGIN_BYTES > maxAllocatable)
        {
            RaftMutex_unlock(_writeMutex);
#ifdef WARN_ON_BACKGROUND_WRITE_FAILED
            LOG_W(MODULE_PREFIX, "writeFileBlock no memory for %d bytes waiting to be written (largest free block %d)",
                        newSize, maxAllocatable);
#endif
            return RAFT_INSUFFICIENT_RESOURCE;
        }
        _writePending.reserve(newCapacity);
    }
    if (fileStreamBlock.pBlock && (fileStreamBlock.blockLen > 0))
        _writePending.insert(_writePending.end(), fileStreamBlock.pBlock, fileStreamBlock.pBlock + fileStreamBlock.blockLen);
    if (fileStreamBlock.finalBlock)
        _writePendingFinal = true;
    bool startJob = !_writeJobOutstanding.load();
    _writeJobOutstanding = true;
    RaftMutex_unlock(_writeMutex);

    // Start writing (or write here if the worker can't take the job)
    if (startJob && !fileSystemWorker.submit(std::bind(&FileStreamSession::writeJob, this)))
        writeJob();
    return RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Background write job (runs on the file system worker): writes what is waiting until there is nothing left
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileStreamSession::writeJob()
{
    SpiramAwareUint8Vector toWrite;
    while (true)
    {
        // Take what is waiting
        RaftMutex_lock(_writeMutex, RAFT_MUTEX_WAIT_FOREVER);
        if (_writeCancelled || (_writeError != RAFT_OK))
        {
            _writePending.clear();
            _writePendingFinal = false;
        }
        if (_writePending.empty() && !_writePendingFinal)
        {
            _writeJobOutstanding = false;
            RaftMutex_unlock(_writeMutex);
            return;
        }
        toWrite.swap(_writePending);
        bool isFinal = _writePendingFinal;
        _writePendingFinal = false;
        RaftMutex_unlock(_writeMutex);

        // Write it
        RaftRetCode rslt = writeToFile(toWrite.data(), toWrite.size(), isFinal);
        toWrite.clear();
        if (rslt != RAFT_OK)
        {
#ifdef WARN_ON_BACKGROUND_WRITE_FAILED
            LOG_W(MODULE_PREFIX, "writeJob write failed filename %s", _fileStreamName.c_str());
#endif
            RaftMutex_lock(_writeMutex, RAFT_MUTEX_WAIT_FOREVER);
            _writeError = rslt;
            RaftMutex_unlock(_writeMutex);
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Write to the file in pieces (the file system lock is released between them), closing it after the final block
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileStreamSession::writeToFile(const uint8_t* pBuf, uint32_t len, bool isFinal)
{
    uint32_t pos = 0;
    do
    {
        uint32_t pieceLen = std::min(len - pos, WRITE_PIECE_MAX_BYTES);
        bool finalPiece = isFinal && (pos + pieceLen >= len);
        uint32_t bytesWritten = 0;
        uint64_t startUs = micros();
        bool writeOk = _pFileChunker->nextWrite(pBuf ? pBuf + pos : nullptr, pieceLen, bytesWritten, finalPiece);
        _totalBytes += bytesWritten;
        _totalWriteTimeUs += micros() - startUs;
        if (!writeOk)
            return RAFT_OTHER_FAILURE;
        pos += pieceLen;
    } while (pos < len);
    return RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Background write status
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode FileStreamSession::getWriteStatus()
{
    RaftMutex_lock(_writeMutex, RAFT_MUTEX_WAIT_FOREVER);
    RaftRetCode rslt = _writeError;
    if ((rslt == RAFT_OK) && _writeJobOutstanding.load())
        rslt = RAFT_BUSY;
    RaftMutex_unlock(_writeMutex);
    return rslt;
}

uint32_t FileStreamSession::getWritePendingBytes()
{
    RaftMutex_lock(_writeMutex, RAFT_MUTEX_WAIT_FOREVER);
    uint32_t pendingBytes = _writePending.size();
    RaftMutex_unlock(_writeMutex);
    return pendingBytes;
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
    // Session no longer active (it is removed once any background write in progress stops)
    _isActive = false;
    if (!isNormalEnd)
    {
        RaftMutex_lock(_writeMutex, RAFT_MUTEX_WAIT_FOREVER);
        _writeCancelled = true;
        RaftMutex_unlock(_writeMutex);
    }

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