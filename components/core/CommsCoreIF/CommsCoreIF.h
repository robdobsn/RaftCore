/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CommsCoreIF
// Interface to Communications Core
//
// Rob Dobson 2018-2022
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <functional>
#include <ArduinoOrAlt.h>

class CommsChannelMsg;
class ProtocolCodecFactoryHelper;
class CommsChannelSettings;

// Message callback function type
typedef std::function<bool(CommsChannelMsg& msg)> CommsChannelMsgCB;
// Ready to receive callback function type
typedef std::function<bool()> CommsChannelReadyToRxCB;
// Channel ready function type
typedef std::function<bool(uint32_t channelID, bool& noConn)> ChannelReadyToSendCB;

class CommsCoreIF
{
public:
    // Register as an external message channel
    // xxBlockMax and xxQueueMaxLen parameters can be 0 for defaults to be used
    // Returns an ID used to identify this channel
    virtual uint32_t registerChannel(const char* protocolName, 
                const char* interfaceName,
                const char* channelName, 
                CommsChannelMsgCB msgCB, 
                ChannelReadyToSendCB outboundChannelReadyCB,
                const CommsChannelSettings* pSettings = nullptr) = 0;

    // Add protocol handler
    virtual void addProtocol(ProtocolCodecFactoryHelper& protocolDef) = 0;

    // Check if we can accept inbound message
    virtual bool canAcceptInbound(uint32_t channelID) = 0;
    
    // Handle channel message
    virtual void handleInboundMessage(uint32_t channelID, const uint8_t* pMsg, uint32_t msgLen) = 0;

    // Get the optimal comms block size
    virtual uint32_t getInboundBlockLen(uint32_t channelID, uint32_t defaultSize) = 0;
    virtual uint32_t getOutboundBlockLen(uint32_t channelID, uint32_t defaultSize) = 0;

    // Check if we can accept outbound message
    virtual bool canAcceptOutbound(uint32_t channelID, bool &noConn) = 0;

    // Handle outbound message
    virtual void handleOutboundMessage(CommsChannelMsg& msg) = 0;

    // Get channel IDs
    virtual int32_t getChannelIDByName(const String& channelName, const String& protocolName) = 0;

    // Register and unregister a bridge between two different interfaces
    virtual uint32_t bridgeRegister(const char* bridgeName, uint32_t establishmentChannelID, uint32_t otherChannelID) = 0;
    virtual void bridgeUnregister(uint32_t bridgeID, bool forceClose) = 0;
    virtual void bridgeHandleInboundMsg(uint32_t bridgeID, CommsChannelMsg& msg) = 0;
    virtual bool bridgeHandleOutboundMsg(CommsChannelMsg& msg) = 0;

    // Special channelIDs
    static const uint32_t CHANNEL_ID_UNDEFINED = 0xffff;
    static const uint32_t CHANNEL_ID_REST_API = 0xfffe;
    
};