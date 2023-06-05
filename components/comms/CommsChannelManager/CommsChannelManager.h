/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CommsChannelManager
// Manages channels for comms messages
//
// Rob Dobson 2020-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <ArduinoOrAlt.h>
#include <vector>
#include <ProtocolBase.h>
#include "ProtocolCodecFactoryHelper.h"
#include "CommsChannel.h"
#include "SysModBase.h"
#include "CommsChannelBridge.h"
#include <CommsCoreIF.h>

class CommsChannelManager : public SysModBase, public CommsCoreIF
{
public:
    CommsChannelManager(const char *pModuleName, ConfigBase &defaultConfig, ConfigBase *pGlobalConfig, ConfigBase *pMutableConfig);
    virtual ~CommsChannelManager();

    // Register as an external message channel
    // xxBlockMax and xxQueueMaxLen parameters can be 0 for defaults to be used
    // Returns an ID used to identify this channel
    virtual uint32_t registerChannel(const char* protocolName, 
                const char* interfaceName,
                const char* channelName, 
                CommsChannelMsgCB msgCB, 
                ChannelReadyToSendCB outboundChannelReadyCB,
                const CommsChannelSettings* pSettings = nullptr) override final;

    // Register as an internal message sink
    uint32_t registerSink(CommsChannelMsgCB msgCB);

    // Add protocol handler
    virtual void addProtocol(ProtocolCodecFactoryHelper& protocolDef) override final;

    // Get channel IDs
    virtual int32_t getChannelIDByName(const String& channelName, const String& protocolName) override final;
    void getChannelIDsByInterface(const char* interfaceName, std::vector<uint32_t>& channelIDs);
    void getChannelIDs(std::vector<uint32_t>& channelIDs);

    // Check if we can accept inbound message
    virtual bool canAcceptInbound(uint32_t channelID) override final;
    
    // Handle channel message
    virtual void handleInboundMessage(uint32_t channelID, const uint8_t* pMsg, uint32_t msgLen) override final;

    // Check if we can accept outbound message
    virtual bool canAcceptOutbound(uint32_t channelID, bool &noConn) override final;
    
    // Handle outbound message
    virtual void handleOutboundMessage(CommsChannelMsg& msg) override final;

    // Get the optimal comms block size
    virtual uint32_t getInboundBlockLen(uint32_t channelID, uint32_t defaultSize) override final;
    virtual uint32_t getOutboundBlockLen(uint32_t channelID, uint32_t defaultSize) override final;


    // Get info
    String getInfoJSON();

    // Register and unregister a bridge between two different interfaces
    virtual uint32_t bridgeRegister(const char* bridgeName, uint32_t establishmentChannelID, uint32_t otherChannelID) override final;
    virtual void bridgeUnregister(uint32_t bridgeID, bool forceClose) override final;
    virtual void bridgeHandleInboundMsg(uint32_t bridgeID, CommsChannelMsg& msg) override final;
    virtual bool bridgeHandleOutboundMsg(CommsChannelMsg& msg) override final;

protected:
    // Service - called frequently
    virtual void service() override final;

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
    static const uint32_t BRIDGE_CLOSE_TIMEOUT_MS = 30000;

    // Callbacks
    bool frameSendCB(CommsChannelMsg& msg);

    // Helpers
    void ensureProtocolCodecExists(uint32_t channelID);
    void handleOutboundMessageOnChannel(CommsChannelMsg& msg, uint32_t channelID);
    void bridgeService();

    // Consts
    static const int MAX_INBOUND_MSGS_IN_LOOP = 1;
};
