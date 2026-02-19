/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ProtocolExchange
// Hub for handling protocol endpoint messages
//
// Rob Dobson 2021-23
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftArduino.h"
#include "Logger.h"
#include "ProtocolExchange.h"
#include "CommsChannelMsg.h"
#include "RestAPIEndpointManager.h"
#include "ProtocolRICSerial.h"
#include "ProtocolRICFrame.h"
#include "ProtocolRICJSON.h"
#include "RICRESTMsg.h"
#include "RaftJson.h"
#include "CommsBridgeMsg.h"

// Warn
#define WARN_ON_SLOW_PROC_ENDPOINT_MESSAGE
#define WARN_ON_FILE_UPLOAD_FAILED
#define WARN_ON_FILE_STREAM_SESSION_NOT_FOUND
// #define WARN_ON_FILE_STREAM_BLOCK_LENGTH_ZERO

// Debug
// #define DEBUG_ENDPOINT_MESSAGES
// #define DEBUG_ENDPOINT_MESSAGES_DETAIL
// #define DEBUG_RICREST_MESSAGES
// #define DEBUG_RICREST_MESSAGES_DETAIL
// #define DEBUG_RICREST_MESSAGES_RESPONSE
// #define DEBUG_RICREST_MESSAGES_RESPONSE_DETAIL
// #define DEBUG_RICREST_BRIDGE_MESSAGES
// #define DEBUG_FILE_STREAM_SESSIONS
// #define DEBUG_FILE_STREAM_SESSION_MATCHING
// #define DEBUG_RAW_CMD_FRAME
// #define DEBUG_SLOW_PROC_ENDPOINT_MESSAGE_DETAIL

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ProtocolExchange::ProtocolExchange(const char *pModuleName, RaftJsonIF& sysConfig)
    : RaftSysMod(pModuleName, sysConfig)
{
#ifdef DEBUG_ADD_COMMS_CHANNELS
    LOG_I(MODULE_PREFIX, "constructed %s", pModuleName);
#endif
}

ProtocolExchange::~ProtocolExchange()
{
    // Remove sessions
    for (FileStreamSession* pSession : _sessions)
    {
        delete pSession;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loop - called frequently
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ProtocolExchange::loop()
{
    // Service active sessions

    bool isMainFWUpdate = false;
    bool isFileSystemActivity = false;
    bool isStreaming = false;
    for (FileStreamSession* pSession : _sessions)
    {
        // Service the session
        pSession->loop();

        // Update status
        isMainFWUpdate = pSession->isMainFWUpdate();
        isFileSystemActivity = pSession->isFileSystemActivity();
        isStreaming = pSession->isStreaming();

        // Check if the session is inactive
        if (!pSession->isActive())
        {
#ifdef DEBUG_FILE_STREAM_SESSIONS
            LOG_I(MODULE_PREFIX, "loop session inactive name %s channel %d streamID %d pSession %p",
                        pSession->getFileStreamName().c_str(), pSession->getChannelID(), 
                        pSession->getStreamID(), pSession);
#endif            
            // Tidy up inactive session
            _sessions.remove(pSession);
            delete pSession;

            // Must break here so list iteration isn't hampered
            break;
        }
    }

    // Inform SysManager of changes in file/stream and firmware update activity
    bool isActive = isMainFWUpdate || isFileSystemActivity || isStreaming;
    if (_sysManStateIndWasActive != isActive)
    {
        if (_fileStreamActivityHookFn)
            _fileStreamActivityHookFn(isMainFWUpdate, isFileSystemActivity, isStreaming);
        _sysManStateIndWasActive = isActive;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get info JSON
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String ProtocolExchange::getDebugJSON() const
{
    String jsonStr;
    for (FileStreamSession* pSession : _sessions)
    {
        if (jsonStr.length() != 0)
            jsonStr += ",";
        jsonStr += pSession->getDebugJSON();
    }
    return "[" + jsonStr + "]";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Comms Channels
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ProtocolExchange::addCommsChannels(CommsCoreIF& commsCore)
{
    // Debug
#ifdef DEBUG_ADD_COMMS_CHANNELS
    LOG_I(MODULE_PREFIX, "addCommsChannels");
#endif

    // Add support for RICSerial
    ProtocolCodecFactoryHelper ricSerialProtocolDef = { ProtocolRICSerial::getProtocolNameStatic(), 
                        ProtocolRICSerial::createInstance,
                        configGetConfig(), "RICSerial",
                        std::bind(&ProtocolExchange::processEndpointMsg, this, std::placeholders::_1),
                        std::bind(&ProtocolExchange::canProcessEndpointMsg, this) };
    commsCore.addProtocol(ricSerialProtocolDef);

    // Add support for RICFrame
    ProtocolCodecFactoryHelper ricFrameProtocolDef = { ProtocolRICFrame::getProtocolNameStatic(), 
                        ProtocolRICFrame::createInstance, 
                        configGetConfig(), "RICFrame",
                        std::bind(&ProtocolExchange::processEndpointMsg, this, std::placeholders::_1),
                        std::bind(&ProtocolExchange::canProcessEndpointMsg, this) };
    commsCore.addProtocol(ricFrameProtocolDef);

    // Add support for RICJSON
    ProtocolCodecFactoryHelper ricJSONProtocolDef = { ProtocolRICJSON::getProtocolNameStatic(), 
                        ProtocolRICJSON::createInstance, 
                        configGetConfig(), "RICJSON",
                        std::bind(&ProtocolExchange::processEndpointMsg, this, std::placeholders::_1),
                        std::bind(&ProtocolExchange::canProcessEndpointMsg, this) };
    commsCore.addProtocol(ricJSONProtocolDef);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if we can process endpoint message
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ProtocolExchange::canProcessEndpointMsg()
{
    // Check if we can process the message 
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process endpoint message
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ProtocolExchange::processEndpointMsg(CommsChannelMsg &cmdMsg)
{
    // Result
    bool rslt = false;

    // Check configured
    if (!getCommsCore())
        return false;

    // Check if message is received on a bridged channel
    if (getCommsCore()->bridgeHandleOutboundMsg(cmdMsg))
        return true;

    // Handle the command message
    CommsMsgProtocol protocol = cmdMsg.getProtocol();

#ifdef WARN_ON_SLOW_PROC_ENDPOINT_MESSAGE
    uint32_t msgProcStartTimeMs = millis();
#endif

    // Debug
#if defined(DEBUG_ENDPOINT_MESSAGES) || defined(DEBUG_ENDPOINT_MESSAGES_DETAIL)
    debugEndpointMessage(cmdMsg);
#endif

    // Handle ROSSerial
    if (protocol == MSG_PROTOCOL_ROSSERIAL)
    {
        // Not implemented as ROSSERIAL is unused in this direction
    }
    else if (protocol == MSG_PROTOCOL_RICREST)
    {
        // Extract request msg
        RICRESTMsg ricRESTReqMsg;
        ricRESTReqMsg.decode(cmdMsg.getBuf(), cmdMsg.getBufLen());

        // Debug
#if defined(DEBUG_RICREST_MESSAGES) || defined(DEBUG_RICREST_MESSAGES_DETAIL)
        debugRICRESTMessage(cmdMsg, ricRESTReqMsg);
#endif

        // Check elemCode of message
        String respMsg;
        switch(ricRESTReqMsg.getElemCode())
        {
            case RICRESTMsg::RICREST_ELEM_CODE_URL:
            {
                rslt = processRICRESTURL(ricRESTReqMsg, respMsg, APISourceInfo(cmdMsg.getChannelID())) == RAFT_OK;
                if (!rslt && respMsg.isEmpty())
                {
                    Raft::setJsonErrorResult(ricRESTReqMsg.getReq().c_str(), respMsg, "API not found");
                }
                break;
            }
            case RICRESTMsg::RICREST_ELEM_CODE_BODY:
            {
                rslt = processRICRESTBody(ricRESTReqMsg, respMsg, APISourceInfo(cmdMsg.getChannelID())) == RAFT_OK;
                break;
            }
            case RICRESTMsg::RICREST_ELEM_CODE_CMDRESPJSON:
            {
                rslt = processRICRESTCmdRespJSON(ricRESTReqMsg, respMsg, APISourceInfo(cmdMsg.getChannelID())) == RAFT_OK;
                break;
            }
            case RICRESTMsg::RICREST_ELEM_CODE_COMMAND_FRAME:
            {
                rslt = processRICRESTCmdFrame(ricRESTReqMsg, respMsg, cmdMsg) == RAFT_OK;
                break;
            }
            case RICRESTMsg::RICREST_ELEM_CODE_FILEBLOCK:
            {
                rslt = processRICRESTFileStreamBlock(ricRESTReqMsg, respMsg, cmdMsg) == RAFT_OK;;
                break;
            }
        }

        // Check for response
        if (respMsg.length() != 0)
        {
            // Send the response back
            CommsChannelMsg endpointMsg;
            RICRESTMsg::encode(respMsg, endpointMsg, RICRESTMsg::RICREST_ELEM_CODE_CMDRESPJSON);
            endpointMsg.setAsResponse(cmdMsg);

            // Send message on the appropriate channel
            getCommsCore()->outboundHandleMsg(endpointMsg);

            // Debug
#if defined(DEBUG_RICREST_MESSAGES_RESPONSE) || defined(DEBUG_RICREST_MESSAGES_RESPONSE_DETAIL)
            debugRICRESTResponse(endpointMsg);
#endif
        }
    }

    // Bridge protocol
    else if (protocol == MSG_PROTOCOL_BRIDGE_RICREST)
    {
        // Extract request msg
        uint32_t bridgeID = CommsBridgeMsg::getBridgeIdx(cmdMsg.getBuf(), cmdMsg.getBufLen());

        // Extract the payload into a new message
        uint32_t payloadPos = CommsBridgeMsg::getPayloadPos(cmdMsg.getBuf(), cmdMsg.getBufLen());
        if (cmdMsg.getBufLen() <= payloadPos)
        {
            LOG_E(MODULE_PREFIX, "processEndpointMsg bridgeID %d payloadPos %d", bridgeID, payloadPos);
            rslt = false;
        }
        else
        {
            // Decode the message
            CommsChannelMsg bridgeMsg;
            rslt = ProtocolRICSerial::decodeIntoCommsChannelMsg(cmdMsg.getChannelID(), cmdMsg.getBuf()+payloadPos, 
                            cmdMsg.getBufLen()-payloadPos, bridgeMsg);

            // Handle the bridged message
            if (rslt)
                getCommsCore()->bridgeHandleInboundMsg(bridgeID, bridgeMsg);

#ifdef DEBUG_RICREST_BRIDGE_MESSAGES
            // Debug
            LOG_I(MODULE_PREFIX, "processEndpointMsg bridgeID %d", bridgeID);
#endif
        }
    }

    // Raw commands
    else if (protocol == MSG_PROTOCOL_RAWCMDFRAME)
    {
        String cmdMsgStr(cmdMsg.getBuf(), cmdMsg.getBufLen());
        RaftJson cmdFrame = cmdMsgStr;
        String reqStr = cmdFrame.getString("cmdName", "");
        String queryStr = RaftJson::getHTMLQueryFromJSON(cmdMsgStr);
        if (queryStr.length() > 0)
            reqStr += "?" + queryStr;

#ifdef DEBUG_RAW_CMD_FRAME
        LOG_I(MODULE_PREFIX, "processEndpointMsg rawCmdFrame %s", reqStr.c_str());
#endif

        // Handle via standard REST API
        String respMsg;
        if (getRestAPIEndpointManager())
            rslt = getRestAPIEndpointManager()->handleApiRequest(reqStr.c_str(), 
                        respMsg, APISourceInfo(cmdMsg.getChannelID()));
    }

#ifdef WARN_ON_SLOW_PROC_ENDPOINT_MESSAGE
    if (Raft::isTimeout(millis(), msgProcStartTimeMs, MSG_PROC_SLOW_PROC_THRESH_MS))
    {
#ifdef DEBUG_SLOW_PROC_ENDPOINT_MESSAGE_DETAIL
        String msgHex;
        Raft::getHexStrFromBytes(cmdMsg.getBuf(), cmdMsg.getBufLen(), msgHex);
#endif
        LOG_W(MODULE_PREFIX, "processEndpointMsg SLOW took %dms protocol %d len %d msg %s", 
                (int)Raft::timeElapsed(millis(), msgProcStartTimeMs),
                protocol,
                cmdMsg.getBufLen(),
#ifdef DEBUG_SLOW_PROC_ENDPOINT_MESSAGE_DETAIL
                msgHex.c_str()
#else
                ""
#endif
                );
    }
#endif
    return rslt;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process RICRESTMsg URL
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode ProtocolExchange::processRICRESTURL(RICRESTMsg& ricRESTReqMsg, String& respMsg, const APISourceInfo& sourceInfo)
{
    // Handle via standard REST API
    if (getRestAPIEndpointManager())
        return getRestAPIEndpointManager()->handleApiRequest(ricRESTReqMsg.getReq().c_str(), respMsg, sourceInfo);
    return RAFT_NOT_IMPLEMENTED;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process RICRESTMsg URL
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode ProtocolExchange::processRICRESTBody(RICRESTMsg& ricRESTReqMsg, String& respMsg, const APISourceInfo& sourceInfo)
{
    // NOTE - implements POST for RICREST - not currently needed

//     // Handle the body
//     String reqStr;
//     uint32_t bufferPos = ricRESTReqMsg.getBufferPos();
//     const uint8_t* pBuffer = ricRESTReqMsg.getBinBuf();
//     uint32_t bufferLen = ricRESTReqMsg.getBinLen();
//     uint32_t totalBytes = ricRESTReqMsg.getTotalBytes();
//     bool rsltOk = false;
//     if (pBuffer && _pRestAPIEndpointManager)
//     {
//         _pRestAPIEndpointManager->handleApiRequestBody(reqStr, pBuffer, 
                    // bufferLen, bufferPos, totalBytes, sourceInfo);
//         rsltOk = true;
//     }

//     // Response
//     Raft::setJsonBoolResult(pReqStr, respMsg, rsltOk);

//     // Debug
// // #ifdef DEBUG_RICREST_MESSAGES
//     LOG_I(MODULE_PREFIX, "addCommand restBody binBufLen %d bufferPos %d totalBytes %d", 
//                 bufferLen, bufferPos, totalBytes);
// // #endif
    return RAFT_NOT_IMPLEMENTED;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process RICRESTMsg CmdRespJSON
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode ProtocolExchange::processRICRESTCmdRespJSON(RICRESTMsg& ricRESTReqMsg, String& respMsg, 
                const APISourceInfo& sourceInfo)
{
    // Not currently used
    return RAFT_NOT_IMPLEMENTED;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process RICRESTMsg CmdFrame
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode ProtocolExchange::processRICRESTCmdFrame(RICRESTMsg& ricRESTReqMsg, String& respMsg, 
                const CommsChannelMsg &endpointMsg)
{
    // Handle command frames
    RaftJson cmdFrame = ricRESTReqMsg.getPayloadJson();
    String cmdName = cmdFrame.getString("cmdName", "");

    // Get File/Stream message type
    FileStreamBase::FileStreamMsgType fileStreamMsgType = 
                        FileStreamSession::getFileStreamMsgType(ricRESTReqMsg, cmdName);

    // Handle non-file-stream messages
    if (fileStreamMsgType == FileStreamBase::FILE_STREAM_MSG_TYPE_NONE)
        return processRICRESTNonFileStream(cmdName, ricRESTReqMsg, respMsg, endpointMsg);

    // ChannelID
    uint32_t channelID = endpointMsg.getChannelID();

    // Check file/stream messages
    String fileStreamName;
    FileStreamBase::FileStreamContentType fileStreamContentType = FileStreamBase::FILE_STREAM_CONTENT_TYPE_FILE;
    String restAPIEndpointName;
    uint32_t streamID = FileStreamBase::FILE_STREAM_ID_ANY;
    uint32_t fileStreamLength = 0;
    FileStreamBase::getFileStreamMsgInfo(cmdFrame, fileStreamName, 
                            fileStreamContentType, streamID, restAPIEndpointName, fileStreamLength);

    // Handle file stream
    FileStreamSession* pSession = nullptr;
    bool respondToMismatchedSession = true;
    switch (fileStreamMsgType)
    {
        case FileStreamBase::FILE_STREAM_MSG_TYPE_UPLOAD_START:
            pSession = getFileStreamNewSession(fileStreamName.c_str(), channelID, fileStreamContentType, 
                                    restAPIEndpointName.c_str(), 
                                    FileStreamBase::FILE_STREAM_FLOW_TYPE_RICREST_UPLOAD,
                                    fileStreamLength);
            break;
        case FileStreamBase::FILE_STREAM_MSG_TYPE_UPLOAD_END:
            pSession = getFileStreamExistingSession(fileStreamName.c_str(), channelID, streamID);
            // If session already cleaned up (e.g. after final block), respond OK
            // so the client's numbered message gets acknowledged (avoiding timeout)
            if (!pSession)
            {
                Raft::setJsonBoolResult(ricRESTReqMsg.getReq().c_str(), respMsg, true);
                return RAFT_OK;
            }
            break;
        case FileStreamBase::FILE_STREAM_MSG_TYPE_DOWNLOAD_START:
            pSession = getFileStreamNewSession(fileStreamName.c_str(), channelID, fileStreamContentType, 
                                    restAPIEndpointName.c_str(), 
                                    FileStreamBase::FILE_STREAM_FLOW_TYPE_RICREST_DOWNLOAD,
                                    fileStreamLength);
            respondToMismatchedSession = false;
            break;
        case FileStreamBase::FILE_STREAM_MSG_TYPE_DOWNLOAD_END:
            pSession = getFileStreamExistingSession(fileStreamName.c_str(), channelID, streamID);
            break;
        case FileStreamBase::FILE_STREAM_MSG_TYPE_DOWNLOAD_ACK:
            pSession = getFileStreamExistingSession(fileStreamName.c_str(), channelID, streamID);
            // Ignore mismatched ACK frames (which will result in pSession being nullptr)
            respondToMismatchedSession = false;
            break;
        default:
            pSession = getFileStreamExistingSession(fileStreamName.c_str(), channelID, streamID);
            break;
    }

    // Check session is found
    if (!pSession)
    {
        if (respondToMismatchedSession)
        {
            // Failure
            Raft::setJsonBoolResult(ricRESTReqMsg.getReq().c_str(), respMsg, true);
        }

        // Debug
#ifdef WARN_ON_FILE_STREAM_SESSION_NOT_FOUND
        LOG_W(MODULE_PREFIX, "processRICRESTCmdFrame session not found msgType %s streamName %s streamID %d", 
                FileStreamBase::getFileStreamMsgTypeStr(fileStreamMsgType), fileStreamName.c_str(), streamID);
#endif
        return RaftRetCode::RAFT_SESSION_NOT_FOUND;
    }

    // Session is valid so send message to it
    return pSession->handleCmdFrame(fileStreamMsgType, ricRESTReqMsg, respMsg, endpointMsg);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process RICRESTMsg file/stream block
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode ProtocolExchange::processRICRESTFileStreamBlock(const RICRESTMsg& ricRESTReqMsg, 
                    String& respMsg, CommsChannelMsg &cmdMsg)
{
    // Check length
    if (ricRESTReqMsg.getBinLen() == 0)
    {
#ifdef WARN_ON_FILE_STREAM_BLOCK_LENGTH_ZERO
        LOG_W(MODULE_PREFIX, "processRICRESTFileStreamBlock invalid length %d", ricRESTReqMsg.getBinLen());
#endif
        RaftRetCode rslt = RaftRetCode::RAFT_INVALID_DATA;
        char errorMsg[100];
        snprintf(errorMsg, sizeof(errorMsg), "\"length\":%d,\"reason\":\"%s\"", 
                            (int)ricRESTReqMsg.getBinLen(), Raft::getRetCodeStr(rslt));
        Raft::setJsonBoolResult(ricRESTReqMsg.getReq().c_str(), respMsg, false, errorMsg);
        return rslt;
    }

    // Extract streamID
    uint32_t streamID = ricRESTReqMsg.getStreamID();

    // Find corresponding session
    FileStreamSession* pSession = findFileStreamSession(streamID, nullptr, cmdMsg.getChannelID());
    if (!pSession)
    {
        LOG_W(MODULE_PREFIX, "processRICRESTFileStreamBlock session not found for streamID %d", streamID);
        RaftRetCode rslt = RAFT_SESSION_NOT_FOUND;
        char errorMsg[100];
        snprintf(errorMsg, sizeof(errorMsg), "\"streamID\":%d,\"reason\":\"%s\"", 
                            (int)streamID, Raft::getRetCodeStr(rslt));
        Raft::setJsonBoolResult(ricRESTReqMsg.getReq().c_str(), respMsg, false, errorMsg);
        return rslt;
    }

    // Handle message
    return pSession->handleDataFrame(ricRESTReqMsg, respMsg);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process RICRESTMsg CmdFrame that are non-file-stream messages
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode ProtocolExchange::processRICRESTNonFileStream(const String& cmdName, 
                RICRESTMsg& ricRESTReqMsg, String& respMsg, 
                const CommsChannelMsg &endpointMsg)
{
    // Convert to REST API query string - it won't necessarily be a valid query string for external use
    // but works fine for internal use
    String reqStr = cmdName;
    String queryStr = RaftJson::getHTMLQueryFromJSON(ricRESTReqMsg.getPayloadJson());
    if (queryStr.length() > 0)
        reqStr += "?" + queryStr;

    // Handle via standard REST API
    if (getRestAPIEndpointManager())
        return getRestAPIEndpointManager()->handleApiRequest(reqStr.c_str(), 
                            respMsg, APISourceInfo(endpointMsg.getChannelID()));
    return RAFT_INVALID_OBJECT;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process RICRESTMsg CmdFrame that are non-file-stream messages
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileStreamSession* ProtocolExchange::findFileStreamSession(uint32_t streamID, 
                                    const char* fileStreamName, uint32_t channelID)
{
    // First check the case where we know the streamID
    if (streamID != FileStreamBase::FILE_STREAM_ID_ANY)
    {
        for (FileStreamSession* pSession : _sessions)
        {
            if (pSession->getStreamID() == streamID)
                return pSession;
        }
        return nullptr;
    }

    // Check for matching filename and channel
    for (FileStreamSession* pSession : _sessions)
    {
#ifdef DEBUG_FILE_STREAM_SESSION_MATCHING
        LOG_I(MODULE_PREFIX, "findFileStreamSession checking name %s channelID %d streamID %d vs pSession %p %s %d %d", 
                        fileStreamName, channelID, streamID, pSession,
                        pSession ? pSession->getFileStreamName().c_str() : "NONE", 
                        pSession ? pSession->getChannelID() : 0, 
                        pSession ? pSession->getStreamID() : 0);
#endif
        if (pSession && 
                ((!fileStreamName || pSession->getFileStreamName().equals(fileStreamName))) &&
                (pSession->getChannelID() == channelID))
        {
            return pSession;
        }
    }
    return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle file/stream start condition
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileStreamSession* ProtocolExchange::getFileStreamNewSession(const char* fileStreamName, uint32_t channelID, 
                FileStreamBase::FileStreamContentType fileStreamContentType, const char* restAPIEndpointName,
                FileStreamBase::FileStreamFlowType flowType, uint32_t fileStreamLength)
{
    // Check existing sessions
    FileStreamSession* pSession = findFileStreamSession(FileStreamBase::FILE_STREAM_ID_ANY, 
                        fileStreamName, channelID);
    if (pSession)
    {
        // If we find one then ignore this as it is a re-start of an existing session
        LOG_W(MODULE_PREFIX, "getFileStreamNewSession restart existing name %s channelID %d flow %s",
                        fileStreamName, channelID, FileStreamBase::getFileStreamFlowTypeStr(flowType));
        pSession->resetCounters(fileStreamLength);
        return pSession;
    }

    // Check number of sessions
    if (_sessions.size() > MAX_SIMULTANEOUS_FILE_STREAM_SESSIONS)
    {
        // Max sessions already active
        LOG_W(MODULE_PREFIX, "getFileStreamNewSession max active - ignored name %s channelID %d flow %s",
                        fileStreamName, channelID, FileStreamBase::getFileStreamFlowTypeStr(flowType));
        return nullptr;
    }

    // Create new session
    pSession = new FileStreamSession(fileStreamName, channelID, 
                getCommsCore(), _pFirmwareUpdater, 
                fileStreamContentType, flowType, _nextStreamID, 
                restAPIEndpointName, getRestAPIEndpointManager(),
                fileStreamLength);
    if (!pSession)
    {
        LOG_W(MODULE_PREFIX, "getFileStreamNewSession failed to create session name %s channelID %d endpointName %s flow %s",
                        fileStreamName, channelID, restAPIEndpointName, FileStreamBase::getFileStreamFlowTypeStr(flowType));
        return nullptr;
    }

    // Debug
#ifdef DEBUG_FILE_STREAM_SESSIONS
    LOG_I(MODULE_PREFIX, "getFileStreamNewSession name %s channelID %d streamID %d streamType %s endpointName %s flowType %s fileStreamLength %d", 
                        fileStreamName, channelID, _nextStreamID, 
                        FileStreamBase::getFileStreamContentTypeStr(fileStreamContentType), 
                        restAPIEndpointName, FileStreamBase::getFileStreamFlowTypeStr(flowType),
                        fileStreamLength);
#endif

    // Add to session list
    _sessions.push_back(pSession);

    // Bump the stream ID
    _nextStreamID++;
    if (_nextStreamID >= FileStreamBase::FILE_STREAM_ID_MAX)
        _nextStreamID = FileStreamBase::FILE_STREAM_ID_MIN;

    // Session is valid so send message to it
    return pSession;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle file/stream end
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileStreamSession* ProtocolExchange::getFileStreamExistingSession(const char* fileStreamName, uint32_t channelID, uint32_t streamID)
{
    // Find existing session
    FileStreamSession* pSession = findFileStreamSession(streamID, fileStreamName, channelID);
    if (!pSession)
    {
    // Debug
#ifdef DEBUG_FILE_STREAM_SESSIONS
        LOG_I(MODULE_PREFIX, "getFileStreamExistingSession NOT FOUND name %s channelID %d streamID %d", 
                fileStreamName, channelID, streamID);
#endif
        return nullptr;
    }
#ifdef DEBUG_FILE_STREAM_SESSIONS
    LOG_I(MODULE_PREFIX, "getFileStreamExistingSession OK name %s channelID %d streamID %d pSession %p", 
            fileStreamName, channelID, streamID, pSession);
#endif
    return pSession;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle file upload data block
// This function is only called from the FileManager when the fileupload API is used via HTTP
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode ProtocolExchange::handleFileUploadBlock(const String& req, FileStreamBlock& fileStreamBlock, 
            const APISourceInfo& sourceInfo, FileStreamBase::FileStreamContentType fileStreamContentType,
            const char* restAPIEndpointName)
{
    // See if this is the first block
    if (fileStreamBlock.firstBlock)
    {
        // Get a new session
        if (!getFileStreamNewSession(fileStreamBlock.filename, 
                        sourceInfo.channelID, fileStreamContentType, restAPIEndpointName,
                        FileStreamBase::FILE_STREAM_FLOW_TYPE_HTTP_UPLOAD,
                        fileStreamBlock.fileLenValid ? fileStreamBlock.fileLen : fileStreamBlock.contentLen))
            return RAFT_INSUFFICIENT_RESOURCE;
    }

    // Get the session
    FileStreamSession* pSession = getFileStreamExistingSession(fileStreamBlock.filename, sourceInfo.channelID, 
                    FileStreamBase::FILE_STREAM_ID_ANY);
    if (!pSession)
        return RAFT_SESSION_NOT_FOUND;
    
    // Handle the block
    return pSession->fileStreamBlockWrite(fileStreamBlock);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug endpoint message
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ProtocolExchange::debugEndpointMessage(const CommsChannelMsg &cmdMsg)
{
#ifdef DEBUG_ENDPOINT_MESSAGES_DETAIL
    String debugStr;
    static const uint32_t MAX_DEBUG_BYTES_LEN = 40;
    uint32_t numBytes = cmdMsg.getBufLen() > MAX_DEBUG_BYTES_LEN ? MAX_DEBUG_BYTES_LEN : cmdMsg.getBufLen();
    Raft::getHexStrFromBytes(cmdMsg.getBuf(), numBytes, debugStr);
    String msgNumStr;
    if (cmdMsg.getMsgNumber() != 0)
        msgNumStr = String(cmdMsg.getMsgNumber());
    else
        msgNumStr = "Unnumbered";
    LOG_I(MODULE_PREFIX, "processEndpointMsg %s msgNum %s msgType %s len %d data %s%s", 
            CommsChannelMsg::getProtocolAsString(cmdMsg.getProtocol()), 
            msgNumStr.c_str(),
            CommsChannelMsg::getMsgTypeAsString(cmdMsg.getMsgTypeCode()),
            cmdMsg.getBufLen(),
            debugStr.c_str(),
            numBytes < cmdMsg.getBufLen() ? "..." : "");
#elif defined(DEBUG_ENDPOINT_MESSAGES)
    // Debug
    String msgNumStr;
    if (cmdMsg.getMsgNumber() != 0)
        msgNumStr = String(cmdMsg.getMsgNumber());
    else
        msgNumStr = "Unnumbered";
    LOG_I(MODULE_PREFIX, "processEndpointMsg %s msgNum %s msgType %s len %d", 
    		CommsChannelMsg::getProtocolAsString(cmdMsg.getProtocol()), 
            msgNumStr.c_str(),
            CommsChannelMsg::getMsgTypeAsString(cmdMsg.getMsgTypeCode()),
    		cmdMsg.getBufLen());
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug RICREST message
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ProtocolExchange::debugRICRESTMessage(const CommsChannelMsg &cmdMsg, const RICRESTMsg& ricRESTReqMsg)
{
#ifdef DEBUG_RICREST_MESSAGES_DETAIL
    // Debug
    static const uint32_t MAX_DEBUG_BYTES_LEN = 80;
    String debugStr = ricRESTReqMsg.debugMsg(MAX_DEBUG_BYTES_LEN, true);
    LOG_I(MODULE_PREFIX, "processEndpointMsg RICREST elemCode %s len %d data %s", 
                RICRESTMsg::getRICRESTElemCodeStr(ricRESTReqMsg.getElemCode()), 
                cmdMsg.getBufLen(), debugStr.c_str());
#elif defined(DEBUG_RICREST_MESSAGES)
    LOG_I(MODULE_PREFIX, "processEndpointMsg RICREST elemCode %s", 
                RICRESTMsg::getRICRESTElemCodeStr(ricRESTReqMsg.getElemCode()));
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug RICREST response
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ProtocolExchange::debugRICRESTResponse(const CommsChannelMsg &endpointMsg)
{
#ifdef DEBUG_RICREST_MESSAGES_RESPONSE_DETAIL
    // Debug
    static const uint32_t MAX_DEBUG_BYTES_LEN = 80;
    LOG_I(MODULE_PREFIX, "processEndpointMsg RICREST %s", 
                RICRESTMsg::debugResp(endpointMsg, MAX_DEBUG_BYTES_LEN, true).c_str());
#elif defined(DEBUG_RICREST_MESSAGES_RESPONSE)
    static const uint32_t MAX_DEBUG_BYTES_LEN = 10;
    LOG_I(MODULE_PREFIX, "processEndpointMsg RICREST %s", 
                RICRESTMsg::debugResp(endpointMsg, MAX_DEBUG_BYTES_LEN, false).c_str());
#endif
}
