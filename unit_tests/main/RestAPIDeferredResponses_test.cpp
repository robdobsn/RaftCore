/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Unit test of RestAPIDeferredResponses and deferred responses via RestAPIEndpointManager
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "unity.h"
#include "RestAPIEndpointManager.h"
#include "RestAPIDeferredResponses.h"
#include "RaftJson.h"
#include "RaftArduino.h"
#include <vector>

namespace
{
    // Source info for a request from a transport which can defer
    APISourceInfo deferrableSource(RestAPIDeferredResponses& reg, uint32_t channelID, uint32_t msgNum)
    {
        APISourceInfo sourceInfo(channelID);
        sourceInfo.msgNum = msgNum;
        sourceInfo.canDefer = true;
        sourceInfo.deferToken = reg.allocToken();
        return sourceInfo;
    }

    // Delivery recorder
    struct Delivered
    {
        std::vector<String> resps;
        RestAPIDeferredResponses::DeliveryFn fn()
        {
            return [this](const String& resp) { resps.push_back(resp); };
        }
    };
}

TEST_CASE("deferred - rejected when transport cannot defer", "[deferred]")
{
    RestAPIDeferredResponses reg;
    APISourceInfo sourceInfo(1);
    TEST_ASSERT_EQUAL(RAFT_INVALID_OPERATION, reg.defer("req", sourceInfo, 0, nullptr));
    sourceInfo.canDefer = true;
    sourceInfo.deferToken = 0;
    TEST_ASSERT_EQUAL(RAFT_INVALID_OPERATION, reg.defer("req", sourceInfo, 0, nullptr));
    TEST_ASSERT_EQUAL(0, reg.getNumPending());
    TEST_ASSERT_EQUAL(2, reg.getStats().rejectedCannotDefer);
}

TEST_CASE("deferred - complete after bind delivers once", "[deferred]")
{
    RestAPIDeferredResponses reg;
    Delivered delivered;
    APISourceInfo src = deferrableSource(reg, 5, 10);
    TEST_ASSERT_EQUAL(RAFT_RESP_DEFERRED, reg.defer("cmd", src, 0, nullptr));
    TEST_ASSERT_TRUE(reg.bindDelivery(src.deferToken, delivered.fn()));
    TEST_ASSERT_EQUAL(0, delivered.resps.size());
    TEST_ASSERT_TRUE(reg.complete(src.deferToken, "{\"rslt\":\"ok\"}"));
    TEST_ASSERT_EQUAL(1, delivered.resps.size());
    TEST_ASSERT_EQUAL_STRING("{\"rslt\":\"ok\"}", delivered.resps[0].c_str());
    TEST_ASSERT_EQUAL(0, reg.getNumPending());

    // A second completion (or late completion) is rejected and not delivered
    TEST_ASSERT_FALSE(reg.complete(src.deferToken, "again"));
    TEST_ASSERT_EQUAL(1, delivered.resps.size());
    TEST_ASSERT_EQUAL(1, reg.getStats().unknownToken);
}

TEST_CASE("deferred - complete before bind delivers on bind", "[deferred]")
{
    RestAPIDeferredResponses reg;
    Delivered delivered;
    APISourceInfo src = deferrableSource(reg, 5, 11);
    TEST_ASSERT_EQUAL(RAFT_RESP_DEFERRED, reg.defer("cmd", src, 0, nullptr));
    TEST_ASSERT_TRUE(reg.complete(src.deferToken, "early"));
    TEST_ASSERT_EQUAL(1, reg.getNumPending());
    TEST_ASSERT_TRUE(reg.bindDelivery(src.deferToken, delivered.fn()));
    TEST_ASSERT_EQUAL(1, delivered.resps.size());
    TEST_ASSERT_EQUAL_STRING("early", delivered.resps[0].c_str());
    TEST_ASSERT_EQUAL(0, reg.getNumPending());
}

TEST_CASE("deferred - bind and complete of unknown token", "[deferred]")
{
    RestAPIDeferredResponses reg;
    Delivered delivered;
    TEST_ASSERT_FALSE(reg.bindDelivery(1234, delivered.fn()));
    TEST_ASSERT_FALSE(reg.complete(1234, "x"));
    TEST_ASSERT_FALSE(reg.complete(0, "x"));
    TEST_ASSERT_EQUAL(0, delivered.resps.size());
}

TEST_CASE("deferred - timeout delivers error and calls timeout fn", "[deferred]")
{
    RestAPIDeferredResponses reg;
    Delivered delivered;
    uint32_t timedOutToken = 0;
    APISourceInfo src = deferrableSource(reg, 7, 20);
    TEST_ASSERT_EQUAL(RAFT_RESP_DEFERRED, reg.defer("devman/cmdraw?x=1", src, 50,
                [&timedOutToken](uint32_t token) { timedOutToken = token; }));
    TEST_ASSERT_TRUE(reg.bindDelivery(src.deferToken, delivered.fn()));

    // Not timed out yet
    reg.loop();
    TEST_ASSERT_EQUAL(0, delivered.resps.size());

    // Timed out
    delay(70);
    reg.loop();
    TEST_ASSERT_EQUAL(1, delivered.resps.size());
    RaftJson resp(delivered.resps[0].c_str());
    TEST_ASSERT_EQUAL_STRING("fail", resp.getString("rslt", "").c_str());
    TEST_ASSERT_EQUAL_STRING("timeout", resp.getString("error", "").c_str());
    TEST_ASSERT_EQUAL_STRING("devman/cmdraw?x=1", resp.getString("req", "").c_str());
    TEST_ASSERT_EQUAL(src.deferToken, timedOutToken);
    TEST_ASSERT_EQUAL(0, reg.getNumPending());
    TEST_ASSERT_EQUAL(1, reg.getStats().timedOut);

    // Late completion is ignored
    TEST_ASSERT_FALSE(reg.complete(src.deferToken, "late"));
    TEST_ASSERT_EQUAL(1, delivered.resps.size());
}

TEST_CASE("deferred - timeout without bound delivery drops the request", "[deferred]")
{
    RestAPIDeferredResponses reg;
    APISourceInfo src = deferrableSource(reg, 7, 21);
    TEST_ASSERT_EQUAL(RAFT_RESP_DEFERRED, reg.defer("cmd", src, 20, nullptr));
    delay(40);
    reg.loop();
    TEST_ASSERT_EQUAL(0, reg.getNumPending());
}

TEST_CASE("deferred - default timeout used when 0", "[deferred]")
{
    RestAPIDeferredResponses reg;
    Delivered delivered;
    APISourceInfo src = deferrableSource(reg, 1, 1);
    TEST_ASSERT_EQUAL(RAFT_RESP_DEFERRED, reg.defer("cmd", src, 0, nullptr));
    TEST_ASSERT_TRUE(reg.bindDelivery(src.deferToken, delivered.fn()));
    delay(RestAPIDeferredResponses::DEFAULT_TIMEOUT_MS / 2);
    reg.loop();
    TEST_ASSERT_EQUAL(0, delivered.resps.size());
    delay(RestAPIDeferredResponses::DEFAULT_TIMEOUT_MS / 2 + 50);
    reg.loop();
    TEST_ASSERT_EQUAL(1, delivered.resps.size());
}

TEST_CASE("deferred - capacity limit", "[deferred]")
{
    RestAPIDeferredResponses reg;
    std::vector<uint32_t> tokens;
    for (uint32_t i = 0; i < RestAPIDeferredResponses::MAX_PENDING; i++)
    {
        APISourceInfo src = deferrableSource(reg, 1, i + 1);
        TEST_ASSERT_EQUAL(RAFT_RESP_DEFERRED, reg.defer("cmd", src, 0, nullptr));
        tokens.push_back(src.deferToken);
    }
    APISourceInfo extra = deferrableSource(reg, 1, 200);
    TEST_ASSERT_EQUAL(RAFT_INSUFFICIENT_RESOURCE, reg.defer("cmd", extra, 0, nullptr));
    TEST_ASSERT_EQUAL(1, reg.getStats().rejectedFull);
    TEST_ASSERT_EQUAL(RestAPIDeferredResponses::MAX_PENDING, reg.getStats().maxPending);

    // Space is available again once one is delivered
    Delivered delivered;
    TEST_ASSERT_TRUE(reg.bindDelivery(tokens[0], delivered.fn()));
    TEST_ASSERT_TRUE(reg.complete(tokens[0], "x"));
    TEST_ASSERT_EQUAL(1, delivered.resps.size());
    TEST_ASSERT_EQUAL(RAFT_RESP_DEFERRED, reg.defer("cmd", extra, 0, nullptr));
}

TEST_CASE("deferred - pending lookup by channel and message number", "[deferred]")
{
    RestAPIDeferredResponses reg;
    Delivered delivered;
    APISourceInfo src = deferrableSource(reg, 9, 42);
    TEST_ASSERT_FALSE(reg.isPending(9, 42));
    TEST_ASSERT_EQUAL(RAFT_RESP_DEFERRED, reg.defer("cmd", src, 0, nullptr));
    TEST_ASSERT_TRUE(reg.isPending(9, 42));
    TEST_ASSERT_FALSE(reg.isPending(9, 43));
    TEST_ASSERT_FALSE(reg.isPending(8, 42));
    TEST_ASSERT_FALSE(reg.isPending(9, 0));
    TEST_ASSERT_TRUE(reg.bindDelivery(src.deferToken, delivered.fn()));
    TEST_ASSERT_TRUE(reg.complete(src.deferToken, "x"));
    TEST_ASSERT_FALSE(reg.isPending(9, 42));
}

TEST_CASE("deferred - cancel removes without delivering", "[deferred]")
{
    RestAPIDeferredResponses reg;
    Delivered delivered;
    APISourceInfo src = deferrableSource(reg, 1, 1);
    TEST_ASSERT_EQUAL(RAFT_RESP_DEFERRED, reg.defer("cmd", src, 0, nullptr));
    TEST_ASSERT_TRUE(reg.bindDelivery(src.deferToken, delivered.fn()));
    TEST_ASSERT_TRUE(reg.cancel(src.deferToken));
    TEST_ASSERT_FALSE(reg.cancel(src.deferToken));
    TEST_ASSERT_FALSE(reg.complete(src.deferToken, "x"));
    TEST_ASSERT_EQUAL(0, delivered.resps.size());
    TEST_ASSERT_EQUAL(1, reg.getStats().cancelled);
}

TEST_CASE("deferred - tokens unique and duplicate defer rejected", "[deferred]")
{
    RestAPIDeferredResponses reg;
    uint32_t t1 = reg.allocToken();
    uint32_t t2 = reg.allocToken();
    TEST_ASSERT_NOT_EQUAL(0, t1);
    TEST_ASSERT_NOT_EQUAL(t1, t2);
    APISourceInfo src(1);
    src.canDefer = true;
    src.deferToken = t1;
    TEST_ASSERT_EQUAL(RAFT_RESP_DEFERRED, reg.defer("cmd", src, 0, nullptr));
    TEST_ASSERT_EQUAL(RAFT_INVALID_OPERATION, reg.defer("cmd", src, 0, nullptr));
    TEST_ASSERT_EQUAL(1, reg.getNumPending());
}

TEST_CASE("deferred - timeout clamped to maximum", "[deferred]")
{
    RestAPIDeferredResponses reg;
    Delivered delivered;
    APISourceInfo src = deferrableSource(reg, 1, 1);
    TEST_ASSERT_EQUAL(RAFT_RESP_DEFERRED, reg.defer("cmd", src, 0xFFFFFFFF, nullptr));
    TEST_ASSERT_TRUE(reg.bindDelivery(src.deferToken, delivered.fn()));
    delay(20);
    reg.loop();
    TEST_ASSERT_EQUAL(0, delivered.resps.size());
    TEST_ASSERT_TRUE(reg.cancel(src.deferToken));
}

TEST_CASE("deferred - delivery function may re-enter the registry", "[deferred]")
{
    RestAPIDeferredResponses reg;
    Delivered delivered;
    APISourceInfo srcA = deferrableSource(reg, 1, 1);
    APISourceInfo srcB = deferrableSource(reg, 1, 2);
    TEST_ASSERT_EQUAL(RAFT_RESP_DEFERRED, reg.defer("a", srcA, 0, nullptr));
    TEST_ASSERT_EQUAL(RAFT_RESP_DEFERRED, reg.defer("b", srcB, 0, nullptr));
    TEST_ASSERT_TRUE(reg.bindDelivery(srcB.deferToken, delivered.fn()));

    // Delivering A completes B from within the delivery function
    uint32_t tokenB = srcB.deferToken;
    TEST_ASSERT_TRUE(reg.bindDelivery(srcA.deferToken, [&reg, &delivered, tokenB](const String& resp) {
            delivered.resps.push_back(resp);
            reg.complete(tokenB, "fromA");
        }));
    TEST_ASSERT_TRUE(reg.complete(srcA.deferToken, "A"));
    TEST_ASSERT_EQUAL(2, delivered.resps.size());
    TEST_ASSERT_EQUAL_STRING("A", delivered.resps[0].c_str());
    TEST_ASSERT_EQUAL_STRING("fromA", delivered.resps[1].c_str());
    TEST_ASSERT_EQUAL(0, reg.getNumPending());
}

TEST_CASE("deferred - handleApiRequest return codes", "[deferred]")
{
    RestAPIEndpointManager mgr;

    // Endpoint which defers when it can and responds immediately when it can't
    mgr.addEndpoint("slow", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
            [&mgr](const String& reqStr, String& respStr, const APISourceInfo& sourceInfo) -> RaftRetCode
            {
                RaftRetCode retc = mgr.deferResponse(reqStr.c_str(), sourceInfo, 0);
                if (retc == RAFT_RESP_DEFERRED)
                    return retc;
                return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, "\"immediate\":1");
            }, "slow");

    // Endpoint which returns an error code with an error response
    mgr.addEndpoint("bad", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
            [](const String& reqStr, String& respStr, const APISourceInfo& /*sourceInfo*/) -> RaftRetCode
            {
                return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "nope");
            }, "bad");

    // Non-deferrable source - immediate response and RAFT_OK
    String resp;
    TEST_ASSERT_EQUAL(RAFT_OK, mgr.handleApiRequest("slow", resp, APISourceInfo(3)));
    TEST_ASSERT_TRUE(resp.indexOf("immediate") >= 0);

    // Deferrable source - deferred and no immediate response
    APISourceInfo src = deferrableSource(mgr.getDeferredResponses(), 3, 7);
    resp = "";
    TEST_ASSERT_EQUAL(RAFT_RESP_DEFERRED, mgr.handleApiRequest("slow", resp, src));
    TEST_ASSERT_EQUAL(0, resp.length());
    TEST_ASSERT_TRUE(mgr.getDeferredResponses().isPending(3, 7));
    TEST_ASSERT_TRUE(mgr.completeDeferred(src.deferToken, "done") );
    TEST_ASSERT_TRUE(mgr.getDeferredResponses().cancel(src.deferToken));

    // Error responses still report RAFT_OK (behaviour unchanged - the error is in the response)
    resp = "";
    TEST_ASSERT_EQUAL(RAFT_OK, mgr.handleApiRequest("bad", resp, APISourceInfo(3)));
    TEST_ASSERT_TRUE(resp.indexOf("nope") >= 0);

    // Unknown API
    resp = "";
    TEST_ASSERT_EQUAL(RAFT_INVALID_DATA, mgr.handleApiRequest("nosuchapi", resp, APISourceInfo(3)));
}
