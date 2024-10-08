/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// File Upload Over HTTP Protocol
//
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "FileStreamBase.h"

class FileUploadHTTPProtocol : public FileStreamBase
{
public:
    // Constructor
    FileUploadHTTPProtocol(FileStreamBlockWriteFnType fileBlockWrite, 
            FileStreamBlockReadFnType fileBlockRead,
            FileStreamGetCRCFnType fileGetCRC,
            FileStreamCancelEndFnType fileCancelEnd,
            CommsCoreIF* pCommsCore,
            FileStreamBase::FileStreamContentType fileStreamContentType,
            FileStreamBase::FileStreamFlowType fileStreamFlowType,
            uint32_t streamID, 
            uint32_t fileStreamLength,
            const char* fileStreamName);

    // Service
    virtual void loop() override final;

    // Handle command frame
    virtual RaftRetCode handleCmdFrame(FileStreamBase::FileStreamMsgType fsMsgType,
                const RICRESTMsg& ricRESTReqMsg, String& respMsg, 
                const CommsChannelMsg &endpointMsg) override final;

    // Handle received file/stream block
    virtual RaftRetCode handleDataFrame(const RICRESTMsg& ricRESTReqMsg, String& respMsg) override final;

    // Get debug str
    virtual String getDebugJSON(bool includeBraces) override final;

    // Is active
    virtual bool isActive() override final;

private:
    // Debug
    static constexpr const char* MODULE_PREFIX = "FileULHTTP";

};
