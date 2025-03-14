/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Comms Channel
// Channels for messages from interfaces (BLE, WiFi, etc)
//
// Rob Dobson 2020-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "CommsChannel.h"

// Warn
#define WARN_ON_INBOUND_QUEUE_FULL

// Debug
// #define DEBUG_COMMS_CHANNEL
// #define DEBUG_COMMS_CHANNEL_CREATE_DELETE
// #define DEBUG_OUTBOUND_QUEUE
// #define DEBUG_INBOUND_QUEUE

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
// xxBlockMax and xxQueueMaxLen parameters can be 0 for defaults to be used
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CommsChannel::CommsChannel(const char* pSourceProtocolName, 
            const char* interfaceName, 
            const char* channelName,
            CommsChannelOutboundHandleMsgFnType outboundHandleMsgCB, 
            CommsChannelOutboundCanAcceptFnType outboundCanAcceptCB,
            const CommsChannelSettings* pSettings)
            :
            _settings(pSettings ? *pSettings : CommsChannelSettings()),
#ifdef COMMS_CHANNEL_USE_INBOUND_QUEUE
            _inboundQueue(_settings.inboundBlockLen),
#endif
            _outboundQueue(_settings.outboundQueueMaxLen)
{
    _channelProtocolName = pSourceProtocolName;
    _outboundHandleMsgCB = outboundHandleMsgCB;
    _interfaceName = interfaceName;
    _channelName = channelName;
    _outboundCanAcceptCB = outboundCanAcceptCB;
    _pProtocolCodec = NULL;
    _outboundQPeak = 0;
    _inboundQPeak = 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// setProtocolCodec
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommsChannel::setProtocolCodec(ProtocolBase* pProtocolCodec)
{
#ifdef DEBUG_COMMS_CHANNEL_CREATE_DELETE
    LOG_I(MODULE_PREFIX, "setProtocolCodec channelID %d", 
                            pProtocolCodec->getChannelID());
#endif
    _pProtocolCodec = pProtocolCodec;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Inbound queue
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CommsChannel::inboundCanAccept()
{
#ifdef COMMS_CHANNEL_USE_INBOUND_QUEUE
    return _inboundQueue.canAcceptData();
#else
    return true;
#endif
}

void CommsChannel::handleRxData(const uint8_t* pMsg, uint32_t msgLen)
{
    // Debug
#ifdef DEBUG_COMMS_CHANNEL
    LOG_I(MODULE_PREFIX, "handleRxData protocolName %s interfaceName %s channelName %s len %d handlerPtrOk %s", 
        _channelProtocolName.c_str(), 
        _interfaceName.c_str(), 
        _channelName.c_str(),
        msgLen, 
        (_pProtocolCodec ? "YES" : "NO"));
#endif
#ifdef COMMS_CHANNEL_USE_INBOUND_QUEUE
    inboundQueueAdd(pMsg, msgLen);
#else
if (_pProtocolCodec)
    _pProtocolCodec->addRxData(pMsg, msgLen);
#endif
}

void CommsChannel::handleRxData(const SpiramAwareUint8Vector& msg)
{
    // Debug
#ifdef DEBUG_COMMS_CHANNEL
    LOG_I(MODULE_PREFIX, "handleRxData protocolName %s interfaceName %s channelName %s len %d handlerPtrOk %s", 
        _channelProtocolName.c_str(), 
        _interfaceName.c_str(), 
        _channelName.c_str(),
        msg.size(), 
        (_pProtocolCodec ? "YES" : "NO"));
#endif
#ifdef COMMS_CHANNEL_USE_INBOUND_QUEUE
    inboundQueueAdd(msg);
#else
if (_pProtocolCodec)
    _pProtocolCodec->addRxData(msg);
#endif
}

#ifdef COMMS_CHANNEL_USE_INBOUND_QUEUE
void CommsChannel::inboundQueueAdd(const uint8_t* pMsg, uint32_t msgLen)
{
    ProtocolRawMsg msg(pMsg, msgLen);
#if defined(DEBUG_COMMS_CHANNEL) || defined(WARN_ON_INBOUND_QUEUE_FULL)
    bool addedOk = 
#endif
    _inboundQueue.put(msg, 10);
    if (_inboundQPeak < _inboundQueue.count())
        _inboundQPeak = _inboundQueue.count();
#ifdef DEBUG_COMMS_CHANNEL
    LOG_I(MODULE_PREFIX, "inboundQueueAdd %slen %d peak %d", addedOk ? "" : "FAILED QUEUE IS FULL ", msgLen, _inboundQPeak);
#endif
#ifdef WARN_ON_INBOUND_QUEUE_FULL
    if (!addedOk)
    {
        LOG_W(MODULE_PREFIX, "inboundQueueAdd QUEUE IS FULL peak %d", _inboundQPeak);
    }
#endif
}
#endif

#ifdef COMMS_CHANNEL_USE_INBOUND_QUEUE
void CommsChannel::inboundQueueAdd(const SpiramAwareUint8Vector& inMsg)
{
    ProtocolRawMsg msg(inMsg);
#if defined(DEBUG_COMMS_CHANNEL) || defined(WARN_ON_INBOUND_QUEUE_FULL)
    bool addedOk = 
#endif
    _inboundQueue.put(msg, 10);
    if (_inboundQPeak < _inboundQueue.count())
        _inboundQPeak = _inboundQueue.count();
#ifdef DEBUG_COMMS_CHANNEL
    LOG_I(MODULE_PREFIX, "inboundQueueAdd %slen %d peak %d", addedOk ? "" : "FAILED QUEUE IS FULL ", msgLen, _inboundQPeak);
#endif
#ifdef WARN_ON_INBOUND_QUEUE_FULL
    if (!addedOk)
    {
        LOG_W(MODULE_PREFIX, "inboundQueueAdd QUEUE IS FULL peak %d", _inboundQPeak);
    }
#endif
}
#endif

#ifdef COMMS_CHANNEL_USE_INBOUND_QUEUE
bool CommsChannel::inboundQueueGet(ProtocolRawMsg& msg)
{
    return _inboundQueue.get(msg);
}
#endif

bool CommsChannel::processInboundQueue()
{
#ifndef COMMS_CHANNEL_USE_INBOUND_QUEUE
    return false;
#else
    // Peek queue
    ProtocolRawMsg msg;
    bool msgAvailable = _inboundQueue.peek(msg);
    if (!msgAvailable || !_pProtocolCodec)
        return false;

    // Check if protocol codec can handle more data
    if (!_pProtocolCodec->readyForRxData())
        return false;

    // Add data to codec
    _pProtocolCodec->addRxData(msg.getBuf(), msg.getBufLen());

#ifdef DEBUG_INBOUND_QUEUE
    LOG_I(MODULE_PREFIX, "processInboundQueue msgLen %d channelID %d protocolName %s", 
                msg.getBufLen(), 
                _pProtocolCodec ? _pProtocolCodec->getChannelID() : -1, 
                _pProtocolCodec ? _pProtocolCodec->getProtocolName() : "NULL");
#endif

    // Remove message from queue
    _inboundQueue.get(msg);
    return true;
#endif
}

void CommsChannel::addTxMsgToProtocolCodec(CommsChannelMsg& msg)
{
    if (_pProtocolCodec)
        _pProtocolCodec->encodeTxMsgAndSend(msg);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Outbound queue
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommsChannel::outboundQueueAdd(CommsChannelMsg& msg)
{
    _outboundQueue.put(msg);
    if (_outboundQPeak < _outboundQueue.count())
        _outboundQPeak = _outboundQueue.count();

#ifdef DEBUG_OUTBOUND_QUEUE
    LOG_I(MODULE_PREFIX, "outboundQueueAdd msglen %d cmdVecPtr %p dataPtr %p", 
                msg.getBufLen(), msg.getCmdVector(), msg.getCmdVector().data());
#endif
}

bool CommsChannel::outboundQueuePeek(CommsChannelMsg& msg)
{
    return _outboundQueue.peek(msg);
}

bool CommsChannel::outboundQueueGet(CommsChannelMsg& msg)
{
    bool hasGot = _outboundQueue.get(msg);

#ifdef DEBUG_OUTBOUND_QUEUE
    if (hasGot)
    {
        LOG_I(MODULE_PREFIX, "outboundQueueGet msglen %d cmdVecPtr %p dataPtr %p", 
                    msg.getBufLen(), msg.getCmdVector(), msg.getCmdVector().data());    
    }
#endif
    return hasGot;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get info JSON
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String CommsChannel::getInfoJSON()
{
    char jsonInfoStr[200];
    snprintf(jsonInfoStr, sizeof(jsonInfoStr),
            R"("name":"%s","if":"%s","ch":%s,"hdlr":%d,"chanID":%d,"inMax":%d,"inPk":%d,"inBlk":%d,"outMax":%d,"outPk":%d,"outBlk":%d)",
            _channelProtocolName.c_str(), _interfaceName.c_str(), _channelName.c_str(),
            _pProtocolCodec ? 1 : 0,
            (int)(_pProtocolCodec ? _pProtocolCodec->getChannelID() : -1),
            (int)_inboundQueue.maxLen(), _inboundQPeak, (int)_settings.inboundBlockLen,
            (int)_outboundQueue.maxLen(), _outboundQPeak, (int)_settings.outboundBlockLen);
    return "{" + String(jsonInfoStr) + "}";
}
