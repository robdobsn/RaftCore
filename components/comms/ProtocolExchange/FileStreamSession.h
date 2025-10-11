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

    // Info
    bool isActive() const
    {
        return _isActive;
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
