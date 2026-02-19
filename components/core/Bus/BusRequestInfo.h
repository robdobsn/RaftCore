/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Bus Request Info
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include <functional>
#include "RaftRetCode.h"
#include "RaftArduino.h"
#include "HWElemReq.h"
#include "RaftDeviceConsts.h"

class BusRequestResult;
class BusRequestInfo;
typedef std::function<void(void*, BusRequestResult&)> BusRequestCallbackType;

enum BusReqType
{
    BUS_REQ_TYPE_STD,
    BUS_REQ_TYPE_POLL,
    BUS_REQ_TYPE_FW_UPDATE,
    BUS_REQ_TYPE_SLOW_SCAN,
    BUS_REQ_TYPE_FAST_SCAN,
    BUS_REQ_TYPE_SEND_IF_PAUSED    
};

// Callback to send i2c message (async)
typedef std::function<RaftRetCode(const BusRequestInfo* pReqRec, uint32_t pollListIdx)> BusReqAsyncFn;

// Callback to send i2c message (sync)
typedef std::function<RaftRetCode(const BusRequestInfo* pReqRec, std::vector<uint8_t>* pReadData)> BusReqSyncFn;

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

    BusReqType getBusReqType() const
    {
        return _busReqType;
    }

    BusRequestCallbackType getCallback() const
    {
        return _busReqCallback;
    }

    void* getCallbackParam() const
    {
        return _pCallbackData; 
    }

    bool isPolling() const
    {
        return _busReqType == BUS_REQ_TYPE_POLL;
    }

    double getPollFreqHz() const
    {
        return _pollFreqHz;
    }

    bool isFWUpdate() const
    {
        return _busReqType == BUS_REQ_TYPE_FW_UPDATE;
    }

    bool isFastScan() const
    {
        return _busReqType == BUS_REQ_TYPE_FAST_SCAN;
    }

    bool isScan() const
    {
        return _busReqType == BUS_REQ_TYPE_FAST_SCAN || _busReqType == BUS_REQ_TYPE_SLOW_SCAN;
    }

    bool isSlowScan() const
    {
        return _busReqType == BUS_REQ_TYPE_SLOW_SCAN;
    }

    const uint8_t* getWriteData() const
    {
        if (_writeData.size() == 0)
            return NULL;
        return _writeData.data();
    }

    uint16_t getWriteDataLen() const
    {
        return _writeData.size();
    }

    uint16_t getReadReqLen() const
    {
        return _readReqLen;
    }

    BusElemAddrType getAddress() const
    {
        return _address;
    }

    uint32_t getCmdId() const
    {
        return _cmdId;
    }

    void setBarAccessForMsAfterSend(uint16_t barMs)
    {
        _barAccessForMsAfterSend = barMs;
    }    

    uint16_t getBarAccessForMsAfterSend() const
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
