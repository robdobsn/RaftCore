/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// FileUploadOKTOProtocol tests
//
// Drives the upload protocol directly (ufStart, then file blocks) and checks when it acks (okto): after the first
// block accepted, after each batch, after the last block, at once for the first block out of sequence (after a
// lost block, or one sent again), and on its timer when blocks stop - and that its acks stay in step with the
// host's batches after a loss.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <string>
#include <thread>
#include <vector>
#include "testhdr.h"
#include "ArduinoWString.h"
#include "RaftJson.h"
#include "CommsCoreIF.h"
#include "CommsChannelMsg.h"
#include "RICRESTMsg.h"
#include "FileStreamBlock.h"
#include "FileUploadOKTOProtocol.h"

static const uint32_t TEST_CHANNEL_ID = 3;
static const uint32_t STREAM_ID = 1;
static const uint32_t BLOCK = 100;
static const uint32_t BATCH = 5;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CommsCore stand-in: the channel's block size, and the messages the protocol sends on its own (timer acks)
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
// An upload in progress: the protocol, the file it writes and the acks it gives
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class Upload
{
public:
    TestCommsCore commsCore;
    std::vector<uint8_t> written;
    std::vector<uint8_t> contents;
    FileUploadOKTOProtocol protocol;

    explicit Upload(uint32_t fileLen) :
        protocol(
            [this](FileStreamBlock& block) {
                if (written.size() < block.filePos + block.blockLen)
                    written.resize(block.filePos + block.blockLen);
                std::copy(block.pBlock, block.pBlock + block.blockLen, written.begin() + block.filePos);
                return RAFT_OK;
            },
            nullptr, nullptr, nullptr, &commsCore,
            FileStreamBase::FILE_STREAM_CONTENT_TYPE_FILE, FileStreamBase::FILE_STREAM_FLOW_TYPE_RICREST_UPLOAD,
            STREAM_ID, fileLen, "test.bin")
    {
        for (uint32_t i = 0; i < fileLen; i++)
            contents.push_back((uint8_t)(i * 7));
        String startJson = R"({"cmdName":"ufStart","reqStr":"fileupload","fileType":"fs","fileName":"test.bin","fileLen":)" +
                    String(fileLen) + R"(,"batchMsgSize":)" + String(BLOCK) + R"(,"batchAckSize":)" + String(BATCH) + "}";
        CommsChannelMsg msg(TEST_CHANNEL_ID, MSG_PROTOCOL_RICREST, 1, MSG_TYPE_COMMAND);
        RICRESTMsg::encode(startJson, msg, RICRESTMsg::RICREST_ELEM_CODE_COMMAND_FRAME);
        RICRESTMsg req;
        req.decode(msg.getBuf(), msg.getBufLen());
        String resp;
        protocol.handleCmdFrame(FileStreamBase::FILE_STREAM_MSG_TYPE_UPLOAD_START, req, resp, msg);
        startResp = resp;
    }

    // Send the block at `blockIdx`; returns the okto acked in reply, or -1 if there is no ack
    int32_t sendBlock(uint32_t blockIdx)
    {
        uint32_t pos = blockIdx * BLOCK;
        uint32_t len = std::min<uint32_t>(BLOCK, contents.size() - pos);
        CommsChannelMsg msg(TEST_CHANNEL_ID, MSG_PROTOCOL_RICREST, 0, MSG_TYPE_COMMAND);
        RICRESTMsg::encodeFileBlock((STREAM_ID << 24) | pos, contents.data() + pos, len, msg);
        RICRESTMsg req;
        req.decode(msg.getBuf(), msg.getBufLen());
        String resp;
        protocol.handleDataFrame(req, resp);
        if (resp.length() == 0)
            return -1;
        return RaftJson(resp).getInt("okto", -2);
    }

    // The acks given for a run of blocks, as "blockIdx:okto" for each block that was acked
    String acksFor(const std::vector<uint32_t>& blockIdxs)
    {
        String out;
        for (uint32_t idx : blockIdxs)
        {
            int32_t okto = sendBlock(idx);
            if (okto >= 0)
                out += (out.length() ? " " : "") + String(idx) + ":" + String(okto / (int32_t)BLOCK);
        }
        return out;
    }

    String startResp;
};

static std::vector<uint32_t> range(uint32_t from, uint32_t to)
{
    std::vector<uint32_t> out;
    for (uint32_t i = from; i < to; i++)
        out.push_back(i);
    return out;
}

static std::vector<uint32_t> without(std::vector<uint32_t> blocks, uint32_t lost)
{
    blocks.erase(std::remove(blocks.begin(), blocks.end(), lost), blocks.end());
    return blocks;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tests (okto values are shown in blocks)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void testInOrder()
{
    printf("Running testInOrder...\n");
    Upload up(23 * BLOCK);
    TEST_ASSERT(RaftJson(up.startResp).getString("rslt", "") == "ok", "ufStart ok");
    TEST_ASSERT(RaftJson(up.startResp).getInt("batchMsgSize", 0) == (int)BLOCK, "block size as requested");
    // The first block, then each batch of 5, then the last
    String acks = up.acksFor(range(0, 23));
    TEST_ASSERT(acks == "0:1 5:6 10:11 15:16 20:21 22:23", ("acks " + acks).c_str());
    TEST_ASSERT(up.written == up.contents, "file intact");
}

static void testLostBlockAckedAtOnceAndInStep()
{
    printf("Running testLostBlockAckedAtOnceAndInStep...\n");
    Upload up(30 * BLOCK);
    up.acksFor(range(0, 6));
    // Block 7 lost: block 8 is acked at once with 7 (one ack, not one per block after the gap)
    String acks = up.acksFor(without(range(6, 11), 7));
    TEST_ASSERT(acks == "8:7", ("ack on the gap " + acks).c_str());
    // Sending again from 7: the next ack comes at the end of that batch, in step with it
    acks = up.acksFor(range(7, 12));
    TEST_ASSERT(acks == "11:12", ("in step after the gap " + acks).c_str());
    acks = up.acksFor(range(12, 30));
    TEST_ASSERT(acks == "16:17 21:22 26:27 29:30", ("rest " + acks).c_str());
    TEST_ASSERT(up.written == up.contents, "file intact");
}

static void testSecondBlockLost()
{
    printf("Running testSecondBlockLost...\n");
    Upload up(20 * BLOCK);
    // Block 1 lost: before the change every block after it was acked (only one block accepted counted as "first")
    String acks = up.acksFor(without(range(0, 6), 1));
    TEST_ASSERT(acks == "0:1 2:1", ("acks " + acks).c_str());
    acks = up.acksFor(range(1, 20));
    TEST_ASSERT(acks == "5:6 10:11 15:16 19:20", ("rest " + acks).c_str());
    TEST_ASSERT(up.written == up.contents, "file intact");
}

static void testBlockSentAgain()
{
    printf("Running testBlockSentAgain...\n");
    Upload up(20 * BLOCK);
    up.acksFor(range(0, 4));
    // A block already accepted, sent again: acked at once with the position wanted
    String acks = up.acksFor({2, 3});
    TEST_ASSERT(acks == "2:4", ("acks " + acks).c_str());
    acks = up.acksFor(range(4, 20));
    TEST_ASSERT(acks == "8:9 13:14 18:19 19:20", ("rest " + acks).c_str());
    TEST_ASSERT(up.written == up.contents, "file intact");
}

static void testTimerAckStartsTheBatchAgain()
{
    printf("Running testTimerAckStartsTheBatchAgain...\n");
    Upload up(30 * BLOCK);
    up.acksFor(range(0, 3));
    // Blocks stop part-way through a batch: after 1s the timer acks
    std::this_thread::sleep_for(std::chrono::milliseconds(FileUploadOKTOProtocol::BLOCK_MSGS_TIMEOUT_MS + 100));
    up.protocol.loop();
    TEST_ASSERT(up.commsCore.sent.size() == 1, "timer ack sent");
    TEST_ASSERT(up.commsCore.sent.size() == 1 && RaftJson(up.commsCore.sent[0]).getInt("okto", -1) == (int)(3 * BLOCK),
                "timer ack okto");
    // The host sends a batch from there: acked at its end, not part-way through
    String acks = up.acksFor(range(3, 13));
    TEST_ASSERT(acks == "7:8 12:13", ("in step after the timer ack " + acks).c_str());
}

int main()
{
    testInOrder();
    testLostBlockAckedAtOnceAndInStep();
    testSecondBlockLost();
    testBlockSentAgain();
    testTimerAckStartsTheBatchAgain();
    return testSummary("test_file_upload_okto");
}
