/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// File Download Protocol
//
// Rob Dobson 2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "FileStreamBase.h"
#include <ArduinoTime.h>

class JSONParams;

class FileDownloadOKTOProtocol : public FileStreamBase
{
public:
    // Consts
    static const uint32_t MIN_TIME_BETWEEN_BLOCKS_MS = 100;
    static const uint32_t BLOCK_MSGS_TIMEOUT_MS = 3000;
    static const uint32_t MAX_BATCH_BLOCK_ACK_RETRIES = 5;
    static const uint32_t FILE_BLOCK_SIZE_MIN = 20;
    static const uint32_t FILE_BLOCK_SIZE_DEFAULT = 5000;
    static const uint32_t BATCH_ACK_SIZE_DEFAULT = 40;
    static const uint32_t MAX_TOTAL_BYTES_IN_BATCH = 50000;
    // The overall timeout needs to be very big as BLE transfer can take over 30 minutes
    static const uint32_t DOWNLOAD_FAIL_TIMEOUT_MS = 2 * 3600 * 1000;

    // Constructor
    FileDownloadOKTOProtocol(FileStreamBlockWriteCB fileBlockWriteCB, 
            FileStreamBlockReadCB fileBlockReadCB, 
            FileStreamGetCRCCB fileGetCRCCB,
            FileStreamCancelEndCB fileRxCancelCB,
            CommsCoreIF* pCommsCoreIF,
            FileStreamBase::FileStreamContentType fileStreamContentType, 
            FileStreamBase::FileStreamFlowType fileStreamFlowType,
            uint32_t streamID, 
            uint32_t fileStreamLength,
            const char* fileStreamName);

    // Service
    void service();

    // Handle command frame
    virtual UtilsRetCode::RetCode handleCmdFrame(FileStreamBase::FileStreamMsgType fsMsgType, 
                const RICRESTMsg& ricRESTReqMsg, String& respMsg, 
                const CommsChannelMsg &endpointMsg) override final;

    // Handle received file/stream block
    virtual UtilsRetCode::RetCode handleDataFrame(const RICRESTMsg& ricRESTReqMsg, String& respMsg) override final;

    // Get debug str
    virtual String getDebugJSON(bool includeBraces) override final;

    // Is active
    virtual bool isActive() override final
    {
        return _isDownloading;
    }

    // Check if message is a file/stream message
    static FileStreamBase::FileStreamMsgType getFileStreamMsgType(const RICRESTMsg& ricRESTReqMsg,
                const String& cmdName)
    {
        if (cmdName.startsWith("df"))
        {
            if (cmdName.equalsIgnoreCase("dfStart"))
                return FILE_STREAM_MSG_TYPE_DOWNLOAD_START;
            else if (cmdName.equalsIgnoreCase("dfEnd"))
                return FILE_STREAM_MSG_TYPE_DOWNLOAD_END;
            else if (cmdName.equalsIgnoreCase("dfCancel"))
                return FILE_STREAM_MSG_TYPE_DOWNLOAD_CANCEL;
            else if (cmdName.equalsIgnoreCase("dfAck"))
                return FILE_STREAM_MSG_TYPE_DOWNLOAD_ACK;
        }
        return FILE_STREAM_MSG_TYPE_NONE;
    }

private:
    // Message helpers
    UtilsRetCode::RetCode handleStartMsg(const RICRESTMsg& ricRESTReqMsg, String& respMsg, uint32_t channelID);
    UtilsRetCode::RetCode handleEndMsg(const RICRESTMsg& ricRESTReqMsg, String& respMsg);
    UtilsRetCode::RetCode handleCancelMsg(const RICRESTMsg& ricRESTReqMsg, String& respMsg);
    UtilsRetCode::RetCode handleAckMsg(const RICRESTMsg& ricRESTReqMsg, String& respMsg);

    // State-machine helpers
    void transferService();
    bool validateFileStreamStart(const String& fileName,
                    uint32_t channelID, String& respInfo, uint32_t& crc16, bool& crc16Valid, uint32_t& fileSize);
    void transferCancel(const char* reasonStr = nullptr);
    void transferEnd();

    // Helpers
    void sendBlock(FileStreamBlockOwned& block);
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

    // State
    bool _isDownloading = false;
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
    uint32_t _fileDownloadStartMs = 0;

    // Batch handling
    uint32_t _oktoFilePos = 0;
    uint32_t _lastBatchAckRxOrRetryMs = 0;
    uint32_t _lastSentUptoFilePos = 0;
    uint32_t _batchBlockSendRetryCount = 0;

    // Debug
    uint32_t _debugLastStatsMs = millis();
    static const uint32_t DEBUG_STATS_MS = 10000;
    bool _debugFinalMsgToSend = false;
    uint32_t _betweenBlocksMs = 0;
};
