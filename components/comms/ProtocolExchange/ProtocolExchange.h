/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ProtocolExchange
// Hub for handling protocol endpoint messages
//
// Rob Dobson 2021-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <vector>
#include "RaftArduino.h"
#include "ProtocolBase.h"
#include "ProtocolCodecFactoryHelper.h"
#include "CommsChannel.h"
#include "RaftSysMod.h"
#include "FileStreamBase.h"
#include "FileStreamSession.h"
#include "FileStreamActivityHookFnType.h"

class APISourceInfo;

class ProtocolExchange : public RaftSysMod
{
public:
    ProtocolExchange(const char *pModuleName, RaftJsonIF& sysConfig);
    virtual ~ProtocolExchange();

    // Set file stream activity hook function
    void setFileStreamActivityHook(FileStreamActivityHookFnType fileStreamActivityHookFn)
    {
        _fileStreamActivityHookFn = fileStreamActivityHookFn;
    }

    // Set firmware update handler
    void setFWUpdateHandler(RaftSysMod* pFirmwareUpdater)
    {
        _pFirmwareUpdater = pFirmwareUpdater;
    }

    // File Upload Block - only used for HTTP file uploads
    RaftRetCode handleFileUploadBlock(const String& req, FileStreamBlock& fileStreamBlock, 
            const APISourceInfo& sourceInfo, FileStreamBase::FileStreamContentType fileStreamContentType,
            const char* restAPIEndpointName);

    // HTTP file uploads are written in the background: whether the channel's upload can take more data now (the
    // web server stops reading the request when it can't), and whether it is still being written (the reply to the
    // upload waits until it isn't)
    bool fileUploadCanAccept(uint32_t channelID);
    bool isFileUploadWriting(uint32_t channelID);
    // How the channel's last upload written in the background ended (RAFT_BUSY while it is still being written)
    RaftRetCode getFileUploadWriteStatus(uint32_t channelID);
    static const uint32_t HTTP_UPLOAD_PENDING_MAX_BYTES = 16384;
    static const uint32_t HTTP_UPLOAD_CHUNK_MAX_BYTES = 4096;

protected:
    // Loop - called frequently
    virtual void loop() override final;

    // Add comms channels
    virtual void addCommsChannels(CommsCoreIF& commsCore) override final;

    // Get debug info
    virtual String getDebugJSON() const override final;

private:
    // Handlers
    RaftSysMod* _pFirmwareUpdater = nullptr;

    // Next streamID to allocate to a stream session
    uint32_t _nextStreamID = FileStreamBase::FILE_STREAM_ID_MIN;

    // Transfer sessions
    static const uint32_t MAX_SIMULTANEOUS_FILE_STREAM_SESSIONS = 3;
    std::list<FileStreamSession*> _sessions;

    // How the last upload written in the background ended, for each channel (the session is gone by the time an
    // HTTP upload's reply is sent)
    std::list<std::pair<uint32_t, RaftRetCode>> _lastUploadWriteStatus;
    static const uint32_t LAST_UPLOAD_WRITE_STATUS_MAX = 4;

    // Uploads checked against the CRC16 the client gave (by channel and file name), kept after the session ends
    // so the ufEnd reply (which usually comes after it) can say the file was checked
    struct CheckedUploadCRC
    {
        uint32_t channelID;
        String fileName;
        uint16_t crc;
    };
    std::list<CheckedUploadCRC> _checkedUploadCRCs;
    static const uint32_t CHECKED_UPLOAD_CRCS_MAX = 4;
    bool takeCheckedUploadCRC(uint32_t channelID, const String& fileName, uint16_t& crc);

    // Activity last reported through the file stream activity hook (to keep SysManager up-to-date)
    bool _activityReportedMainFWUpdate = false;
    bool _activityReportedFileSystem = false;
    bool _activityReportedStreaming = false;

    // Threshold for determining if message processing is slow
    static const uint32_t MSG_PROC_SLOW_PROC_THRESH_MS = 50;

    // Process endpoint message
    bool canProcessEndpointMsg();
    bool processEndpointMsg(CommsChannelMsg& msg);
    RaftRetCode processRICRESTURL(RICRESTMsg& ricRESTReqMsg, String& respMsg, const APISourceInfo& sourceInfo);
    RaftRetCode processRICRESTBody(RICRESTMsg& ricRESTReqMsg, String& respMsg, const APISourceInfo& sourceInfo);
    RaftRetCode processRICRESTCmdRespJSON(RICRESTMsg& ricRESTReqMsg, String& respMsg, 
                const APISourceInfo& sourceInfo);
    RaftRetCode processRICRESTCmdFrame(RICRESTMsg& ricRESTReqMsg, String& respMsg, 
                        const CommsChannelMsg &endpointMsg, const APISourceInfo& sourceInfo);
    RaftRetCode processRICRESTFileStreamBlock(const RICRESTMsg& ricRESTReqMsg, String& respMsg, CommsChannelMsg &cmdMsg);
    RaftRetCode processRICRESTNonFileStream(const String& cmdName, RICRESTMsg& ricRESTReqMsg, String& respMsg, 
                const APISourceInfo& sourceInfo);

    // File/stream session handling
    FileStreamSession* findFileStreamSession(uint32_t streamID, const char* fileStreamName, uint32_t channelID);
    FileStreamSession* getFileStreamNewSession(const char* fileStreamName, uint32_t channelID, 
                    FileStreamBase::FileStreamContentType fileStreamContentType, const char* restAPIEndpointName,
                    FileStreamBase::FileStreamFlowType flowType, uint32_t fileStreamLength);
    FileStreamSession* getFileStreamExistingSession(const char* fileStreamName, uint32_t channelID, uint32_t streamID);

    // File stream activity hook fn
    FileStreamActivityHookFnType _fileStreamActivityHookFn = nullptr;

    // Debug
    void debugEndpointMessage(const CommsChannelMsg& msg);
    void debugRICRESTMessage(const CommsChannelMsg &cmdMsg, const RICRESTMsg& ricRESTReqMsg);
    void debugRICRESTResponse(const CommsChannelMsg &endpointMsg);

    // Debug
    static constexpr const char* MODULE_PREFIX = "ProtExch";
};
