/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CommsChannelManager
// Manages channels for comms messages
//
// Rob Dobson 2020-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <vector>
#include "RaftArduino.h"
#include "ProtocolBase.h"
#include "ProtocolCodecFactoryHelper.h"
#include "CommsChannel.h"
#include "RaftSysMod.h"
#include "CommsChannelBridge.h"
#include "CommsCoreIF.h"

class CommsChannelManager : public RaftSysMod, public CommsCoreIF
{
public:
    CommsChannelManager(const char *pModuleName, RaftJsonIF& sysConfig);
    virtual ~CommsChannelManager();

    // Register as an external message channel
    // xxBlockMax and xxQueueMaxLen parameters can be 0 for defaults to be used
    // Returns an ID used to identify this channel
    virtual uint32_t registerChannel(const char* protocolName, 
                const char* interfaceName,
                const char* channelName, 
                CommsChannelOutboundHandleMsgFnType outboundHandleMsgCB, 
                CommsChannelOutboundCanAcceptFnType outboundCanAcceptCB,
                const CommsChannelSettings* pSettings = nullptr) override final;

    // Add protocol handler
    virtual void addProtocol(ProtocolCodecFactoryHelper& protocolDef) override final;

    // Get channel IDs
    virtual int32_t getChannelIDByName(const String& channelName, const String& protocolName) override final;
    void getChannelIDsByInterface(const char* interfaceName, std::vector<uint32_t>& channelIDs);
    void getChannelIDs(std::vector<uint32_t>& channelIDs);

    // Check if we can accept inbound message
    virtual bool inboundCanAccept(uint32_t channelID) override final;
    
    // Handle channel message
    virtual void inboundHandleMsg(uint32_t channelID, const uint8_t* pMsg, uint32_t msgLen) override final;

    // Get max inbound message size
    virtual uint32_t inboundMsgBlockMax(uint32_t channelID, uint32_t defaultSize) override final;

    // Check if we can accept outbound message
    virtual bool outboundCanAccept(uint32_t channelID, CommsMsgTypeCode msgType, bool &noConn) override final;
    
    // Handle outbound message
    virtual CommsCoreRetCode outboundHandleMsg(CommsChannelMsg& msg) override final;

    // Get the max outbound message size
    virtual uint32_t outboundMsgBlockMax(uint32_t channelID, uint32_t defaultSize) override final;


    // Get info
    String getInfoJSON();

    // Register and unregister a bridge between two different interfaces
    virtual uint32_t bridgeRegister(const char* bridgeName, uint32_t establishmentChannelID, 
                    uint32_t otherChannelID, uint32_t idleCloseSecs) override final;
    virtual void bridgeUnregister(uint32_t bridgeID, bool forceClose) override final;
    virtual void bridgeHandleInboundMsg(uint32_t bridgeID, CommsChannelMsg& msg) override final;
    virtual bool bridgeHandleOutboundMsg(CommsChannelMsg& msg) override final;
    void bridgeService();

protected:
    // Loop - called frequently
    virtual void loop() override final;

private:
    // Vector of channels - pointer must be deleted and vector
    // element set to nullptr is the channel is deleted
    std::vector<CommsChannel*> _commsChannelVec;

    // List of protocol translations
    std::list<ProtocolCodecFactoryHelper> _protocolCodecFactoryList;

    // Bridge ID counter
    uint32_t _bridgeIDCounter = 1;

    // List of bridges
    std::list<CommsChannelBridge> _bridgeList;

    // Bridge close timeout (after last message or weak close)
    static const uint32_t DEFAULT_BRIDGE_CLOSE_TIMEOUT_MS = 30000;

    // Callbacks
    bool frameSendCB(CommsChannelMsg& msg);

    // Helpers
    void ensureProtocolCodecExists(uint32_t channelID);
    CommsCoreRetCode handleOutboundMessageOnChannel(CommsChannelMsg& msg, uint32_t channelID);

    // Consts
    static const int MAX_INBOUND_MSGS_IN_LOOP = 1;
};
