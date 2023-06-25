/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Stream Datagram Protocol
//
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "FileStreamBase.h"

class StreamDatagramProtocol : public FileStreamBase
{
public:
    // Constructor
    StreamDatagramProtocol(FileStreamBlockWriteCB fileBlockWriteCB, 
            FileStreamBlockReadCB fileBlockReadCB,
            FileStreamGetCRCCB fileGetCRCCB,
            FileStreamCancelEndCB fileCancelEndCB,
            CommsCoreIF* pCommsCoreIF,
            FileStreamBase::FileStreamContentType fileStreamContentType, 
            FileStreamBase::FileStreamFlowType fileStreamFlowType,
            uint32_t streamID,
            uint32_t fileStreamLength,
            const char* fileStreamName);

    // Service
    void service() override final;

    void resetCounters(uint32_t fileStreamLength);

    // Handle command frame
    virtual RaftRetCode::RetCode handleCmdFrame(FileStreamBase::FileStreamMsgType fsMsgType, 
                const RICRESTMsg& ricRESTReqMsg, String& respMsg, 
                const CommsChannelMsg &endpointMsg) override final;

    // Handle received file/stream block
    virtual RaftRetCode::RetCode handleDataFrame(const RICRESTMsg& ricRESTReqMsg, String& respMsg) override final;

    // Get debug str
    virtual String getDebugJSON(bool includeBraces) override final;
    static const uint32_t MAX_DEBUG_BIN_HEX_LEN = 50;
    
    // Is active
    virtual bool isActive() override final;

    // Check if message is a file download message
    static FileStreamBase::FileStreamMsgType getFileStreamMsgType(const RICRESTMsg& ricRESTReqMsg,
                const String& cmdName)
    {
        return FILE_STREAM_MSG_TYPE_NONE;
    }

private:
    // Stream position
    uint32_t _streamPos;
    bool _continuingStream = false;

};
