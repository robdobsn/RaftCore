// API Source Info
// Rob Dobson 2021

#pragma once

#include <stdint.h>

class APISourceInfo
{
public:
    APISourceInfo(uint32_t channelID)
    {
        this->channelID = channelID;
    }

    // Channel the request arrived on
    uint32_t channelID;

    // Message protocol and number of the request (when it arrived as a numbered message, e.g. RICSerial)
    // so that a deferred response can be matched to the request by the client
    uint32_t msgProtocol = 0;
    uint32_t msgNum = 0;

    // Set by a transport which is able to deliver a response after the handler has returned. A handler may
    // only defer its response (RestAPIEndpointManager::deferResponse()) when this is true - otherwise it must
    // respond before returning
    bool canDefer = false;

    // Token identifying this request for a deferred response (non-zero when canDefer is true)
    uint32_t deferToken = 0;
};
