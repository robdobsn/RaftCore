/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// File Stream Session
//
// Rob Dobson 2021-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftUtils.h"
#include "RaftArduino.h"
#include "FileStreamBlock.h"
#include "FileStreamBlockOwned.h"
#include "FileSystemChunker.h"
#include "FileStreamBase.h"
#include "RestAPIEndpoint.h"
#include "RaftThreading.h"
#include "SpiramAwareAllocator.h"
#include <atomic>

class RestAPIEndpointManager;

class FileStreamSession
{
public:
    // Constructor / destructor
    FileStreamSession(const String& filename, uint32_t channelID,
                CommsCoreIF* pCommsCore, RaftSysMod* pFirmwareUpdater,
                FileStreamBase::FileStreamContentType fileStreamContentType, 
                FileStreamBase::FileStreamFlowType fileStreamFlowType,
                uint32_t streamID, const char* restAPIEndpointName,
                RestAPIEndpointManager* pRestAPIEndpointManager,
                uint32_t fileStreamLength);
    virtual ~FileStreamSession();

    // Info - a session stays active until its background writes and reads are finished
    bool isActive() const
    {
        return _isActive || _writeJobOutstanding.load() || _readJobOutstanding.load();
    }
    const String& getFileStreamName() const
    {
        return _fileStreamName;
    }
    uint32_t getChannelID() const
    {
        return _channelID;
    }
    uint32_t getStreamID() const
    {
        if (_pFileStreamProtocolHandler)
            return _pFileStreamProtocolHandler->getStreamID();
        return FileStreamBase::FILE_STREAM_ID_ANY;
    }
    bool isMainFWUpdate() const
    {
        return _fileStreamContentType == FileStreamBase::FILE_STREAM_CONTENT_TYPE_FIRMWARE;
    }
    bool isFileSystemActivity() const
    {
        return _fileStreamContentType == FileStreamBase::FILE_STREAM_CONTENT_TYPE_FILE;
    }
    bool isStreaming() const
    {
        return _fileStreamContentType == FileStreamBase::FILE_STREAM_CONTENT_TYPE_RT_STREAM;
    }
    bool isUpload() const
    {
        return true;
    }
    void loop();

    void resetCounters(uint32_t fileStreamLength);

    // Static method to get file/stream message type
    static FileStreamBase::FileStreamMsgType getFileStreamMsgType(const RICRESTMsg& ricRESTReqMsg,
                const String& cmdName);

    // Handle command frame
    RaftRetCode handleCmdFrame(FileStreamBase::FileStreamMsgType fsMsgType,
                const RICRESTMsg& ricRESTReqMsg, String& respMsg, 
                const CommsChannelMsg &endpointMsg);

    // Handle file/stream block message
    RaftRetCode handleDataFrame(const RICRESTMsg& ricRESTReqMsg, String& respMsg);

    // Handle file read/write and cancel
    RaftRetCode fileStreamBlockWrite(FileStreamBlock& fileStreamBlock);
    RaftRetCode fileStreamBlockRead(FileStreamBlockOwned& fileStreamBlock, uint32_t filePos, uint32_t maxLen);
    RaftRetCode fileStreamGetCRC(uint32_t& crc, uint32_t& fileLen);
    void fileStreamCancelEnd(bool isNormalEnd);

    // Background writes (file uploads are written on the file system worker)
    // Status: RAFT_BUSY while data received is still to be written, RAFT_OK when it is all written, or the error
    RaftRetCode getWriteStatus();
    // Bytes received and not yet written
    uint32_t getWritePendingBytes();
    // Whether len more bytes can be taken now (there's room for them, or the memory to grow the buffer)
    bool canAcceptWriteBytes(uint32_t len);
    bool isWriteBusy() const
    {
        return _writeJobOutstanding.load();
    }

    // Debug
    String getDebugJSON() const;
    
private:
    // Is active
    bool _isActive = false;

    // File/stream name and content type
    String _fileStreamName;
    FileStreamBase::FileStreamContentType _fileStreamContentType;

    // File/stream flow type
    FileStreamBase::FileStreamFlowType _fileStreamFlowType;

    // Endpoint name
    String _restAPIEndpointName;
    
    // REST API endpoint manager
    RestAPIEndpointManager* _pRestAPIEndpointManager = nullptr;

    // Callbacks to stream endpoint
    RestAPIFnChunk _pStreamChunkCB;
    RestAPIFnIsReady _pStreamIsReadyCB;

    // Stream info
    String _streamRequestStr;
    APISourceInfo _streamSourceInfo;

    // Channel ID
    uint32_t _channelID;

    // Protocol handler
    FileStreamBase* _pFileStreamProtocolHandler = nullptr;

    // Chunker
    FileSystemChunker* _pFileChunker = nullptr;

    // Handlers
    RaftSysMod* _pFirmwareUpdater = nullptr;

    // Background writes (file uploads): data received waits in _writePending until the file system worker writes
    // it, in pieces of up to WRITE_PIECE_MAX_BYTES (the file system lock is released between them). The worker's job
    // swaps _writePending for its own buffer, so blocks keep arriving while it writes. _writeMutex guards the
    // _write... members except _writeJobOutstanding, which is set while _writePending holds data or a job runs
    bool _writeInBackground = false;
    // A firmware update written in the background by the firmware updater (its fileStreamWriteStatus())
    bool _firmwareWrittenInBackground = false;
    RaftMutex _writeMutex;
    SpiramAwareUint8Vector _writePending;
    bool _writePendingFinal = false;
    bool _writeCancelled = false;
    RaftRetCode _writeError = RAFT_OK;
    std::atomic<bool> _writeJobOutstanding{false};
    bool _finalBlockReceived = false;
    static constexpr uint32_t WRITE_PIECE_MAX_BYTES = 4096;
    static constexpr uint32_t WRITE_HEAP_MARGIN_BYTES = 16384;
    static constexpr uint32_t DESTRUCT_WRITE_WAIT_MAX_MS = 5000;
    void writeJob();
    RaftRetCode writeToFile(const uint8_t* pBuf, uint32_t len, bool isFinal);

    // Check of an uploaded file against the CRC16 (CRC16-CCITT, as filehash reports) the client gave in ufStart:
    // updated as the data is written, in order (on the file system worker for background writes), and compared
    // after the final block. A mismatch fails the upload (RAFT_CRC_MISMATCH) and the file is deleted
    bool _crcCheck = false;
    uint16_t _crcExpected = 0;
    uint16_t _crcWritten = 0;
    std::atomic<bool> _crcMatched{false};
    RaftRetCode checkWrittenCRC(const uint8_t* pBuf, uint32_t len, bool isFinal);

public:
    // The CRC16 the uploaded file was checked against - false unless it was given, the whole file has been written
    // and it matched
    bool getCheckedCRC(uint16_t& crc) const
    {
        if (!_crcMatched.load())
            return false;
        crc = _crcExpected;
        return true;
    }

private:

    // Background reads (file downloads): a block is read on the file system worker and the next one read ahead.
    // fileStreamBlockRead() returns RAFT_BUSY until the block asked for has been read. The job sets the _read...
    // results, then clears _readJobOutstanding; the main task only uses them when no job is outstanding
    std::atomic<bool> _readJobOutstanding{false};
    bool _readDone = false;
    uint32_t _readFilePos = 0;
    uint32_t _readMaxLen = 0;
    SpiramAwareUint8Vector _readData;
    bool _readFinal = false;
    bool _readOk = false;
    void startReadJob(uint32_t filePos, uint32_t maxLen);

    // Session idle handler
    uint32_t _sessionLastActiveMs;
    static const uint32_t MAX_SESSION_IDLE_TIME_MS = 
#ifdef IMPLEMENT_FILE_STREAM_SESSION_IDLE_TIMEOUT
                IMPLEMENT_FILE_STREAM_SESSION_IDLE_TIMEOUT;
#else
    10000;
#endif

    // Stats
    uint32_t _startTimeMs;
    uint64_t _totalWriteTimeUs;
    uint32_t _totalBytes;
    uint32_t _totalChunks;

    // Helpers
    RaftRetCode writeFirmwareBlock(FileStreamBlock& fileStreamBlock);
    RaftRetCode writeFileBlock(FileStreamBlock& fileStreamBlock);
    RaftRetCode writeRealTimeStreamBlock(FileStreamBlock& fileStreamBlock);

    // Debug
    static constexpr const char* MODULE_PREFIX = "FSess";

};
