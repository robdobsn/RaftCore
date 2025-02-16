/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Comms Channel
// Channels for messages from interfaces (BLE, WiFi, etc)
//
// Rob Dobson 2020-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include "Logger.h"
#include "RaftArduino.h"
#include "ProtocolBase.h"
#include "ThreadSafeQueue.h"
#include "CommsChannelMsg.h"
#include "ProtocolRawMsg.h"
#include "CommsChannelSettings.h"

// Use a queue
#define COMMS_CHANNEL_USE_INBOUND_QUEUE

class CommsChannel
{
    friend class CommsChannelManager;
public:

    // xxBlockMax and xxQueueMaxLen parameters can be 0 for defaults to be used
    CommsChannel(const char* pSourceProtocolName, 
                const char* interfaceName, 
                const char* channelName,
                CommsChannelOutboundHandleMsgFnType outboundHandleMsgCB, 
                CommsChannelOutboundCanAcceptFnType outboundCanAcceptCB,
                const CommsChannelSettings* pSettings = nullptr);

private:
    String getInterfaceName()
    {
        return _interfaceName;
    }

    String getChannelName()
    {
        return _channelName;
    }

    String getSourceProtocolName()
    {
        return _channelProtocolName;
    }

    ProtocolBase* getProtocolCodec()
    {
        return _pProtocolCodec;
    }

    // Set protocol handler for channel
    void setProtocolCodec(ProtocolBase* pProtocolCodec);

    // Handle Rx data
    void handleRxData(const SpiramAwareUint8Vector& msg);

    // Handle Rx data
    void handleRxData(const SpiramAwareUint8Vector& msg);

    // Inbound queue
    bool inboundCanAccept();

#ifdef COMMS_CHANNEL_USE_INBOUND_QUEUE
    void inboundQueueAdd(const SpiramAwareUint8Vector& msg);
#endif

#ifdef COMMS_CHANNEL_USE_INBOUND_QUEUE
    bool inboundQueueGet(ProtocolRawMsg& msg);
#endif

    uint32_t inboundMsgBlockMax()
    {
        return _settings.inboundBlockLen;
    }
    bool processInboundQueue();

    // Outbound queue
    void outboundQueueAdd(CommsChannelMsg& msg);
    bool outboundQueuePeek(CommsChannelMsg& msg);
    bool outboundQueueGet(CommsChannelMsg& msg);
    uint32_t outboundMsgBlockMax()
    {
        return _settings.outboundBlockLen;
    }
    uint32_t outboundQueuedCount()
    {
        return _outboundQueue.count();
    }

    // Call protocol handler with a message
    void addTxMsgToProtocolCodec(CommsChannelMsg& msg);

    // Check channel is ready
    bool outboundCanAccept(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn)
    {
        if (_outboundCanAcceptCB)
            return _outboundCanAcceptCB(channelID, msgType, noConn);
        noConn = false;
        return true;
    }

    // Send the message on the channel
    bool sendMsgOnChannel(CommsChannelMsg& msg)
    {
        if (_outboundHandleMsgCB)
            return _outboundHandleMsgCB(msg);
        return false;
    }

    // Get info JSON
    String getInfoJSON();

private:
    // Protocol supported
    String _channelProtocolName;

    // Channel ready callback
    CommsChannelOutboundCanAcceptFnType _outboundCanAcceptCB;
    
    // Callback to send message on channel
    CommsChannelOutboundHandleMsgFnType _outboundHandleMsgCB;

    // Name of interface and channel
    String _interfaceName;
    String _channelName;

    // Protocol codec
    ProtocolBase* _pProtocolCodec;

    // Comms settings
    CommsChannelSettings _settings;

    // Inbound queue peak level
    uint16_t _inboundQPeak;

#ifdef COMMS_CHANNEL_USE_INBOUND_QUEUE
    // Inbound message queue for raw messages
    ThreadSafeQueue<ProtocolRawMsg> _inboundQueue;
#endif

    // Outbount queue peak level
    uint16_t _outboundQPeak;

    // Outbound message queue for response messages
    ThreadSafeQueue<CommsChannelMsg> _outboundQueue;

    // Debug
    static constexpr const char* MODULE_PREFIX = "CommsChan";
};
