/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// FileDownloadOKTOProtocol tests
//
// Drives the download protocol directly (dfStart, loop(), dfAck, dfEnd) and checks how it sends blocks:
// - as fast as the channel takes them (no fixed time between blocks), within the ack window
// - not while the channel's queue holds a message or its transport isn't ready
// - with the block size the channel offers
// - with smaller blocks when nothing is acknowledged (a channel that can't deliver blocks that big)
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
#include "MiniHDLC.h"
#include "FileStreamBlockOwned.h"
#include "FileDownloadOKTOProtocol.h"

static const uint32_t TEST_CHANNEL_ID = 3;
static const uint32_t STREAM_ID = 5;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CommsCore stand-in: a channel with a block size, a queue and a transport that may not be ready, which records
// the blocks sent on it
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct SentBlock
{
    uint32_t pos;
    uint32_t len;
    uint32_t streamID;
    std::vector<uint8_t> data;
    bool ackedByProtocol;
};

class TestCommsCore : public CommsCoreIF
{
public:
    std::vector<SentBlock> blocks;
    std::vector<String> jsonSent;
    uint32_t blockMax = 5000;
    uint32_t queued = 0;
    bool canAccept = true;
    // Largest block the transport delivers (bigger ones are lost, as a transport that can't send them)
    uint32_t deliverMax = UINT32_MAX;

    virtual uint32_t registerChannel(const char*, const char*, const char*, CommsChannelOutboundHandleMsgFnType,
                CommsChannelOutboundCanAcceptFnType, const CommsChannelSettings*) override { return 0; }
    virtual void addProtocol(ProtocolCodecFactoryHelper&) override {}
    virtual bool inboundCanAccept(uint32_t) override { return true; }
    virtual void inboundHandleMsg(uint32_t, const uint8_t*, uint32_t) override {}
    virtual void inboundHandleMsgVec(uint32_t, const SpiramAwareUint8Vector&) override {}
    virtual uint32_t inboundMsgBlockMax(uint32_t, uint32_t) override { return 1200; }
    virtual bool outboundCanAccept(uint32_t, CommsMsgTypeCode, bool& noConn) override { noConn = false; return canAccept; }
    virtual CommsCoreRetCode outboundHandleMsg(CommsChannelMsg& msg) override
    {
        RICRESTMsg ricMsg;
        if (!ricMsg.decode(msg.getBuf(), msg.getBufLen()))
            return COMMS_CORE_RET_OK;
        if (ricMsg.getElemCode() == RICRESTMsg::RICREST_ELEM_CODE_FILEBLOCK)
        {
            if (ricMsg.getBinLen() <= deliverMax)
                blocks.push_back({ ricMsg.getBufferPos(), ricMsg.getBinLen(), ricMsg.getStreamID(),
                            std::vector<uint8_t>(ricMsg.getBinBuf(), ricMsg.getBinBuf() + ricMsg.getBinLen()),
                            msg.isAckedByProtocol() });
        }
        else
        {
            jsonSent.push_back(ricMsg.getPayloadJson());
        }
        return COMMS_CORE_RET_OK;
    }
    virtual uint32_t outboundMsgBlockMax(uint32_t, uint32_t) override { return blockMax; }
    virtual uint32_t outboundQueuedCount(uint32_t) override { return queued; }
    virtual int32_t getChannelIDByName(const String&, const String&) override { return -1; }
    virtual uint32_t bridgeRegister(const char*, uint32_t, uint32_t, uint32_t) override { return 0; }
    virtual void bridgeUnregister(uint32_t, bool) override {}
    virtual void bridgeHandleInboundMsg(uint32_t, CommsChannelMsg&) override {}
    virtual bool bridgeHandleOutboundMsg(CommsChannelMsg&) override { return false; }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A download in progress: the protocol, the file it reads, and a client that takes blocks in order and acks
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class Download
{
public:
    TestCommsCore commsCore;
    std::vector<uint8_t> contents;
    std::vector<uint8_t> received;
    uint32_t blocksTaken = 0;
    FileDownloadOKTOProtocol protocol;
    String startResp;

    explicit Download(uint32_t fileLen) :
        protocol(nullptr,
            [this](FileStreamBlockOwned& block, uint32_t filePos, uint32_t maxLen) {
                uint32_t len = std::min<uint32_t>(maxLen, contents.size() - filePos);
                block.set("test.bin", contents.size(), filePos, contents.data() + filePos, len,
                            filePos + len >= contents.size(), 0, false, contents.size(), true, filePos == 0);
                return RAFT_OK;
            },
            nullptr, nullptr, &commsCore,
            FileStreamBase::FILE_STREAM_CONTENT_TYPE_FILE, FileStreamBase::FILE_STREAM_FLOW_TYPE_RICREST_DOWNLOAD,
            STREAM_ID, fileLen, "test.bin")
    {
        for (uint32_t i = 0; i < fileLen; i++)
            contents.push_back((uint8_t)(i * 7 + (i >> 8)));
    }

    String command(FileStreamBase::FileStreamMsgType msgType, const String& json)
    {
        CommsChannelMsg msg(TEST_CHANNEL_ID, MSG_PROTOCOL_RICREST, 1, MSG_TYPE_COMMAND);
        RICRESTMsg::encode(json, msg, RICRESTMsg::RICREST_ELEM_CODE_COMMAND_FRAME);
        RICRESTMsg req;
        req.decode(msg.getBuf(), msg.getBufLen());
        String resp;
        protocol.handleCmdFrame(msgType, req, resp, msg);
        return resp;
    }

    // Start a download. Clients ack (okto 0) as soon as they are ready to receive, which starts the blocks
    void start(uint32_t block, uint32_t batch, bool readyAck = true)
    {
        startResp = command(FileStreamBase::FILE_STREAM_MSG_TYPE_DOWNLOAD_START,
                    R"({"cmdName":"dfStart","reqStr":"getFile","fileType":"fs","fileName":"test.bin","batchMsgSize":)" +
                    String(block) + R"(,"batchAckSize":)" + String(batch) + R"(,"crcAt":"end"})");
        if (readyAck)
            clientTakesAndAcks();
    }

    // Run the protocol's loop a number of times (no time passes)
    void loops(uint32_t count)
    {
        for (uint32_t i = 0; i < count; i++)
            protocol.loop();
    }

    // The client takes the blocks sent since it last looked (those in sequence) and acks what it has
    void clientTakesAndAcks()
    {
        for (; blocksTaken < commsCore.blocks.size(); blocksTaken++)
        {
            const SentBlock& block = commsCore.blocks[blocksTaken];
            if (block.pos == received.size())
                received.insert(received.end(), block.data.begin(), block.data.end());
        }
        command(FileStreamBase::FILE_STREAM_MSG_TYPE_DOWNLOAD_ACK,
                    R"({"cmdName":"dfAck","okto":)" + String((uint32_t)received.size()) +
                    R"(,"streamID":)" + String(STREAM_ID) + R"(,"rslt":"ok"})");
    }

    String end()
    {
        return command(FileStreamBase::FILE_STREAM_MSG_TYPE_DOWNLOAD_END,
                    R"({"cmdName":"dfEnd","reqStr":"getFile","fileName":"test.bin","streamID":)" + String(STREAM_ID) + "}");
    }

    String crcHex() const
    {
        char buf[10];
        snprintf(buf, sizeof(buf), "%04x", MiniHDLC::crcUpdateCCITT(MiniHDLC::crcInitCCITT(), contents.data(), contents.size()));
        return buf;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tests
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void testSendsAsFastAsTheChannelAllows()
{
    printf("Running testSendsAsFastAsTheChannelAllows...\n");
    Download down(10 * 1000 + 300);
    down.start(1000, 4);
    TEST_ASSERT(RaftJson(down.startResp).getString("rslt", "") == "ok", "dfStart ok");
    TEST_ASSERT(RaftJson(down.startResp).getInt("batchMsgSize", 0) == 1000, "block size as requested");
    TEST_ASSERT(RaftJson(down.startResp).getInt("fileLen", 0) == 10300, "file length");
    // A batch goes at once (one block a loop, no time between them), then it waits for the ack
    down.loops(3);
    TEST_ASSERT(down.commsCore.blocks.size() == 3, ("a block a loop " + String((int)down.commsCore.blocks.size())).c_str());
    down.loops(20);
    TEST_ASSERT(down.commsCore.blocks.size() == 4, ("waits for the ack after a batch " + String((int)down.commsCore.blocks.size())).c_str());
    TEST_ASSERT(down.commsCore.blocks[3].pos == 3000 && down.commsCore.blocks[3].streamID == STREAM_ID, "block header");
    // Blocks are marked as acknowledged by the protocol (a transport such as BLE then needn't confirm each packet)
    TEST_ASSERT(down.commsCore.blocks[3].ackedByProtocol, "blocks marked as acked by the protocol");
    // Acked: the next batch
    down.clientTakesAndAcks();
    down.loops(20);
    TEST_ASSERT(down.commsCore.blocks.size() == 8, "next batch after the ack");
    down.clientTakesAndAcks();
    down.loops(20);
    TEST_ASSERT(down.commsCore.blocks.size() == 11, "the rest");
    TEST_ASSERT(down.commsCore.blocks[10].len == 300, "last block is the remainder");
    down.clientTakesAndAcks();
    TEST_ASSERT(down.received == down.contents, "file intact");
    TEST_ASSERT(RaftJson(down.end()).getString("crc16", "") == down.crcHex(), "CRC at the end");
}

static void testWaitsForTheChannel()
{
    printf("Running testWaitsForTheChannel...\n");
    Download down(8000);
    down.start(1000, 4);
    // The transport isn't ready (e.g. the serial port is still sending the last frame)
    down.commsCore.canAccept = false;
    down.loops(10);
    TEST_ASSERT(down.commsCore.blocks.empty(), "nothing sent while the transport isn't ready");
    // A message is still in the channel's queue
    down.commsCore.canAccept = true;
    down.commsCore.queued = 1;
    down.loops(10);
    TEST_ASSERT(down.commsCore.blocks.empty(), "nothing sent while the queue holds a message");
    down.commsCore.queued = 0;
    down.loops(1);
    TEST_ASSERT(down.commsCore.blocks.size() == 1, "sent when the channel is ready");
}

static void testBlockSizeFromTheChannel()
{
    printf("Running testBlockSizeFromTheChannel...\n");
    Download down(3000);
    down.commsCore.blockMax = 500;
    down.start(5000, 4);
    TEST_ASSERT(RaftJson(down.startResp).getInt("batchMsgSize", 0) == 500, "the channel's block size");
    down.loops(10);
    TEST_ASSERT(down.commsCore.blocks.size() == 4 && down.commsCore.blocks[0].len == 500, "blocks of that size");
}

static void testSmallerBlocksWhenNothingIsAcked()
{
    printf("Running testSmallerBlocksWhenNothingIsAcked (waits for two ack timeouts)...\n");
    // The channel offers 1000-byte blocks but its transport only delivers up to 300
    Download down(2500);
    down.commsCore.blockMax = 1000;
    down.commsCore.deliverMax = 300;
    down.start(5000, 4);
    down.loops(10);
    TEST_ASSERT(down.commsCore.blocks.empty(), "blocks of 1000 are lost");
    // No ack: after the timeout it goes back to the start with blocks of 500 (lost too), then 250
    std::this_thread::sleep_for(std::chrono::milliseconds(FileDownloadOKTOProtocol::BLOCK_MSGS_TIMEOUT_MS + 100));
    down.loops(10);
    TEST_ASSERT(down.commsCore.blocks.empty(), "blocks of 500 are lost");
    std::this_thread::sleep_for(std::chrono::milliseconds(FileDownloadOKTOProtocol::BLOCK_MSGS_TIMEOUT_MS + 100));
    down.loops(10);
    TEST_ASSERT(down.commsCore.blocks.size() == 4, ("blocks of 250 arrive " + String((int)down.commsCore.blocks.size())).c_str());
    TEST_ASSERT(down.commsCore.blocks.size() == 4 && down.commsCore.blocks[0].pos == 0 && down.commsCore.blocks[0].len == 250,
                "from the start, 250 bytes");
    // The client acks as it receives: the rest follows at that size, and the CRC covers the file once
    for (int i = 0; i < 5; i++)
    {
        down.clientTakesAndAcks();
        down.loops(10);
    }
    TEST_ASSERT(down.received == down.contents, "file intact");
    TEST_ASSERT(RaftJson(down.end()).getString("crc16", "") == down.crcHex(), "CRC at the end");
}

static void testFirstBlockWaitsForTheClient()
{
    printf("Running testFirstBlockWaitsForTheClient (waits for the start time)...\n");
    // Nothing is sent until the client's first ack: a block sent straight after the reply to dfStart can reach a
    // client before it has handled that reply
    Download down(3000);
    down.start(1000, 4, false);
    down.loops(10);
    TEST_ASSERT(down.commsCore.blocks.empty(), "nothing sent before the client's first ack");
    down.clientTakesAndAcks();
    down.loops(10);
    TEST_ASSERT(down.commsCore.blocks.size() == 3, "blocks follow the first ack");
    down.clientTakesAndAcks();
    TEST_ASSERT(down.received == down.contents, "file intact");

    // A client that doesn't ack until it has data: the blocks start after a short wait
    Download quiet(3000);
    quiet.start(1000, 4, false);
    quiet.loops(10);
    TEST_ASSERT(quiet.commsCore.blocks.empty(), "nothing sent at once");
    std::this_thread::sleep_for(std::chrono::milliseconds(FileDownloadOKTOProtocol::START_ACK_WAIT_MS + 50));
    quiet.loops(10);
    TEST_ASSERT(quiet.commsCore.blocks.size() == 3, "blocks start after the wait");
    quiet.clientTakesAndAcks();
    TEST_ASSERT(quiet.received == quiet.contents, "file intact without a first ack");
}

int main()
{
    testFirstBlockWaitsForTheClient();
    testSendsAsFastAsTheChannelAllows();
    testWaitsForTheChannel();
    testBlockSizeFromTheChannel();
    testSmallerBlocksWhenNothingIsAcked();
    return testSummary("test_file_download_okto");
}
