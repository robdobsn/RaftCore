/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Comms Channel Bridge
// Info on a bridge between two interfaces
//
// Rob Dobson 2020-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <Logger.h>
#include <ArduinoTime.h>

class CommsChannelBridge
{
public:
    CommsChannelBridge(const char* bridgeName, uint32_t bridgeID, uint32_t establishmentChannelID, uint32_t otherChannelID)
    {
        this->bridgeName = bridgeName;
        this->bridgeID = bridgeID;
        this->establishmentChannelID = establishmentChannelID;
        this->otherChannelID = otherChannelID;
        this->lastMsgTimeMs = millis();
    }
    CommsChannelBridge(const CommsChannelBridge& other)
    {
        this->bridgeName = other.bridgeName;
        this->bridgeID = other.bridgeID;
        this->establishmentChannelID = other.establishmentChannelID;
        this->otherChannelID = other.otherChannelID;
        this->lastMsgTimeMs = other.lastMsgTimeMs;
    }
    String bridgeName;
    uint32_t bridgeID = 0;
    uint32_t establishmentChannelID = 0;
    uint32_t otherChannelID = 0;
    uint32_t lastMsgTimeMs = 0;
};
