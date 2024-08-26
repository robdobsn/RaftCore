/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// File Upload Protocol
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "FileStreamBase.h"

class FileUploadOKTOProtocol : public FileStreamBase
{
public:
    // Consts
    static const uint32_t FIRST_MSG_TIMEOUT_MS = 
#ifdef IMPLEMENT_RAFT_UPLOAD_OKTO_FIRST_MSG_TIMOUT_MS
        IMPLEMENT_RAFT_UPLOAD_OKTO_FIRST_MSG_TIMOUT_MS;
#else
        5000;
#endif

    static const uint32_t BLOCK_MSGS_TIMEOUT_MS = 1000;
    static const uint32_t MAX_BATCH_BLOCK_ACK_RETRIES = 5;
    static const uint32_t FILE_BLOCK_SIZE_MIN = 20;
    static const uint32_t FILE_BLOCK_SIZE_DEFAULT = 5000;
    static const uint32_t BATCH_ACK_SIZE_DEFAULT = 40;
    static const uint32_t MAX_TOTAL_BYTES_IN_BATCH = 50000;
    // The overall timeout needs to be very big as BLE transfers can take over 30 minutes
    static const uint32_t UPLOAD_FAIL_TIMEOUT_MS = 2 * 3600 * 1000;

    // Constructor
    FileUploadOKTOProtocol(FileStreamBlockWriteFnType fileBlockWrite, 
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
                const CommsChannelMsg &endpointMsg);

    // Handle received file/stream block
    virtual RaftRetCode handleDataFrame(const RICRESTMsg& ricRESTReqMsg, String& respMsg);

    // Get debug str
    virtual String getDebugJSON(bool includeBraces) override final;

    // Is active
    virtual bool isActive() override final
    {
        return _isUploading;
    }

    // Check if message is a file/stream message
    static FileStreamBase::FileStreamMsgType getFileStreamMsgType(const RICRESTMsg& ricRESTReqMsg,
                const String& cmdName)
    {
        if (cmdName.startsWith("uf"))
        {
            if (cmdName.equalsIgnoreCase("ufStart"))
                return FILE_STREAM_MSG_TYPE_UPLOAD_START;
            else if (cmdName.equalsIgnoreCase("ufEnd"))
                return FILE_STREAM_MSG_TYPE_UPLOAD_END;
            else if (cmdName.equalsIgnoreCase("ufCancel"))
                return FILE_STREAM_MSG_TYPE_UPLOAD_CANCEL;
            else if (cmdName.equalsIgnoreCase("ufAck"))
                return FILE_STREAM_MSG_TYPE_UPLOAD_ACK;
        }
        return FILE_STREAM_MSG_TYPE_NONE;
    }

private:
    // Message helpers
    RaftRetCode handleStartMsg(const RICRESTMsg& ricRESTReqMsg, String& respMsg, uint32_t channelID);
    RaftRetCode handleEndMsg(const RICRESTMsg& ricRESTReqMsg, String& respMsg);
    RaftRetCode handleCancelMsg(const RICRESTMsg& ricRESTReqMsg, String& respMsg);

    // Upload state-machine helpers
    void transferService(bool& genAck);
    bool validateFileStreamStart(const String& fileName, uint32_t fileSize, 
            uint32_t channelID, String& respInfo, uint32_t crc16, bool crc16Valid);
    void validateRxBlock(uint32_t filePos, uint32_t blockLen, 
                bool& isFirstBlock, bool& blockValid, bool& isFinalBlock, bool& genAck);
    void transferCancel(const char* reasonStr = nullptr);
    void transferEnd();

    // Helpers
    uint32_t getOkTo();
    double getBlockRate();
    bool checkFinalBlock(uint32_t filePos, uint32_t blockLen);
    bool debugStatsReady();
    String debugStatsStr();
    double statsMsgRate();
    double statsDataRate();
    double statsFinalMsgRate();
    double statsFinalDataRate();
    void statsEndWindow();

    // File info
    uint32_t _fileSize = 0;
    String _fileName;
    uint32_t _expCRC16 = 0;
    bool _expCRC16Valid = false;

    // Upload state
    bool _isUploading = false;
    uint32_t _startMs = 0;
    uint32_t _lastMsgMs = 0;
    uint32_t _commsChannelID = 0;

    // Size of batch and block
    uint32_t _batchAckSize = BATCH_ACK_SIZE_DEFAULT;
    uint32_t _blockSize = FILE_BLOCK_SIZE_DEFAULT;

    // Stats
    uint32_t _blockCount = 0;
    uint32_t _bytesCount = 0;
    uint32_t _blocksInWindow = 0;
    uint32_t _bytesInWindow = 0;
    uint32_t _statsWindowStartMs = millis();
    uint32_t _fileUploadStartMs = 0;

    // Batch handling
    uint32_t _expectedFilePos = 0;
    uint32_t _batchBlockCount = 0;
    uint32_t _batchBlockAckRetry = 0;

    // Debug
    uint32_t _debugLastStatsMs = millis();
    static const uint32_t DEBUG_STATS_MS = 10000;
    bool _debugFinalMsgToSend = false;

    // Debug
    static constexpr const char* MODULE_PREFIX = "FileULOKTO";
};