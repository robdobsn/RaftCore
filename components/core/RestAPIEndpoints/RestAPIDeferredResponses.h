/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RestAPIDeferredResponses
// Registry of REST API requests whose response is sent after the handler has returned
//
// A handler which cannot answer immediately (e.g. it has queued a bus transaction) returns RAFT_RESP_DEFERRED
// having called deferResponse(). The transport which received the request then binds a delivery function
// (bindDelivery()) and the handler later calls complete() with the response. If the handler does not complete
// in time the registry delivers a timeout error response instead. All methods must be called on the main task.
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include <functional>
#include "RaftArduino.h"
#include "RaftUtils.h"
#include "RaftRetCode.h"
#include "APISourceInfo.h"

class RestAPIDeferredResponses
{
public:
    // Function which delivers a response to the client
    typedef std::function<void(const String& respStr)> DeliveryFn;

    // Function called (optionally) when a deferred request times out so the handler can release its state
    typedef std::function<void(uint32_t deferToken)> TimeoutFn;

    // Default and maximum time to wait for a deferred response
    // The default is within the 2s retry interval of raftjs so a client does not resend the request
    static const uint32_t DEFAULT_TIMEOUT_MS = 1000;
    static const uint32_t MAX_TIMEOUT_MS = 60000;

    // Maximum number of deferred requests pending at once
    static const uint32_t MAX_PENDING = 16;

    // Stats
    struct Stats
    {
        uint32_t deferred = 0;
        uint32_t completed = 0;
        uint32_t timedOut = 0;
        uint32_t rejectedFull = 0;
        uint32_t rejectedCannotDefer = 0;
        uint32_t unknownToken = 0;
        uint32_t cancelled = 0;
        uint32_t maxPending = 0;
    };

    /// @brief Allocate a token for a request which a transport is able to complete later
    /// @return token (never 0)
    uint32_t allocToken()
    {
        _lastToken++;
        if (_lastToken == 0)
            _lastToken = 1;
        return _lastToken;
    }

    /// @brief Defer the response to a request (called by the handler before returning RAFT_RESP_DEFERRED)
    /// @param reqStr request string (used in the timeout response)
    /// @param sourceInfo source info of the request
    /// @param timeoutMs time to wait for complete() before a timeout response is sent (0 = default)
    /// @param timeoutFn optional function called if the request times out
    /// @return RAFT_RESP_DEFERRED if deferred, otherwise an error (the handler must then respond immediately)
    RaftRetCode defer(const char* reqStr, const APISourceInfo& sourceInfo, uint32_t timeoutMs, TimeoutFn timeoutFn)
    {
        // Check the transport supports deferral
        if (!sourceInfo.canDefer || (sourceInfo.deferToken == 0))
        {
            _stats.rejectedCannotDefer++;
            return RAFT_INVALID_OPERATION;
        }

        // Check space
        if (_pending.size() >= MAX_PENDING)
        {
            _stats.rejectedFull++;
            return RAFT_INSUFFICIENT_RESOURCE;
        }

        // Check not already deferred
        if (findByToken(sourceInfo.deferToken) >= 0)
            return RAFT_INVALID_OPERATION;

        // Add record
        Pending rec;
        rec.token = sourceInfo.deferToken;
        rec.reqStr = reqStr ? reqStr : "";
        rec.channelID = sourceInfo.channelID;
        rec.msgNum = sourceInfo.msgNum;
        rec.startMs = millis();
        rec.timeoutMs = (timeoutMs == 0) ? DEFAULT_TIMEOUT_MS : (timeoutMs > MAX_TIMEOUT_MS ? MAX_TIMEOUT_MS : timeoutMs);
        rec.timeoutFn = timeoutFn;
        _pending.push_back(rec);
        _stats.deferred++;
        if (_pending.size() > _stats.maxPending)
            _stats.maxPending = _pending.size();
        return RAFT_RESP_DEFERRED;
    }

    /// @brief Bind the delivery function for a deferred request (called by the transport)
    /// @param token token of the request
    /// @param deliveryFn function to deliver the response
    /// @return true if the request is pending (the response will be delivered via deliveryFn)
    bool bindDelivery(uint32_t token, DeliveryFn deliveryFn)
    {
        int idx = findByToken(token);
        if (idx < 0)
            return false;
        Pending& rec = _pending[idx];
        rec.deliveryFn = deliveryFn;

        // Deliver now if the handler has already completed the request
        if (rec.isComplete)
            deliverAndRemove(idx, rec.respStr);
        return true;
    }

    /// @brief Complete a deferred request (called by the handler when the response is available)
    /// @param token token of the request
    /// @param respStr response
    /// @return true if the request was pending (false if unknown - e.g. it has already timed out)
    bool complete(uint32_t token, const String& respStr)
    {
        int idx = findByToken(token);
        if (idx < 0)
        {
            _stats.unknownToken++;
            return false;
        }
        _stats.completed++;
        Pending& rec = _pending[idx];

        // If the transport has not yet bound a delivery function then hold the response until it does
        if (!rec.deliveryFn)
        {
            rec.isComplete = true;
            rec.respStr = respStr;
            return true;
        }
        deliverAndRemove(idx, respStr);
        return true;
    }

    /// @brief Cancel a deferred request without sending a response
    /// @param token token of the request
    /// @return true if the request was pending
    bool cancel(uint32_t token)
    {
        int idx = findByToken(token);
        if (idx < 0)
            return false;
        _pending.erase(_pending.begin() + idx);
        _stats.cancelled++;
        return true;
    }

    /// @brief Check if a request from a channel with a message number is pending
    /// @note Used by transports to ignore a client resending a request which is still being handled
    bool isPending(uint32_t channelID, uint32_t msgNum) const
    {
        if (msgNum == 0)
            return false;
        for (const Pending& rec : _pending)
        {
            if ((rec.channelID == channelID) && (rec.msgNum == msgNum))
                return true;
        }
        return false;
    }

    /// @brief Service timeouts (call frequently on the main task)
    void loop()
    {
        uint32_t nowMs = millis();
        for (int idx = (int)_pending.size() - 1; idx >= 0; idx--)
        {
            Pending& rec = _pending[idx];
            if (!Raft::isTimeout(nowMs, rec.startMs, rec.timeoutMs))
                continue;
            _stats.timedOut++;

            // Inform the handler so it can release any state
            uint32_t token = rec.token;
            TimeoutFn timeoutFn = rec.timeoutFn;

            // Deliver timeout response (or drop the record if there is no delivery function)
            String respStr;
            Raft::setJsonErrorResult(rec.reqStr.c_str(), respStr, "timeout");
            deliverAndRemove(idx, respStr);
            if (timeoutFn)
                timeoutFn(token);
        }
    }

    /// @brief Get number of pending deferred requests
    uint32_t getNumPending() const
    {
        return _pending.size();
    }

    /// @brief Get stats
    const Stats& getStats() const
    {
        return _stats;
    }

    /// @brief Get stats as JSON
    String getStatsJSON() const
    {
        char buf[200];
        snprintf(buf, sizeof(buf),
                R"({"pend":%u,"maxPend":%u,"def":%u,"cmpl":%u,"tout":%u,"full":%u,"noDef":%u,"unk":%u,"canc":%u})",
                (unsigned)_pending.size(), (unsigned)_stats.maxPending, (unsigned)_stats.deferred,
                (unsigned)_stats.completed, (unsigned)_stats.timedOut, (unsigned)_stats.rejectedFull,
                (unsigned)_stats.rejectedCannotDefer, (unsigned)_stats.unknownToken, (unsigned)_stats.cancelled);
        return buf;
    }

private:
    struct Pending
    {
        uint32_t token = 0;
        String reqStr;
        uint32_t channelID = 0;
        uint32_t msgNum = 0;
        uint32_t startMs = 0;
        uint32_t timeoutMs = 0;
        DeliveryFn deliveryFn;
        TimeoutFn timeoutFn;
        bool isComplete = false;
        String respStr;
    };
    std::vector<Pending> _pending;
    uint32_t _lastToken = 0;
    Stats _stats;

    int findByToken(uint32_t token) const
    {
        if (token == 0)
            return -1;
        for (uint32_t i = 0; i < _pending.size(); i++)
        {
            if (_pending[i].token == token)
                return i;
        }
        return -1;
    }

    void deliverAndRemove(int idx, String respStr)
    {
        // The response is taken by value as it may be held in the record which is removed here
        // Remove the record before delivering in case delivery leads to further calls into this registry
        DeliveryFn deliveryFn = _pending[idx].deliveryFn;
        _pending.erase(_pending.begin() + idx);
        if (deliveryFn)
            deliveryFn(respStr);
    }
};
