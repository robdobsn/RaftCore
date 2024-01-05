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
#include "RaftArduino.h"
#include "CommsChannelMsg.h"

class ProtocolCodecFactoryHelper;
class CommsChannelSettings;

// Return type for CommsCoreIF messages
enum CommsCoreRetCode
{
    COMMS_CORE_RET_OK,
    COMMS_CORE_RET_FAIL,
    COMMS_CORE_RET_NO_CONN
};

// Outbound channel ready
typedef std::function<bool(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn)> CommsChannelOutboundCanAcceptFnType;
// Outbound handle message
typedef std::function<bool(CommsChannelMsg& msg)> CommsChannelOutboundHandleMsgFnType;
// Inbound channel ready
typedef std::function<bool()> CommsChannelInboundCanAcceptFnType;
// Inbound handle message
typedef std::function<bool(CommsChannelMsg& msg)> CommsChannelInboundHandleMsgFnType;

class CommsCoreIF
{
public:
    // Register as an external message channel
    // xxBlockMax and xxQueueMaxLen parameters can be 0 for defaults to be used
    // Returns an ID used to identify this channel
    virtual uint32_t registerChannel(const char* protocolName, 
                const char* interfaceName,
                const char* channelName, 
                CommsChannelOutboundHandleMsgFnType outboundHandleMsgCB, 
                CommsChannelOutboundCanAcceptFnType outboundCanAcceptCB,
                const CommsChannelSettings* pSettings = nullptr) = 0;

    // Add protocol handler
    virtual void addProtocol(ProtocolCodecFactoryHelper& protocolDef) = 0;

    // Check if we can accept inbound message
    virtual bool inboundCanAccept(uint32_t channelID) = 0;
    
    // Handle inbound message
    virtual void inboundHandleMsg(uint32_t channelID, const uint8_t* pMsg, uint32_t msgLen) = 0;

    // Get max inbound message size
    virtual uint32_t inboundMsgBlockMax(uint32_t channelID, uint32_t defaultSize) = 0;

    // Check if we can accept outbound message
    virtual bool outboundCanAccept(uint32_t channelID, CommsMsgTypeCode msgType, bool &noConn) = 0;

    // Handle outbound message
    virtual CommsCoreRetCode outboundHandleMsg(CommsChannelMsg& msg) = 0;

    // Get the max outbound message size
    virtual uint32_t outboundMsgBlockMax(uint32_t channelID, uint32_t defaultSize) = 0;

    // Get channel IDs
    virtual int32_t getChannelIDByName(const String& channelName, const String& protocolName) = 0;

    // Register and unregister a bridge between two different interfaces
    virtual uint32_t bridgeRegister(const char* bridgeName, uint32_t establishmentChannelID, 
                    uint32_t otherChannelID, uint32_t idleCloseSecs) = 0;
    virtual void bridgeUnregister(uint32_t bridgeID, bool forceClose) = 0;
    virtual void bridgeHandleInboundMsg(uint32_t bridgeID, CommsChannelMsg& msg) = 0;
    virtual bool bridgeHandleOutboundMsg(CommsChannelMsg& msg) = 0;

    // Special channelIDs
    static const uint32_t CHANNEL_ID_UNDEFINED = 0xffff;
    static const uint32_t CHANNEL_ID_REST_API = 0xfffe;
    
};