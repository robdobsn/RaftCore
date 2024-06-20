/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Bus Request Info
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include "RaftArduino.h"
#include "HWElemReq.h"
#include "RaftBusConsts.h"

class BusRequestResult;
typedef void (*BusRequestCallbackType) (void*, BusRequestResult&);

enum BusReqType
{
    BUS_REQ_TYPE_STD,
    BUS_REQ_TYPE_POLL,
    BUS_REQ_TYPE_FW_UPDATE,
    BUS_REQ_TYPE_SLOW_SCAN,
    BUS_REQ_TYPE_FAST_SCAN,
    BUS_REQ_TYPE_SEND_IF_PAUSED    
};

// Bus request info
class BusRequestInfo
{
public:
    BusRequestInfo()
    {
    }

    BusRequestInfo(const String& elemName, BusElemAddrType address)
    {
        _elemName = elemName;
        _address = address;
    }

    BusRequestInfo(const String& elemName, BusElemAddrType address, const uint8_t* pData, uint32_t dataLen)
    {
        _elemName = elemName;
        _address = address;
        _writeData.assign(pData, pData+dataLen);
    }

    BusRequestInfo(BusReqType busReqType, BusElemAddrType address, uint32_t cmdId, uint32_t writeDataLen, 
                const uint8_t* pWriteData, uint16_t readReqLen, uint16_t barAccessForMsAfterSend,
                BusRequestCallbackType busReqCallback, void* pCallbackData)
    {
        _busReqType = busReqType;
        _address = address;
        _cmdId = cmdId;
        if (writeDataLen > 0)
            _writeData.assign(pWriteData, pWriteData+writeDataLen);
        else
            _writeData.clear();
        _readReqLen = readReqLen;
        _pCallbackData = pCallbackData;
        _busReqCallback = busReqCallback;
        _pollFreqHz = 1;
        _barAccessForMsAfterSend = barAccessForMsAfterSend;
    }

    void set(BusReqType reqType, const HWElemReq& hwElemReq, 
                double pollFreqHz=1.0,
                BusRequestCallbackType busReqCallback=NULL, void* pCallbackData=NULL)
    {
        _busReqType = reqType;
        _writeData = hwElemReq._writeData;
        _readReqLen = hwElemReq._readReqLen;
        _pollFreqHz = pollFreqHz;
        _busReqCallback = busReqCallback;
        _pCallbackData = pCallbackData;
        _cmdId = hwElemReq._cmdId;
        _barAccessForMsAfterSend = hwElemReq._barAccessAfterSendMs;
    }

    BusReqType getBusReqType()
    {
        return _busReqType;
    }

    BusRequestCallbackType getCallback()
    {
        return _busReqCallback;
    }

    void* getCallbackParam()
    {
        return _pCallbackData; 
    }

    bool isPolling()
    {
        return _busReqType == BUS_REQ_TYPE_POLL;
    }

    double getPollFreqHz()
    {
        return _pollFreqHz;
    }

    bool isFWUpdate()
    {
        return _busReqType == BUS_REQ_TYPE_FW_UPDATE;
    }

    bool isFastScan()
    {
        return _busReqType == BUS_REQ_TYPE_FAST_SCAN;
    }

    bool isSlowScan()
    {
        return _busReqType == BUS_REQ_TYPE_SLOW_SCAN;
    }

    uint8_t* getWriteData()
    {
        if (_writeData.size() == 0)
            return NULL;
        return _writeData.data();
    }

    uint16_t getWriteDataLen()
    {
        return _writeData.size();
    }

    uint16_t getReadReqLen()
    {
        return _readReqLen;
    }

    BusElemAddrType getAddress()
    {
        return _address;
    }

    uint32_t getCmdId()
    {
        return _cmdId;
    }

    void setBarAccessForMsAfterSend(uint16_t barMs)
    {
        _barAccessForMsAfterSend = barMs;
    }    

    uint16_t getBarAccessForMsAfterSend()
    {
        return _barAccessForMsAfterSend;
    }
    
    void clear()
    {
        _pollFreqHz = 1.0;
        _busReqType = BUS_REQ_TYPE_STD;
        _pCallbackData = nullptr;
        _busReqCallback = nullptr;
        _cmdId = 0;
        _readReqLen = 0;
        _barAccessForMsAfterSend = 0;
    }

private:
    // Request type
    BusReqType _busReqType = BUS_REQ_TYPE_STD;

    // Address
    BusElemAddrType _address = 0;

    // CmdId
    uint32_t _cmdId = 0;

    // Write data
    std::vector<uint8_t> _writeData;

    // Read data
    uint16_t _readReqLen = 0;

    // Elem name
    String _elemName;

    // Data to include in callback
    void* _pCallbackData = nullptr;

    // Callback
    BusRequestCallbackType _busReqCallback = nullptr;

    // Polling
    float _pollFreqHz = 0;

    // Bar access to element after request for a period
    uint16_t _barAccessForMsAfterSend = 0;
};
