/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// File/Stream Base (for protocol handlers)
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "FileStreamBase.h"
#include "RaftJsonIF.h"
#include "RICRESTMsg.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileStreamBase::FileStreamBase(FileStreamBlockWriteFnType fileBlockWrite, 
        FileStreamBlockReadFnType fileBlockRead,
        FileStreamGetCRCFnType fileGetCRC,
        FileStreamCancelEndFnType fileCancelEnd,
        CommsCoreIF* pCommsCore,
        FileStreamBase::FileStreamContentType fileStreamContentType,
        FileStreamBase::FileStreamFlowType fileStreamFlowType,
        uint32_t streamID,
        uint32_t fileStreamLength,
        const char* fileStreamName)
{
    // Vars
    _fileStreamBlockWrite = fileBlockWrite;
    _fileStreamBlockRead = fileBlockRead;
    _fileStreamGetCRC = fileGetCRC;
    _fileStreamCancelEnd = fileCancelEnd;
    _pCommsCore = pCommsCore;
    _fileStreamContentType = fileStreamContentType;
    _fileStreamFlowType = fileStreamFlowType;
    _streamID = streamID;
    _fileStreamLength = fileStreamLength;
    _fileStreamName = fileStreamName;
}

FileStreamBase::~FileStreamBase()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get information from file stream start message
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FileStreamBase::getFileStreamMsgInfo(const RaftJsonIF& cmdFrame,
                String& fileStreamName, 
                FileStreamContentType& fileStreamContentType, uint32_t& streamID,
                String& restAPIEndpointName, uint32_t& fileStreamLength)
{
    // Extract info
    fileStreamName = cmdFrame.getString("fileName", "");
    String fileStreamTypeStr = cmdFrame.getString("fileType", "");
    streamID = cmdFrame.getLong("streamID", FILE_STREAM_ID_ANY);
    fileStreamContentType = getFileStreamContentType(fileStreamTypeStr);
    restAPIEndpointName = cmdFrame.getString("endpoint", "");
    fileStreamLength = cmdFrame.getLong("fileLen", 0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Get fileStreamContentType from string
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileStreamBase::FileStreamContentType FileStreamBase::getFileStreamContentType(const String& fileStreamTypeStr)
{
    if ((fileStreamTypeStr.length() == 0) || fileStreamTypeStr.equalsIgnoreCase("fs") || fileStreamTypeStr.equalsIgnoreCase("file"))
        return FILE_STREAM_CONTENT_TYPE_FILE;
    if (fileStreamTypeStr.equalsIgnoreCase("fw") || fileStreamTypeStr.equalsIgnoreCase("ricfw"))
        return FILE_STREAM_CONTENT_TYPE_FIRMWARE;
    if (fileStreamTypeStr.equalsIgnoreCase("rtstream"))
        return FILE_STREAM_CONTENT_TYPE_RT_STREAM;
    return FILE_STREAM_CONTENT_TYPE_FILE;
}
