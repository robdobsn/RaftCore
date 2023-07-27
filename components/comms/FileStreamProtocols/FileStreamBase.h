/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// File/Stream Protocol Base
//
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <functional>
#include <RaftArduino.h>
#include <RaftRetCode.h>

class RICRESTMsg;
class CommsChannelMsg;
class CommsCoreIF;
class SysModBase;
class FileStreamBlock;
class FileStreamBlockOwned;
class APISourceInfo;
class JSONParams;

// File/stream callback function types
typedef std::function<RaftRetCode(FileStreamBlock& fileBlock)> FileStreamBlockWriteCB;
typedef std::function<RaftRetCode(FileStreamBlockOwned& fileBlock, uint32_t filePos, uint32_t maxLen)> FileStreamBlockReadCB;
typedef std::function<RaftRetCode(uint32_t& fileCRC, uint32_t& fileLen)> FileStreamGetCRCCB;
typedef std::function<void(bool isNormalEnd)> FileStreamCancelEndCB;

class FileStreamBase
{
public:

    // File/Stream Content Type
    enum FileStreamContentType
    {
        FILE_STREAM_CONTENT_TYPE_FILE,
        FILE_STREAM_CONTENT_TYPE_FIRMWARE,
        FILE_STREAM_CONTENT_TYPE_RT_STREAM
    };

    // Get file/stream message type
    enum FileStreamMsgType
    {
        FILE_STREAM_MSG_TYPE_NONE,
        FILE_STREAM_MSG_TYPE_UPLOAD_START,
        FILE_STREAM_MSG_TYPE_UPLOAD_END,
        FILE_STREAM_MSG_TYPE_UPLOAD_CANCEL,
        FILE_STREAM_MSG_TYPE_UPLOAD_ACK,
        FILE_STREAM_MSG_TYPE_DOWNLOAD_START,
        FILE_STREAM_MSG_TYPE_DOWNLOAD_END,
        FILE_STREAM_MSG_TYPE_DOWNLOAD_CANCEL,
        FILE_STREAM_MSG_TYPE_DOWNLOAD_ACK
    };

    // File/Stream Flow Type
    enum FileStreamFlowType
    {
        FILE_STREAM_FLOW_TYPE_HTTP_UPLOAD,
        FILE_STREAM_FLOW_TYPE_RICREST_UPLOAD,
        FILE_STREAM_FLOW_TYPE_RICREST_DOWNLOAD
    };

    // Constructor
    FileStreamBase(FileStreamBlockWriteCB fileBlockWriteCB, 
            FileStreamBlockReadCB fileBlockReadCB,
            FileStreamGetCRCCB fileGetCRCCB,
            FileStreamCancelEndCB fileCancelEndCB,
            CommsCoreIF* pCommsCoreIF,
            FileStreamBase::FileStreamContentType fileStreamContentType,
            FileStreamBase::FileStreamFlowType fileStreamFlowType,
            uint32_t streamID,
            uint32_t fileStreamLength,
            const char* fileStreamName);

    // Destructor
    virtual ~FileStreamBase();

    // Service file/stream
    virtual void service() = 0;

    virtual void resetCounters(uint32_t fileStreamLength){};

    // Handle command frame
    virtual RaftRetCode handleCmdFrame(FileStreamBase::FileStreamMsgType fsMsgType, 
                const RICRESTMsg& ricRESTReqMsg, String& respMsg, 
                const CommsChannelMsg &endpointMsg) = 0;

    // Handle data frame (file/stream block)
    virtual RaftRetCode handleDataFrame(const RICRESTMsg& ricRESTReqMsg, String& respMsg) = 0;

    // Get debug str
    virtual String getDebugJSON(bool includeBraces) = 0;

    // Get file/stream message information from header
    static void getFileStreamMsgInfo(const JSONParams& cmdFrame,
                String& fileStreamName, 
                FileStreamContentType& fileStreamContentType, uint32_t& streamID,
                String& restAPIEndpointName, uint32_t& fileStreamLength);

    // File/stream message type string
    static const char* getFileStreamMsgTypeStr(FileStreamMsgType msgType)
    {
        switch (msgType)
        {
            case FILE_STREAM_MSG_TYPE_UPLOAD_START: return "ufStart";
            case FILE_STREAM_MSG_TYPE_UPLOAD_END: return "ufEnd";
            case FILE_STREAM_MSG_TYPE_UPLOAD_CANCEL: return "ufCancel";
            case FILE_STREAM_MSG_TYPE_DOWNLOAD_START: return "dfStart";
            case FILE_STREAM_MSG_TYPE_DOWNLOAD_END: return "dfEnd";
            case FILE_STREAM_MSG_TYPE_DOWNLOAD_CANCEL: return "dfCancel";
            default: return "unknown";
        }
    }

    // Content type string
    static const char* getFileStreamContentTypeStr(FileStreamContentType fileStreamContentType)
    {
        switch (fileStreamContentType)
        {
        case FILE_STREAM_CONTENT_TYPE_FILE: return "file";
        case FILE_STREAM_CONTENT_TYPE_FIRMWARE: return "firmware";
        case FILE_STREAM_CONTENT_TYPE_RT_STREAM: return "realTimeStream";
        default: return "unknown";
        }
    }

    // Get fileStreamContentType from string
    static FileStreamContentType getFileStreamContentType(const String& fileStreamTypeStr);

    // Get fileStreamFlowType string
    static const char* getFileStreamFlowTypeStr(FileStreamFlowType fileStreamFlowType)
    {
        switch (fileStreamFlowType)
        {
        case FILE_STREAM_FLOW_TYPE_HTTP_UPLOAD: return "httpUpload";
        case FILE_STREAM_FLOW_TYPE_RICREST_UPLOAD: return "ricRestUpload";
        case FILE_STREAM_FLOW_TYPE_RICREST_DOWNLOAD: return "ricRestDownload";
        default: return "unknown";
        }
    }

    // Check if flow type is upload
    static bool isUploadFlowType(FileStreamFlowType fileStreamFlowType)
    {
        return (fileStreamFlowType == FILE_STREAM_FLOW_TYPE_HTTP_UPLOAD) ||
                (fileStreamFlowType == FILE_STREAM_FLOW_TYPE_RICREST_UPLOAD);
    }

    // StreamID values
    static const uint32_t FILE_STREAM_ID_ANY = 0;
    static const uint32_t FILE_STREAM_ID_MIN = 1;
    static const uint32_t FILE_STREAM_ID_MAX = 255;

    // Get streamID
    virtual uint32_t getStreamID()
    {
        return _streamID;
    }

    // Is active
    virtual bool isActive() = 0;

protected:
    // Callbacks
    FileStreamBlockWriteCB _fileStreamBlockWriteCB;
    FileStreamBlockReadCB _fileStreamBlockReadCB;
    FileStreamGetCRCCB _fileStreamGetCRCCB;
    FileStreamCancelEndCB _fileStreamCancelEndCB;

    // Comms core
    CommsCoreIF* _pCommsCore;

    // File/stream content type
    FileStreamContentType _fileStreamContentType;

    // File/stream flow type
    FileStreamFlowType _fileStreamFlowType;

    // StreamID
    uint32_t _streamID;

    // File/stream length
    uint32_t _fileStreamLength;

    // Name of file/stream
    String _fileStreamName;
};