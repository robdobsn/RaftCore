/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ProtocolExchange tests
//
// Drives ProtocolExchange through its RICREST message handler (as CommsCore would) to check the file/stream
// session lifecycle and the file stream activity hook, which SysManager uses to pause other work while a
// file, firmware or stream transfer is in progress.
//
// A real-time stream session is used because it needs no file system or firmware updater: it delivers
// blocks to a REST API endpoint's chunk callback.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <string>
#include <vector>
#include "testhdr.h"
#include "ArduinoWString.h"
#include "RaftJson.h"
#include "ProtocolExchange.h"
#include "ProtocolCodecFactoryHelper.h"
#include "CommsCoreIF.h"
#include "CommsChannelMsg.h"
#include "RICRESTMsg.h"
#include "RestAPIEndpointManager.h"
#include "SysManagerIF.h"
#include "FileStreamBlock.h"

static const uint32_t TEST_CHANNEL_ID = 7;
static const char* TEST_STREAM_ENDPOINT = "teststream";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CommsCore stand-in: captures the message handler ProtocolExchange registers and the responses it sends
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class TestCommsCore : public CommsCoreIF
{
public:
    CommsChannelInboundHandleMsgFnType ricSerialHandler;
    std::vector<String> responses;

    virtual uint32_t registerChannel(const char*, const char*, const char*, CommsChannelOutboundHandleMsgFnType,
                CommsChannelOutboundCanAcceptFnType, const CommsChannelSettings*) override { return 0; }
    virtual void addProtocol(ProtocolCodecFactoryHelper& protocolDef) override
    {
        if (protocolDef.protocolName == "RICSerial")
            ricSerialHandler = protocolDef.frameRxCB;
    }
    virtual bool inboundCanAccept(uint32_t) override { return true; }
    virtual void inboundHandleMsg(uint32_t, const uint8_t*, uint32_t) override {}
    virtual void inboundHandleMsgVec(uint32_t, const SpiramAwareUint8Vector&) override {}
    virtual uint32_t inboundMsgBlockMax(uint32_t, uint32_t defaultSize) override { return defaultSize; }
    virtual bool outboundCanAccept(uint32_t, CommsMsgTypeCode, bool& noConn) override { noConn = false; return true; }
    virtual CommsCoreRetCode outboundHandleMsg(CommsChannelMsg& msg) override
    {
        // Responses are RICREST CMDRESPJSON messages - keep the JSON
        RICRESTMsg respMsg;
        if (respMsg.decode(msg.getBuf(), msg.getBufLen()))
            responses.push_back(respMsg.getPayloadJson());
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
// SysManager stand-in: provides the CommsCore and REST API endpoint manager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class TestSysManager : public SysManagerIF
{
public:
    TestSysManager(CommsCoreIF& commsCore, RestAPIEndpointManager& restAPI) : _commsCore(commsCore), _restAPI(restAPI) {}
    virtual RaftSysMod* getSysMod(const char*) const override { return nullptr; }
    virtual void addManagedSysMod(RaftSysMod*) override {}
    virtual void setStatusChangeCB(const char*, SysMod_statusChangeCB) override {}
    virtual String getStatusJSON(const char*) const override { return "{}"; }
    virtual RaftRetCode sendCmdJSON(const char*, const char*, String*) override { return RAFT_OK; }
    virtual uint16_t registerDataSource(const char*, const char*, SysMod_publishMsgGenFn, SysMod_stateDetectCB) override { return 0; }
    virtual void systemRestart() override {}
    virtual RestAPIEndpointManager* getRestAPIEndpointManager() override { return &_restAPI; }
    virtual CommsCoreIF* getCommsCore() override { return &_commsCore; }
    virtual ProtocolExchange* getProtocolExchange() override { return nullptr; }
    virtual DeviceManager* getDeviceManager() override { return nullptr; }
    virtual SupervisorStats* getStats() override { return nullptr; }
    virtual double getNamedValue(const char*, const char*, bool& isValid) const override { isValid = false; return 0; }
    virtual bool setNamedValue(const char*, const char*, double) override { return false; }
    virtual String getNamedString(const char*, const char*, bool& isValid) const override { isValid = false; return ""; }
    virtual bool setNamedString(const char*, const char*, const char*) override { return false; }
private:
    CommsCoreIF& _commsCore;
    RestAPIEndpointManager& _restAPI;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct HookCall
{
    bool isMainFWUpdate;
    bool isFileSystemActivity;
    bool isStreaming;
};

static bool sendCmdFrame(TestCommsCore& commsCore, uint32_t msgNum, const String& cmdFrameJson)
{
    CommsChannelMsg msg(TEST_CHANNEL_ID, MSG_PROTOCOL_RICREST, msgNum, MSG_TYPE_COMMAND);
    RICRESTMsg::encode(cmdFrameJson, msg, RICRESTMsg::RICREST_ELEM_CODE_COMMAND_FRAME);
    return commsCore.ricSerialHandler(msg);
}

static bool sendFileBlock(TestCommsCore& commsCore, uint32_t msgNum, uint32_t streamID, uint32_t filePos,
            const std::vector<uint8_t>& data)
{
    CommsChannelMsg msg(TEST_CHANNEL_ID, MSG_PROTOCOL_RICREST, msgNum, MSG_TYPE_COMMAND);
    RICRESTMsg::encodeFileBlock((streamID << 24) | filePos, data.data(), data.size(), msg);
    return commsCore.ricSerialHandler(msg);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tests
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Start a real-time stream, send its only block, and check the session and the activity hook throughout
static void testStreamSessionAndActivityHook()
{
    printf("Running testStreamSessionAndActivityHook...\n");

    TestCommsCore commsCore;
    RestAPIEndpointManager restAPI;
    TestSysManager sysManager(commsCore, restAPI);
    RaftSysMod::setSysManager(&sysManager);

    // Stream endpoint: records the start request and every block delivered
    std::vector<String> startRequests;
    std::vector<uint8_t> receivedData;
    bool finalBlockSeen = false;
    restAPI.addEndpoint(TEST_STREAM_ENDPOINT, RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
            [&](const String& reqStr, String& respStr, const APISourceInfo&) {
                startRequests.push_back(reqStr);
                return RAFT_OK;
            },
            "test stream", nullptr, nullptr, RestAPIEndpoint::ENDPOINT_CACHE_NEVER, nullptr, nullptr,
            [&](const String&, FileStreamBlock& block, const APISourceInfo&) {
                receivedData.insert(receivedData.end(), block.pBlock, block.pBlock + block.blockLen);
                finalBlockSeen = finalBlockSeen || block.finalBlock;
                return RAFT_OK;
            },
            [](const APISourceInfo&) { return true; });

    // Protocol exchange, connected to the CommsCore stand-in, with the activity hook recorded
    RaftJson config("{}");
    ProtocolExchange protocolExchange("ProtExchg", config);
    std::vector<HookCall> hookCalls;
    protocolExchange.setFileStreamActivityHook([&](bool isMainFWUpdate, bool isFileSystemActivity, bool isStreaming) {
        hookCalls.push_back({isMainFWUpdate, isFileSystemActivity, isStreaming});
    });
    RaftSysMod& sysMod = protocolExchange;
    sysMod.addCommsChannels(commsCore);
    TEST_ASSERT(commsCore.ricSerialHandler != nullptr, "RICSerial message handler registered");
    if (!commsCore.ricSerialHandler)
        return;

    // Idle: no sessions, so the hook is not called
    sysMod.loop();
    TEST_ASSERT(hookCalls.empty(), "no hook call while idle");

    // Start a 4-byte real-time stream
    String startFrame = String("{\"cmdName\":\"ufStart\",\"reqStr\":\"ufStart\",\"fileType\":\"rtstream\",") +
            "\"fileName\":\"teststream1\",\"endpoint\":\"" + TEST_STREAM_ENDPOINT + "\",\"fileLen\":4}";
    TEST_ASSERT(sendCmdFrame(commsCore, 1, startFrame), "ufStart handled");
    TEST_ASSERT(commsCore.responses.size() == 1, "ufStart response sent");
    uint32_t streamID = 0;
    if (commsCore.responses.size() == 1)
    {
        RaftJson resp(commsCore.responses[0]);
        TEST_ASSERT(resp.getString("rslt", "") == "ok", "ufStart response ok");
        streamID = resp.getLong("streamID", 0);
        TEST_ASSERT(streamID != 0, "ufStart response has a stream ID");
    }
    TEST_ASSERT(startRequests.size() == 1 && startRequests[0].startsWith(TEST_STREAM_ENDPOINT),
            "stream endpoint told the stream has started");

    // The next loop reports streaming activity, once
    sysMod.loop();
    TEST_ASSERT(hookCalls.size() == 1, "hook called when the stream starts");
    if (hookCalls.size() >= 1)
        TEST_ASSERT(hookCalls[0].isStreaming && !hookCalls[0].isFileSystemActivity && !hookCalls[0].isMainFWUpdate,
                "hook reports streaming only");
    sysMod.loop();
    TEST_ASSERT(hookCalls.size() == 1, "hook not called again while still streaming");

    // Send the whole stream as one (final) block - it is delivered to the endpoint and ends the session
    const std::vector<uint8_t> data = { 0x11, 0x22, 0x33, 0x44 };
    TEST_ASSERT(sendFileBlock(commsCore, 2, streamID, 0, data), "file block handled");
    TEST_ASSERT(receivedData == data, "stream endpoint received the block data");
    TEST_ASSERT(finalBlockSeen, "stream endpoint told it was the final block");

    // The session is removed and the hook then reports that activity has stopped
    sysMod.loop();
    sysMod.loop();
    TEST_ASSERT(hookCalls.size() == 2, "hook called when the stream ends");
    if (hookCalls.size() >= 2)
        TEST_ASSERT(!hookCalls[1].isStreaming && !hookCalls[1].isFileSystemActivity && !hookCalls[1].isMainFWUpdate,
                "hook reports no activity");

    RaftSysMod::setSysManager(nullptr);
}

// A stream to an endpoint that does not exist is refused and causes no activity
static void testStreamToUnknownEndpointRefused()
{
    printf("Running testStreamToUnknownEndpointRefused...\n");

    TestCommsCore commsCore;
    RestAPIEndpointManager restAPI;
    TestSysManager sysManager(commsCore, restAPI);
    RaftSysMod::setSysManager(&sysManager);

    RaftJson config("{}");
    ProtocolExchange protocolExchange("ProtExchg", config);
    int hookCallCount = 0;
    protocolExchange.setFileStreamActivityHook([&](bool, bool, bool) { hookCallCount++; });
    RaftSysMod& sysMod = protocolExchange;
    sysMod.addCommsChannels(commsCore);
    if (!commsCore.ricSerialHandler)
    {
        TEST_ASSERT(false, "RICSerial message handler registered");
        return;
    }

    sendCmdFrame(commsCore, 1, "{\"cmdName\":\"ufStart\",\"reqStr\":\"ufStart\",\"fileType\":\"rtstream\","
            "\"fileName\":\"s\",\"endpoint\":\"nosuchendpoint\",\"fileLen\":4}");
    TEST_ASSERT(commsCore.responses.size() == 1, "response sent");
    if (commsCore.responses.size() == 1)
        TEST_ASSERT(RaftJson(commsCore.responses[0]).getString("rslt", "") == "fail", "ufStart refused");
    sysMod.loop();
    sysMod.loop();
    TEST_ASSERT(hookCallCount == 0, "no activity reported");

    RaftSysMod::setSysManager(nullptr);
}

// With several sessions, the hook reports the combined activity of all of them, and reports each change
// (not only the change between idle and active). A session that ends must not stop the others being serviced
static void testConcurrentSessionsReportedTogether()
{
    printf("Running testConcurrentSessionsReportedTogether...\n");

    TestCommsCore commsCore;
    RestAPIEndpointManager restAPI;
    TestSysManager sysManager(commsCore, restAPI);
    RaftSysMod::setSysManager(&sysManager);
    restAPI.addEndpoint(TEST_STREAM_ENDPOINT, RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
            [](const String&, String&, const APISourceInfo&) { return RAFT_OK; },
            "test stream", nullptr, nullptr, RestAPIEndpoint::ENDPOINT_CACHE_NEVER, nullptr, nullptr,
            [](const String&, FileStreamBlock&, const APISourceInfo&) { return RAFT_OK; },
            [](const APISourceInfo&) { return true; });

    RaftJson config("{}");
    ProtocolExchange protocolExchange("ProtExchg", config);
    std::vector<HookCall> hookCalls;
    protocolExchange.setFileStreamActivityHook([&](bool isMainFWUpdate, bool isFileSystemActivity, bool isStreaming) {
        hookCalls.push_back({isMainFWUpdate, isFileSystemActivity, isStreaming});
    });
    RaftSysMod& sysMod = protocolExchange;
    sysMod.addCommsChannels(commsCore);
    if (!commsCore.ricSerialHandler)
    {
        TEST_ASSERT(false, "RICSerial message handler registered");
        return;
    }
    auto lastHookIs = [&](bool fw, bool fs, bool stream) {
        return !hookCalls.empty() && (hookCalls.back().isMainFWUpdate == fw) &&
                (hookCalls.back().isFileSystemActivity == fs) && (hookCalls.back().isStreaming == stream);
    };

    // A stream (first session) and a firmware upload (second session)
    sendCmdFrame(commsCore, 1, String("{\"cmdName\":\"ufStart\",\"reqStr\":\"ufStart\",\"fileType\":\"rtstream\",") +
            "\"fileName\":\"stream1\",\"endpoint\":\"" + TEST_STREAM_ENDPOINT + "\",\"fileLen\":4}");
    sendCmdFrame(commsCore, 2, "{\"cmdName\":\"ufStart\",\"reqStr\":\"ufStart\",\"fileType\":\"fw\","
            "\"fileName\":\"fw.bin\",\"fileLen\":100000}");
    TEST_ASSERT(commsCore.responses.size() == 2, "both starts answered");
    uint32_t streamID = 0;
    for (const String& resp : commsCore.responses)
    {
        TEST_ASSERT(RaftJson(resp).getString("rslt", "") == "ok", "start ok");
        if (streamID == 0)
            streamID = RaftJson(resp).getLong("streamID", 0);
    }

    // Both are reported together
    sysMod.loop();
    TEST_ASSERT(lastHookIs(true, false, true), "firmware update and streaming reported together");

    // The stream ends - the firmware update is still reported, on its own
    sendFileBlock(commsCore, 3, streamID, 0, { 1, 2, 3, 4 });
    sysMod.loop();
    TEST_ASSERT(lastHookIs(true, false, false), "firmware update still reported after the stream ends");

    // The firmware update is cancelled - no activity
    sendCmdFrame(commsCore, 4, "{\"cmdName\":\"ufCancel\",\"reqStr\":\"ufCancel\",\"fileName\":\"fw.bin\"}");
    sysMod.loop();
    TEST_ASSERT(lastHookIs(false, false, false), "no activity after the firmware update is cancelled");
    TEST_ASSERT(hookCalls.size() == 3, "one hook call per change");

    RaftSysMod::setSysManager(nullptr);
}

static void testDownloadOfMissingFileRefused()
{
    printf("Running testDownloadOfMissingFileRefused...\n");

    TestCommsCore commsCore;
    RestAPIEndpointManager restAPI;
    TestSysManager sysManager(commsCore, restAPI);
    RaftSysMod::setSysManager(&sysManager);

    RaftJson config("{}");
    ProtocolExchange protocolExchange("ProtExchg", config);
    RaftSysMod& sysMod = protocolExchange;
    sysMod.addCommsChannels(commsCore);
    if (!commsCore.ricSerialHandler)
    {
        TEST_ASSERT(false, "RICSerial message handler registered");
        return;
    }

    // A download of a file that doesn't exist is refused (before, it got no reply and the client timed out)
    sendCmdFrame(commsCore, 1, "{\"cmdName\":\"dfStart\",\"reqStr\":\"getFile\",\"fileType\":\"fs\","
            "\"fileName\":\"/no/such/file.bin\",\"crcAt\":\"end\"}");
    TEST_ASSERT(commsCore.responses.size() == 1, "response sent");
    if (commsCore.responses.size() == 1)
        TEST_ASSERT(RaftJson(commsCore.responses[0]).getString("rslt", "") == "fail", "dfStart refused");

    RaftSysMod::setSysManager(nullptr);
}

int main()
{
    testStreamSessionAndActivityHook();
    testStreamToUnknownEndpointRefused();
    testConcurrentSessionsReportedTogether();
    testDownloadOfMissingFileRefused();
    return testSummary("test_protocol_exchange");
}
