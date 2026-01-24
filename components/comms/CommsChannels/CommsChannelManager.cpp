/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Comms Channel Manager
// Manages channels for comms messages
//
// Rob Dobson 2020-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "CommsChannelManager.h"
#include "CommsChannelMsg.h"
#include "RaftArduino.h"
#include "RaftUtils.h"

// TODO - decide on enabling this - or maybe it needs to be more sophisticated?
// the idea is to avoid swamping the outbound queue with publish messages that end up either
// blocking non-publish messages or result in publish messages being received which are already
// stale
// But enabling this simplistic check might mean that some publish messages are not sent at all
// For instance if there is a rapid sequence of 2 publish messages and then a long pause
// before repeating that pattern then the second message type will never be sent

// #define IMPLEMENT_PUBLISH_EVEN_IF_OUTBOUND_QUEUE_NOT_EMPTY

// #define WARN_ON_NO_CHANNEL_MATCH
// #define DEBUG_OUTBOUND_NON_PUBLISH
// #define DEBUG_OUTBOUND_PUBLISH
// #define DEBUG_OUTBOUND_MSG
// #define DEBUG_INBOUND_MESSAGE
// #define DEBUG_COMMS_MANAGER_SERVICE
// #define DEBUG_COMMS_MANAGER_SERVICE_NOTSENT
// #define DEBUG_CHANNEL_ID
// #define DEBUG_PROTOCOL_CODEC
// #define DEBUG_FRAME_SEND
// #define DEBUG_REGISTER_CHANNEL
// #define DEBUG_OUTBOUND_MSG_ALL_CHANNELS
// #define DEBUG_INBOUND_BLOCK_MAX
// #define DEBUG_OUTBOUND_BLOCK_MAX
// #define DEBUG_COMMS_MAN_ADD_PROTOCOL
// #define DEBUG_OUTBOUND_CAN_ACCEPT_TIMING
// #define DEBUG_OUTBOUND_HANDLE_MSG_TIMING

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CommsChannelManager::CommsChannelManager(const char *pModuleName, RaftJsonIF& sysConfig)
    : RaftSysMod(pModuleName, sysConfig)
{
}

CommsChannelManager::~CommsChannelManager()
{
    // Clean up 
    for (uint32_t channelID = 0; channelID < _commsChannelVec.size(); channelID++)
    {
        // Check if channel is used
        CommsChannel* pChannel = _commsChannelVec[channelID];
        if (!pChannel)
            continue;
        ProtocolBase* pCodec = pChannel->getProtocolCodec();
        if (pCodec)
            delete pCodec;
        // Delete the channel
        delete pChannel;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loop
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommsChannelManager::loop()
{
    // Pump comms queues
    for (uint32_t channelID = 0; channelID < _commsChannelVec.size(); channelID++)
    {
        // Check if channel is used
        CommsChannel* pChannel = _commsChannelVec[channelID];
        if (!pChannel)
            continue;

        // Peek message from outbound queue
        CommsChannelMsg msg;
        if (pChannel->outboundQueuePeek(msg))
        {
            // Outbound messages - check if interface is ready
            bool noConn = false;
            bool canAccept = pChannel->outboundCanAccept(channelID, msg.getMsgTypeCode(), noConn);

            // When either canAccept or no-connection get any message to be sent
            // In the case of noConn this is so that the queue doesn't block
            // when the connection is not present
            if (canAccept || noConn)
            {
                // Get the outbound message
                if (pChannel->outboundQueueGet(msg))
                {
                    // Check if we can send
                    if (canAccept)
                    {
                        // Ensure protocol handler exists
                        ensureProtocolCodecExists(channelID);

                    // Debug
    #ifdef DEBUG_COMMS_MANAGER_SERVICE
                        LOG_I(MODULE_PREFIX, "loop outbound msg chanID %d, msgType %s msgNum %d, len %d",
                            msg.getChannelID(), msg.getMsgTypeAsString(msg.getMsgTypeCode()), msg.getMsgNumber(), msg.getBufLen());
    #endif
                        // Handle the message
                        pChannel->addTxMsgToProtocolCodec(msg);
                    }
                    else
                    {
    #ifdef DEBUG_COMMS_MANAGER_SERVICE
                        LOG_I(MODULE_PREFIX, "loop, NOCONNDISCARD chanID %d, msgType %s msgNum %d, len %d",
                            msg.getChannelID(), msg.getMsgTypeAsString(msg.getMsgTypeCode()), msg.getMsgNumber(), msg.getBufLen());
    #endif                    
                    }
                }
            }
            else
            {
    #ifdef DEBUG_COMMS_MANAGER_SERVICE_NOTSENT
                LOG_I(MODULE_PREFIX, "loop MSGNOTSENT chanID %d canAccept %d noConn %d", 
                            channelID, canAccept, noConn);
    #endif
            }
        }

        // Inbound messages - possibly multiple messages
        for (int msgIdx = 0; msgIdx < MAX_INBOUND_MSGS_IN_LOOP; msgIdx++)
        {
            if (!pChannel->processInboundQueue())
                break;
        }
    }

    // Service bridges
    bridgeService();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Register a channel
// The blockMax and queueMaxLen values can be left at 0 for default values to be applied
// Returns channel ID
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t CommsChannelManager::registerChannel(const char* protocolName, 
                    const char* interfaceName, const char* channelName,
                    CommsChannelOutboundHandleMsgFnType outboundHandleMsgCB, 
                    CommsChannelOutboundCanAcceptFnType outboundCanAcceptCB,
                    const CommsChannelSettings* pSettings)
{
    // Create new command definition and add
    CommsChannel* pCommsChannel = new CommsChannel(protocolName, interfaceName, channelName, 
                    outboundHandleMsgCB, outboundCanAcceptCB, pSettings);
    if (pCommsChannel)
    {
        // Add to vector
        _commsChannelVec.push_back(pCommsChannel);

        // Channel ID is position in vector
        uint32_t channelID = _commsChannelVec.size() - 1;

#ifdef DEBUG_REGISTER_CHANNEL
        LOG_I(MODULE_PREFIX, "registerChannel protocolName %s interfaceName %s channelID %d", 
                    protocolName, interfaceName, channelID);
#endif

        // Return channelID
        return channelID;
    }

    // Failed to create channel
    LOG_W(MODULE_PREFIX, "registerChannel FAILED protocolName %s interfaceName %s", 
                protocolName, interfaceName);
    return CommsCoreIF::CHANNEL_ID_UNDEFINED;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add protocol handler
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommsChannelManager::addProtocol(ProtocolCodecFactoryHelper& codecFactoryHelper)
{
#ifdef DEBUG_COMMS_MAN_ADD_PROTOCOL
    LOG_I(MODULE_PREFIX, "Adding protocol for %s", codecFactoryHelper.protocolName.c_str());
#endif
    _protocolCodecFactoryList.push_back(codecFactoryHelper);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get channel ID by name
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int32_t CommsChannelManager::getChannelIDByName(const String& channelName, const String& protocolName)
{
    // Iterate the channels list to find a match
    for (uint32_t channelID = 0; channelID < _commsChannelVec.size(); channelID++)
    {
        // Check if channel is used
        CommsChannel* pChannel = _commsChannelVec[channelID];
        if (!pChannel)
            continue;

#ifdef DEBUG_CHANNEL_ID
        LOG_I(MODULE_PREFIX, "Testing chName %s with %s protocol %s with %s", 
                    pChannel->getChannelName().c_str(), channelName.c_str(),
                    pChannel->getSourceProtocolName().c_str(), protocolName.c_str());
#endif
        if (pChannel->getChannelName().equalsIgnoreCase(channelName) &&
                        (pChannel->getSourceProtocolName().equalsIgnoreCase(protocolName)))
            return channelID;
    }
#ifdef WARN_ON_NO_CHANNEL_MATCH
    LOG_W(MODULE_PREFIX, "getChannelID noMatch chName %s protocol %s", channelName.c_str(), protocolName.c_str());
#endif
    return -1;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get channel IDs by interface
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommsChannelManager::getChannelIDsByInterface(const char* interfaceName, std::vector<uint32_t>& channelIDs)
{
    // Iterate the endpoints list to find matches
    channelIDs.clear();
    for (uint32_t channelID = 0; channelID < _commsChannelVec.size(); channelID++)
    {
        // Check if channel is used
        CommsChannel* pChannel = _commsChannelVec[channelID];
        if (!pChannel)
            continue;

#ifdef DEBUG_CHANNEL_ID
        LOG_I(MODULE_PREFIX, "Testing interface %s with %s", 
                    pChannel->getInterfaceName().c_str(), interfaceName);
#endif
        if (pChannel->getInterfaceName().equalsIgnoreCase(interfaceName))
            channelIDs.push_back(channelID);
    }
#ifdef WARN_ON_NO_CHANNEL_MATCH
    LOG_W(MODULE_PREFIX, "getChannelID interface %s returning %d IDs", interfaceName, channelIDs.size());
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get channel IDs
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommsChannelManager::getChannelIDs(std::vector<uint32_t>& channelIDs)
{
    // Iterate and check
    channelIDs.clear();
    channelIDs.reserve(_commsChannelVec.size());
    for (uint32_t channelID = 0; channelID < _commsChannelVec.size(); channelID++)
    {
        // Check if channel is valid
        CommsChannel* pChannel = _commsChannelVec[channelID];
        if (!pChannel)
            continue;
        channelIDs.push_back(channelID);
    }
    channelIDs.shrink_to_fit();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Check if we can accept inbound message
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CommsChannelManager::inboundCanAccept(uint32_t channelID)
{
    // Check the channel
    if (channelID >= _commsChannelVec.size())
        return false;

    // Check if channel is used
    CommsChannel* pChannel = _commsChannelVec[channelID];
    if (!pChannel)
        return false;

    // Ensure we have a handler
    ensureProtocolCodecExists(channelID);

    // Check validity
    return pChannel->inboundCanAccept();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle channel message
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommsChannelManager::inboundHandleMsg(uint32_t channelID, const uint8_t* pMsg, uint32_t msgLen)
{
    // Check the channel
    if (channelID >= _commsChannelVec.size())
    {
        LOG_W(MODULE_PREFIX, "inboundHandleMsg, channelID channelId %d is INVALID msglen %d", channelID, msgLen);
        return;
    }

    // Check if channel is used
    CommsChannel* pChannel = _commsChannelVec[channelID];
    if (!pChannel)
    {
        LOG_W(MODULE_PREFIX, "inboundHandleMsg, channelID channelId %d is NULL msglen %d", channelID, msgLen);
        return;
    }

    // Debug
#ifdef DEBUG_INBOUND_MESSAGE
    LOG_I(MODULE_PREFIX, "inboundHandleMsg, channel Id %d channel name %s pcol %s, msglen %d", channelID, 
                pChannel->getChannelName().c_str(), 
                pChannel->getProtocolCodec() ? pChannel->getProtocolCodec()->getProtocolName() : "UNKNOWN", 
                msgLen);
#endif

    // Ensure we have a handler for this msg
    ensureProtocolCodecExists(channelID);

    // Handle the message
    pChannel->handleRxData(pMsg, msgLen);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle channel message
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommsChannelManager::inboundHandleMsgVec(uint32_t channelID, const SpiramAwareUint8Vector& msg)
{
    // Check the channel
    if (channelID >= _commsChannelVec.size())
    {
        LOG_W(MODULE_PREFIX, "inboundHandleMsg, channelID channelId %d is INVALID msglen %d", channelID, (int)msg.size());
        return;
    }

    // Check if channel is used
    CommsChannel* pChannel = _commsChannelVec[channelID];
    if (!pChannel)
    {
        LOG_W(MODULE_PREFIX, "inboundHandleMsg, channelID channelId %d is NULL msglen %d", channelID, (int)msg.size());
        return;
    }

    // Debug
#ifdef DEBUG_INBOUND_MESSAGE
    LOG_I(MODULE_PREFIX, "inboundHandleMsg, channel Id %d channel name %s pcol %s, msglen %d", channelID, 
                pChannel->getChannelName().c_str(), 
                pChannel->getProtocolCodec() ? pChannel->getProtocolCodec()->getProtocolName() : "UNKNOWN", 
                msg.size());
#endif

    // Ensure we have a handler for this msg
    ensureProtocolCodecExists(channelID);

    // Handle the message
    pChannel->handleRxData(msg);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Check if we can accept outbound message
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CommsChannelManager::outboundCanAccept(uint32_t channelID, CommsMsgTypeCode msgType, bool &noConn)
{
#ifdef DEBUG_OUTBOUND_CAN_ACCEPT_TIMING
    static uint32_t callCount = 0;
    static uint32_t totalElapsedUs = 0;
    static uint32_t maxElapsedUs = 0;
    static uint32_t lastReportMs = 0;
    uint64_t startUs = micros();
#endif

    // Check the channel
    if (channelID >= _commsChannelVec.size())
        return false;

    // Check if channel is used
    CommsChannel* pChannel = _commsChannelVec[channelID];
    if (!pChannel)
        return false;

    // Ensure we have a handler
    ensureProtocolCodecExists(channelID);

    // Check validity
    bool result = pChannel->outboundCanAccept(channelID, msgType, noConn);

#ifdef DEBUG_OUTBOUND_CAN_ACCEPT_TIMING
    uint64_t endUs = micros();
    uint32_t elapsedUs = endUs - startUs;
    callCount++;
    totalElapsedUs += elapsedUs;
    if (elapsedUs > maxElapsedUs)
        maxElapsedUs = elapsedUs;
        
    // Report every 5 seconds
    if (Raft::isTimeout(millis(), lastReportMs, 5000))
    {
        LOG_I(MODULE_PREFIX, "outboundCanAccept stats: calls=%d avgUs=%d maxUs=%d",
                    callCount, callCount > 0 ? totalElapsedUs/callCount : 0, maxElapsedUs);
        lastReportMs = millis();
        callCount = 0;
        totalElapsedUs = 0;
        maxElapsedUs = 0;
    }
#endif

    return result;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle outbound message
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CommsCoreRetCode CommsChannelManager::outboundHandleMsg(CommsChannelMsg& msg)
{
    // Ret code
    CommsCoreRetCode retcode = COMMS_CORE_RET_FAIL;

    // Get the channel
    uint32_t channelID = msg.getChannelID();
    if (channelID < _commsChannelVec.size())
    {
        // Send to one channel
        retcode = handleOutboundMessageOnChannel(msg, channelID);
#ifdef DEBUG_OUTBOUND_MSG
        {
            CommsChannel* pChannel = _commsChannelVec[channelID];
            LOG_I(MODULE_PREFIX, "outboundHandleMsg chanId %d chanName %s msgLen %d retc %d", 
                        channelID, pChannel ? pChannel->getChannelName().c_str() : "UNKNONW", msg.getBufLen(), retcode)
        }
#endif
    }
    else if (channelID == MSG_CHANNEL_ID_ALL)
    {
        // Send on all open channels with an appropriate protocol
        std::vector<uint32_t> channelIDs;
        getChannelIDs(channelIDs);
        retcode = COMMS_CORE_RET_OK;
        for (uint32_t specificChannelID : channelIDs)
        {
            msg.setChannelID(specificChannelID);
            handleOutboundMessageOnChannel(msg, specificChannelID);
#ifdef DEBUG_OUTBOUND_MSG_ALL_CHANNELS
            CommsChannel* pChannel = _commsChannelVec[specificChannelID];
            LOG_W(MODULE_PREFIX, "outboundHandleMsg, all chanId %u chanName %s msglen %d", 
                            specificChannelID, pChannel ? pChannel->getChannelName().c_str() : "UNKNONW", msg.getBufLen());
#endif            
        }
    }
    else
    {
        LOG_W(MODULE_PREFIX, "outboundHandleMsg, channelID INVALID chanId %u msglen %d", channelID, msg.getBufLen());
    }
    return retcode;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get the inbound comms block size
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t CommsChannelManager::inboundMsgBlockMax(uint32_t channelID, uint32_t defaultSize)
{
    // Get the optimal block size
    if (channelID >= _commsChannelVec.size())
        return defaultSize;

    // Ensure we have a handler
    ensureProtocolCodecExists(channelID);

    // Check validity
    uint32_t blockMax = _commsChannelVec[channelID]->inboundMsgBlockMax();
#ifdef DEBUG_INBOUND_BLOCK_MAX
    LOG_I(MODULE_PREFIX, "inboundMsgBlockMax channelID %d %d", channelID, blockMax);
#endif
    return blockMax;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get the outbound comms block size
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t CommsChannelManager::outboundMsgBlockMax(uint32_t channelID, uint32_t defaultSize)
{
    // Get the optimal block size
    if (channelID >= _commsChannelVec.size())
        return defaultSize;

    // Ensure we have a handler
    ensureProtocolCodecExists(channelID);

    // Check validity
    uint32_t blockMax = _commsChannelVec[channelID]->outboundMsgBlockMax();
#ifdef DEBUG_OUTBOUND_BLOCK_MAX
    LOG_I(MODULE_PREFIX, "outboundMsgBlockMax channelID %d %d", channelID, blockMax);
#endif
    return blockMax;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle outbound message on a specific channel
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CommsCoreRetCode CommsChannelManager::handleOutboundMessageOnChannel(CommsChannelMsg& msg, uint32_t channelID)
{
    // Check valid
    if (channelID >= _commsChannelVec.size())
        return COMMS_CORE_RET_NO_CONN;

    // Check if channel is used
    CommsChannel* pChannel = _commsChannelVec[channelID];
    if (!pChannel)
        return COMMS_CORE_RET_NO_CONN;

    // Check for message type code, this controls whether a publish message or response
    // response messages are placed in a regular queue
    // publish messages are handled such that only the latest one of any address is sent
    if (msg.getMsgTypeCode() != MSG_TYPE_PUBLISH)
    {
        pChannel->outboundQueueAdd(msg);
#ifdef DEBUG_OUTBOUND_NON_PUBLISH
        // Debug
        LOG_I(MODULE_PREFIX, "handleOutboundMessage queued channel Id %d channel name %s, msglen %d, msgType %s msgNum %d numQueued %d", 
                    channelID, 
                    pChannel->getChannelName().c_str(), msg.getBufLen(), msg.getMsgTypeAsString(msg.getMsgTypeCode()),
                    msg.getMsgNumber(), pChannel->outboundQueuedCount());
#endif
    }

#ifndef IMPLEMENT_PUBLISH_EVEN_IF_OUTBOUND_QUEUE_NOT_EMPTY
    // Skip publishing if there is another message in the queue
    else if (pChannel->outboundQueuedCount() > 0)
    {
#ifdef DEBUG_OUTBOUND_PUBLISH
            // Debug
            LOG_I(MODULE_PREFIX, "handleOutboundMessage PUBLISH IGNORED while other msg waiting");
#endif
    }
#endif

    else
    {
        // TODO - maybe on callback thread here so make sure this is ok!!!!
        // TODO - probably have a single-element buffer for each publish type???
        //      - then service it in the service loop

        // Ensure protocol handler exists
        ensureProtocolCodecExists(channelID);

#ifdef DEBUG_OUTBOUND_PUBLISH
        // Debug
        LOG_I(MODULE_PREFIX, "handleOutboundMessage msg channelID %d, msgType %s msgNum %d, len %d",
            msg.getChannelID(), msg.getMsgTypeAsString(msg.getMsgTypeCode()), msg.getMsgNumber(), msg.getBufLen());
#endif

        // Check if channel can accept an outbound message and send if so
        bool noConn = false;
        if (pChannel->outboundCanAccept(channelID, msg.getMsgTypeCode(), noConn))
        {
            pChannel->addTxMsgToProtocolCodec(msg);
        }
    }

    // Return ok
    return COMMS_CORE_RET_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ensure protocol codec exists - lazy creation of codec
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommsChannelManager::ensureProtocolCodecExists(uint32_t channelID)
{
    // Check valid
    if (channelID >= _commsChannelVec.size())
        return;

    // Check if we already have a handler for this msg
    CommsChannel* pChannel = _commsChannelVec[channelID];
    if ((!pChannel) || (pChannel->getProtocolCodec() != nullptr))
        return;

    // If not we need to get one
    String channelProtocol = pChannel->getSourceProtocolName();

    // Debug
#ifdef DEBUG_PROTOCOL_CODEC
    LOG_I(MODULE_PREFIX, "No handler specified so creating one for channelID %d protocol %s",
            channelID, channelProtocol.c_str());
#endif

    // Find the protocol in the factory
    for (ProtocolCodecFactoryHelper& codecFactoryHelper : _protocolCodecFactoryList)
    {
        if (codecFactoryHelper.protocolName == channelProtocol)
        {
            // Debug
#ifdef DEBUG_PROTOCOL_CODEC
            LOG_I(MODULE_PREFIX, "ensureProtocolCodecExists channelID %d protocol %s params %s",
                    channelID, channelProtocol.c_str(), codecFactoryHelper.paramsJSON.c_str());
#endif

            // Create a protocol object
            ProtocolBase* pProtocolCodec = codecFactoryHelper.createFn(channelID,
                        codecFactoryHelper.config,
                        codecFactoryHelper.pConfigPrefix,
                        std::bind(&CommsChannelManager::frameSendCB, this, std::placeholders::_1), 
                        codecFactoryHelper.frameRxCB,
                        codecFactoryHelper.readyToRxCB);
            pChannel->setProtocolCodec(pProtocolCodec);
            return;
        }
    }

    // Debug
    LOG_W(MODULE_PREFIX, "No suitable codec found for protocol %s map entries %d", channelProtocol.c_str(), (int)_protocolCodecFactoryList.size());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Callbacks
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CommsChannelManager::frameSendCB(CommsChannelMsg& msg)
{
#ifdef DEBUG_FRAME_SEND
    LOG_I(MODULE_PREFIX, "frameSendCB (response/publish) frame type %s len %d", msg.getProtocolAsString(msg.getProtocol()), msg.getBufLen());
#endif

    uint32_t channelID = msg.getChannelID();
    if (channelID >= _commsChannelVec.size())
    {
        LOG_W(MODULE_PREFIX, "frameSendCB channelID INVALID channel Id %d msglen %d", channelID, msg.getBufLen());
        return false;
    }

    // Check if channel is used
    CommsChannel* pChannel = _commsChannelVec[channelID];
    if (!pChannel)
        return false;

#ifdef DEBUG_FRAME_SEND
    // Debug
    LOG_I(MODULE_PREFIX, "frameSendCB channel Id %d channel name %s msglen %d", channelID, 
                pChannel->getChannelName().c_str(), msg.getBufLen());
#endif

    // Send back to the channel
    return pChannel->sendMsgOnChannel(msg);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get info JSON
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String CommsChannelManager::getInfoJSON()
{
    String outStr;
    // Channels
    for (uint32_t channelID = 0; channelID < _commsChannelVec.size(); channelID++)
    {
        // Check if channel is used
        CommsChannel* pChannel = _commsChannelVec[channelID];
        if (!pChannel)
            continue;
        // Get info
        if (outStr.length() > 0)
            outStr += ",";
        outStr += pChannel->getInfoJSON();
    }
    return "[" + outStr + "]";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Register a comms bridge
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t CommsChannelManager::bridgeRegister(const char* bridgeName, uint32_t establishmentChannelID, 
            uint32_t otherChannelID, uint32_t idleCloseSecs)
{
    // Check if bridge already exists between required channels
    for (auto it = _bridgeList.begin(); it != _bridgeList.end(); ++it)
    {
        if ((it->establishmentChannelID == establishmentChannelID) && (it->otherChannelID == otherChannelID))
        {
            // Debug
            LOG_I(MODULE_PREFIX, "bridgeRegister bridgeName %s bridgeID %d estChanID %d otherChanID %d already exists", bridgeName, (int)it->bridgeID, 
                        (int)establishmentChannelID, (int)otherChannelID);

            // Return bridge ID
            return it->bridgeID;
        }
    }

    // Bridge ID
    uint32_t bridgeID = _bridgeIDCounter++;

    // Create a bridge
    CommsChannelBridge bridge(bridgeName, bridgeID, establishmentChannelID, otherChannelID, idleCloseSecs);
    _bridgeList.push_back(bridge);

    // Debug
    LOG_I(MODULE_PREFIX, "registerBridge bridgeName %s bridgeID %d estChanID %d otherChanID %d idleCloseSecs %d", 
                bridgeName, (int)bridgeID, 
                (int)establishmentChannelID, (int)otherChannelID,
                (int)idleCloseSecs);

    // Return bridge ID
    return bridgeID;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Unregister a comms bridge
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommsChannelManager::bridgeUnregister(uint32_t bridgeID, bool forceClose)
{
    // Find the bridge
    for (auto it = _bridgeList.begin(); it != _bridgeList.end(); ++it)
    {
        if (it->bridgeID == bridgeID)
        {
            // Check if closure is forced
            if (forceClose)
            {
                // Debug
                LOG_I(MODULE_PREFIX, "unregisterBridge bridgeID %d force close", bridgeID);

                // Close the bridge
                _bridgeList.erase(it);
                return;
            }

            // Debug
            LOG_I(MODULE_PREFIX, "unregisterBridge bridgeID %d will be removed at later time", bridgeID);

            // Set last message time to current time
            it->lastMsgTimeMs = millis();
            return;
        }
    }

    // Debug
    LOG_W(MODULE_PREFIX, "unregisterBridge bridgeID %d NOT FOUND", bridgeID);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle bridged inbound message
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommsChannelManager::bridgeHandleInboundMsg(uint32_t bridgeID, CommsChannelMsg& msg)
{
    // Find the bridge
    for (auto it = _bridgeList.begin(); it != _bridgeList.end(); ++it)
    {
        if (it->bridgeID == bridgeID)
        {
            // Debug
            LOG_I(MODULE_PREFIX, "bridgeHandleInboundMsg bridgeID %d estChanID %d otherChanID %d len %d", (int)it->bridgeID, 
                        (int)it->establishmentChannelID, (int)it->otherChannelID, (int)msg.getBufLen());

            // Switch channel
            msg.setChannelID(it->otherChannelID);

            // Send the message
            handleOutboundMessageOnChannel(msg, it->otherChannelID);

            // Set last message time to current time
            it->lastMsgTimeMs = millis();
            return;
        }
    }

    // Debug
    LOG_W(MODULE_PREFIX, "bridgeHandleInboundMsg bridgeID %d NOT FOUND", bridgeID);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle bridged outbound message
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CommsChannelManager::bridgeHandleOutboundMsg(CommsChannelMsg& msg)
{
    // Find the bridge which has corresponding other channel
    for (auto it = _bridgeList.begin(); it != _bridgeList.end(); ++it)
    {
        if (it->otherChannelID == msg.getChannelID())
        {
            // Debug
            LOG_I(MODULE_PREFIX, "bridgeHandleOutboundMsg bridgeID %d msgChanID %d estChanID %d otherChanID %d len %d", (int)it->bridgeID, 
                        (int)msg.getChannelID(), (int)it->establishmentChannelID, (int)it->otherChannelID, (int)msg.getBufLen());

            // Switch channel
            msg.setChannelID(it->establishmentChannelID);

            // Send the message
            handleOutboundMessageOnChannel(msg, it->establishmentChannelID);

            // Set last message time to current time
            it->lastMsgTimeMs = millis();
            return true;
        }
    }

    // Bridge not found
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service bridges - handle closure of bridges
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommsChannelManager::bridgeService()
{
    // Find the bridge
    for (auto it = _bridgeList.begin(); it != _bridgeList.end(); ++it)
    {
        // Check if bridge is not used for a while
        if (Raft::isTimeout(millis(), it->lastMsgTimeMs, it->idleCloseSecs == 0 ? DEFAULT_BRIDGE_CLOSE_TIMEOUT_MS : it->idleCloseSecs * 1000))
        {
            // Debug
            LOG_I(MODULE_PREFIX, "bridgeService idle bridgeID %d estChanID %d otherChanID %d will be removed", (int)it->bridgeID, 
                        (int)it->establishmentChannelID, (int)it->otherChannelID);

            // Close the bridge
            _bridgeList.erase(it);
            return;
        }
    }
}