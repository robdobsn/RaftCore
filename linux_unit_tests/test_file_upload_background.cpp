/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// File uploads written in the background
//
// A FileStreamSession for a file upload hands the blocks to the file system worker (FileSystemWorker) and the
// upload protocol acknowledges blocks only once they are written. The worker runs its jobs when the test calls
// runPending(), so each step can be checked: blocks waiting, written, the acks held back and sent, the file closed
// after the final block, the session ending only then, and a cancelled upload dropping what wasn't written. An
// upload given a CRC16 in ufStart is checked against it once written: a mismatch cancels it (failCRC) and deletes
// the file, and a match is confirmed in the ufEnd reply. A file
// download's blocks are read on the worker too (RAFT_BUSY until read), each one reading the next ahead.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <string>
#include <vector>
#include <unistd.h>
#include "testhdr.h"
#include "ArduinoWString.h"
#include "RaftJson.h"
#include "CommsCoreIF.h"
#include "CommsChannelMsg.h"
#include "RICRESTMsg.h"
#include "FileStreamSession.h"
#include "FileSystemWorker.h"
#include "FileSystem.h"
#include "MiniHDLC.h"

static const uint32_t TEST_CHANNEL_ID = 3;
static const uint32_t BLOCK = 100;
static const uint32_t BATCH = 5;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CommsCore stand-in: records the messages the protocol sends on its own (acks held back, cancels)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class TestCommsCore : public CommsCoreIF
{
public:
    std::vector<String> sent;

    virtual uint32_t registerChannel(const char*, const char*, const char*, CommsChannelOutboundHandleMsgFnType,
                CommsChannelOutboundCanAcceptFnType, const CommsChannelSettings*) override { return 0; }
    virtual void addProtocol(ProtocolCodecFactoryHelper&) override {}
    virtual bool inboundCanAccept(uint32_t) override { return true; }
    virtual void inboundHandleMsg(uint32_t, const uint8_t*, uint32_t) override {}
    virtual void inboundHandleMsgVec(uint32_t, const SpiramAwareUint8Vector&) override {}
    virtual uint32_t inboundMsgBlockMax(uint32_t, uint32_t) override { return 1200; }
    virtual bool outboundCanAccept(uint32_t, CommsMsgTypeCode, bool& noConn) override { noConn = false; return true; }
    virtual CommsCoreRetCode outboundHandleMsg(CommsChannelMsg& msg) override
    {
        RICRESTMsg respMsg;
        if (respMsg.decode(msg.getBuf(), msg.getBufLen()))
            sent.push_back(respMsg.getPayloadJson());
        return COMMS_CORE_RET_OK;
    }
    virtual uint32_t outboundMsgBlockMax(uint32_t, uint32_t defaultSize) override { return defaultSize; }
    virtual int32_t getChannelIDByName(const String&, const String&) override { return -1; }
    virtual uint32_t bridgeRegister(const char*, uint32_t, uint32_t, uint32_t) override { return 0; }
    virtual void bridgeUnregister(uint32_t, bool) override {}
    virtual void bridgeHandleInboundMsg(uint32_t, CommsChannelMsg&) override {}
    virtual bool bridgeHandleOutboundMsg(CommsChannelMsg&) override { return false; }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// An upload to a file in the test's folder
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class Upload
{
public:
    TestCommsCore commsCore;
    std::vector<uint8_t> contents;
    String path;
    FileStreamSession* pSession = nullptr;
    String startResp;

    // crc16Field: the CRC16 given in ufStart (e.g. "0x1234"), or nullptr for none
    Upload(uint32_t fileLen, const char* name, const char* crc16Field = nullptr)
    {
        char cwd[512];
        path = String(getcwd(cwd, sizeof(cwd))) + "/" + name;
        unlink(path.c_str());
        for (uint32_t i = 0; i < fileLen; i++)
            contents.push_back((uint8_t)(i * 7 + i / 251));
        pSession = new FileStreamSession(path, TEST_CHANNEL_ID, &commsCore, nullptr,
                    FileStreamBase::FILE_STREAM_CONTENT_TYPE_FILE, FileStreamBase::FILE_STREAM_FLOW_TYPE_RICREST_UPLOAD,
                    1, "", nullptr, fileLen);
        String startJson = R"({"cmdName":"ufStart","reqStr":"fileupload","fileType":"fs","fileName":")" + path +
                    R"(","fileLen":)" + String(fileLen) + R"(,"batchMsgSize":)" + String(BLOCK) +
                    R"(,"batchAckSize":)" + String(BATCH) +
                    (crc16Field ? String(R"(,"CRC16":")") + crc16Field + "\"" : String("")) + "}";
        CommsChannelMsg msg(TEST_CHANNEL_ID, MSG_PROTOCOL_RICREST, 1, MSG_TYPE_COMMAND);
        RICRESTMsg::encode(startJson, msg, RICRESTMsg::RICREST_ELEM_CODE_COMMAND_FRAME);
        RICRESTMsg req;
        req.decode(msg.getBuf(), msg.getBufLen());
        pSession->handleCmdFrame(FileStreamBase::FILE_STREAM_MSG_TYPE_UPLOAD_START, req, startResp, msg);
    }

    ~Upload()
    {
        delete pSession;
        unlink(path.c_str());
    }

    // CRC16-CCITT of the contents, as filehash reports it, as "0x..."
    String contentsCRC() const
    {
        char crcStr[10];
        snprintf(crcStr, sizeof(crcStr), "0x%04x",
                    MiniHDLC::crcUpdateCCITT(MiniHDLC::crcInitCCITT(), contents.data(), contents.size()));
        return crcStr;
    }

    // Send ufEnd; returns the reply
    String sendEnd()
    {
        String endJson = R"({"cmdName":"ufEnd","reqStr":"fileupload","fileType":"fs","fileName":")" + path +
                    R"(","fileLen":)" + String(contents.size()) + "}";
        CommsChannelMsg msg(TEST_CHANNEL_ID, MSG_PROTOCOL_RICREST, 2, MSG_TYPE_COMMAND);
        RICRESTMsg::encode(endJson, msg, RICRESTMsg::RICREST_ELEM_CODE_COMMAND_FRAME);
        RICRESTMsg req;
        req.decode(msg.getBuf(), msg.getBufLen());
        String resp;
        pSession->handleCmdFrame(FileStreamBase::FILE_STREAM_MSG_TYPE_UPLOAD_END, req, resp, msg);
        return resp;
    }

    // Upload every block, running the worker after each batch; returns the reasons of any cancels sent
    std::vector<String> sendAll()
    {
        std::vector<String> cancels;
        uint32_t numBlocks = (contents.size() + BLOCK - 1) / BLOCK;
        for (uint32_t i = 0; i < numBlocks; i++)
        {
            sendBlock(i);
            fileSystemWorker.runPending();
            pSession->loop();
            for (const String& msg : commsCore.sent)
                if (RaftJson(msg).getString("cmdName", "") == "ufCancel")
                    cancels.push_back(RaftJson(msg).getString("reason", ""));
            commsCore.sent.clear();
        }
        return cancels;
    }

    // Send the block at `blockIdx`; returns the okto in the reply, or -1 if there is none
    int32_t sendBlock(uint32_t blockIdx)
    {
        uint32_t pos = blockIdx * BLOCK;
        uint32_t len = std::min<uint32_t>(BLOCK, contents.size() - pos);
        CommsChannelMsg msg(TEST_CHANNEL_ID, MSG_PROTOCOL_RICREST, 0, MSG_TYPE_COMMAND);
        RICRESTMsg::encodeFileBlock((1 << 24) | pos, contents.data() + pos, len, msg);
        RICRESTMsg req;
        req.decode(msg.getBuf(), msg.getBufLen());
        String resp;
        pSession->handleDataFrame(req, resp);
        if (resp.length() == 0)
            return -1;
        return RaftJson(resp).getInt("okto", -2);
    }

    // Acks sent from loop() (held back until written), as oktos in blocks
    std::vector<int> loopAcks()
    {
        std::vector<int> out;
        for (const String& msg : commsCore.sent)
            out.push_back(RaftJson(msg).getInt("okto", -1) / (int)BLOCK);
        commsCore.sent.clear();
        return out;
    }

    std::vector<uint8_t> fileContents() const
    {
        std::vector<uint8_t> out;
        FILE* pFile = fopen(path.c_str(), "rb");
        if (!pFile)
            return out;
        int ch;
        while ((ch = fgetc(pFile)) != EOF)
            out.push_back((uint8_t)ch);
        fclose(pFile);
        return out;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tests
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void testBlocksWrittenByWorkerThenAcked()
{
    printf("Running testBlocksWrittenByWorkerThenAcked...\n");
    Upload up(12 * BLOCK, "bg_upload_1.bin");
    TEST_ASSERT(RaftJson(up.startResp).getString("rslt", "") == "ok", "ufStart ok");

    // The first block waits for the worker: no ack, nothing in the file yet
    TEST_ASSERT(up.sendBlock(0) == -1, "first block not acked before it is written");
    TEST_ASSERT(up.pSession->getWriteStatus() == RAFT_BUSY, "write pending");
    TEST_ASSERT(up.pSession->getWritePendingBytes() == BLOCK, "one block waiting");
    TEST_ASSERT(fileSystemWorker.jobsOutstanding() == 1, "one worker job");
    up.pSession->loop();
    TEST_ASSERT(up.loopAcks().empty(), "no ack while writing");

    // The worker writes it, then the ack is sent
    TEST_ASSERT(fileSystemWorker.runPending() == 1, "job run");
    TEST_ASSERT(up.pSession->getWriteStatus() == RAFT_OK, "written");
    up.pSession->loop();
    TEST_ASSERT(up.loopAcks() == std::vector<int>({1}), "first block acked once written");

    // A batch arrives while the worker hasn't run: one job writes it all, then one ack
    for (uint32_t i = 1; i <= 5; i++)
        up.sendBlock(i);
    TEST_ASSERT(up.pSession->getWritePendingBytes() == 5 * BLOCK, "batch waiting");
    TEST_ASSERT(fileSystemWorker.jobsOutstanding() == 1, "one job for the batch");
    fileSystemWorker.runPending();
    up.pSession->loop();
    TEST_ASSERT(up.loopAcks() == std::vector<int>({6}), "batch acked once written");

    // The rest, including the final block: the session stays active until the file is written and closed
    for (uint32_t i = 6; i < 12; i++)
        up.sendBlock(i);
    up.pSession->loop();
    TEST_ASSERT(up.pSession->isActive(), "active while the final block is waiting");
    fileSystemWorker.runPending();
    up.pSession->loop();
    std::vector<int> acks = up.loopAcks();
    TEST_ASSERT(!acks.empty() && acks.back() == 12, "final block acked once written");
    TEST_ASSERT(up.fileContents() == up.contents, "file contents");
    up.pSession->loop();
    TEST_ASSERT(!up.pSession->isActive(), "session ends once the final block is written");
}

static void testCancelDropsUnwritten()
{
    printf("Running testCancelDropsUnwritten...\n");
    Upload up(12 * BLOCK, "bg_upload_2.bin");
    up.sendBlock(0);
    fileSystemWorker.runPending();
    up.pSession->loop();
    up.loopAcks();
    for (uint32_t i = 1; i <= 3; i++)
        up.sendBlock(i);

    // Cancelled with blocks waiting: still active (not removed) until the worker job ends
    up.pSession->fileStreamCancelEnd(false);
    TEST_ASSERT(up.pSession->isActive(), "kept while the job is outstanding");
    fileSystemWorker.runPending();
    TEST_ASSERT(!up.pSession->isActive(), "inactive once the job ends");
    // Removing the session closes the file: only the block written before the cancel is in it
    delete up.pSession;
    up.pSession = nullptr;
    TEST_ASSERT(up.fileContents().size() == BLOCK, "blocks waiting at the cancel not written");
}

static void testCRCChecked()
{
    printf("Running testCRCChecked...\n");

    // The right CRC: the upload completes and ufEnd confirms the CRC checked
    {
        Upload probe(12 * BLOCK - 30, "bg_upload_crc.bin");
        String crc = probe.contentsCRC();
        delete probe.pSession;
        probe.pSession = nullptr;
        Upload up(12 * BLOCK - 30, "bg_upload_crc.bin", crc.c_str());
        TEST_ASSERT(up.sendAll().empty(), "no cancel with the right CRC");
        TEST_ASSERT(up.fileContents() == up.contents, "file written");
        String endResp = up.sendEnd();
        TEST_ASSERT(RaftJson(endResp).getString("rslt", "") == "ok", "ufEnd ok");
        TEST_ASSERT(String("0x") + RaftJson(endResp).getString("crc16", "") == crc, "ufEnd gives the CRC checked");
    }

    // A wrong CRC: cancelled once the final block is written (failCRC), and the file deleted
    {
        Upload up(12 * BLOCK - 30, "bg_upload_crc.bin", "0x1234");
        std::vector<String> cancels = up.sendAll();
        TEST_ASSERT(cancels == std::vector<String>({"failCRC"}), "cancelled with failCRC");
        TEST_ASSERT(up.fileContents().empty(), "file deleted");
    }

    // No CRC given: no check, and none in the ufEnd reply
    {
        Upload up(3 * BLOCK, "bg_upload_crc.bin");
        TEST_ASSERT(up.sendAll().empty(), "no cancel without a CRC");
        TEST_ASSERT(RaftJson(up.sendEnd()).getString("crc16", "none") == "none", "no CRC in ufEnd");
    }
}

static void testWorkerRunsJobsInOrder()
{
    printf("Running testWorkerRunsJobsInOrder...\n");
    std::vector<int> order;
    for (int i = 0; i < 3; i++)
        TEST_ASSERT(fileSystemWorker.submit([&order, i]() { order.push_back(i); }), "submitted");
    TEST_ASSERT(fileSystemWorker.jobsOutstanding() == 3, "three outstanding");
    TEST_ASSERT(fileSystemWorker.runPending() == 3 && order == std::vector<int>({0, 1, 2}), "run in order");
    TEST_ASSERT(fileSystemWorker.jobsOutstanding() == 0, "none outstanding");

    // A full queue refuses more
    uint32_t accepted = 0;
    for (uint32_t i = 0; i < FileSystemWorker::QUEUE_LEN + 2; i++)
        accepted += fileSystemWorker.submit([]() {}) ? 1 : 0;
    TEST_ASSERT(accepted == FileSystemWorker::QUEUE_LEN, "queue length limit");
    fileSystemWorker.runPending();
}

static void testDownloadBlocksReadByWorker()
{
    printf("Running testDownloadBlocksReadByWorker...\n");
    char cwd[512];
    String path = String(getcwd(cwd, sizeof(cwd))) + "/bg_download.bin";
    std::vector<uint8_t> contents;
    for (uint32_t i = 0; i < 1000; i++)
        contents.push_back((uint8_t)(i * 13 + i / 7));
    FILE* pFile = fopen(path.c_str(), "wb");
    fwrite(contents.data(), 1, contents.size(), pFile);
    fclose(pFile);

    TestCommsCore commsCore;
    FileStreamSession* pSession = new FileStreamSession(path, TEST_CHANNEL_ID, &commsCore, nullptr,
                FileStreamBase::FILE_STREAM_CONTENT_TYPE_FILE, FileStreamBase::FILE_STREAM_FLOW_TYPE_RICREST_DOWNLOAD,
                1, "", nullptr, 0);

    // Blocks of 400: each is BUSY until the worker reads it; the next one is read ahead
    std::vector<uint8_t> received;
    uint32_t busyCount = 0;
    bool finalSeen = false;
    for (int guard = 0; (guard < 20) && !finalSeen; guard++)
    {
        FileStreamBlockOwned block;
        RaftRetCode rslt = pSession->fileStreamBlockRead(block, received.size(), 400);
        if (rslt == RAFT_BUSY)
        {
            busyCount++;
            TEST_ASSERT(pSession->isActive(), "active while a read is outstanding");
            fileSystemWorker.runPending();
            continue;
        }
        TEST_ASSERT(rslt == RAFT_OK, "block read");
        received.insert(received.end(), block.getBlockData(), block.getBlockData() + block.getBlockLen());
        finalSeen = block.finalBlock;
    }
    TEST_ASSERT(received == contents, "file read in blocks");
    TEST_ASSERT(busyCount == 3, "each block waits for the worker once");

    // A block asked for again (a resend) is read again
    FileStreamBlockOwned block;
    TEST_ASSERT(pSession->fileStreamBlockRead(block, 400, 400) == RAFT_BUSY, "resend read on the worker");
    fileSystemWorker.runPending();
    TEST_ASSERT(pSession->fileStreamBlockRead(block, 400, 400) == RAFT_OK && block.getBlockLen() == 400 &&
                std::equal(block.getBlockData(), block.getBlockData() + 400, contents.begin() + 400), "resent block");
    fileSystemWorker.runPending();
    delete pSession;
    unlink(path.c_str());
}

int main()
{
    // A local file system (a folder on Linux) - the test's files are given by absolute path
    fileSystem.setup(FileSystem::LOCAL_FS_SPIFFS, false, false, -1, -1, -1, -1, false, false, false);
    testWorkerRunsJobsInOrder();
    testBlocksWrittenByWorkerThenAcked();
    testCancelDropsUnwritten();
    testCRCChecked();
    testDownloadBlocksReadByWorker();
    return testSummary("test_file_upload_background");
}
