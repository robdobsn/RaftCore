/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Serial Bus Handler
//
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <list>
#include <stdint.h>
#include "RaftThreading.h"
#include "RaftBus.h"
#include "RaftArduino.h"
#include "RaftJsonIF.h"

class BusSerial : public RaftBus
{
public:
    /// @brief Constructor
    /// @param busElemStatusCB - callback for bus element status changes
    /// @param busOperationStatusCB - callback for bus operation status changes
    BusSerial(BusElemStatusCB busElemStatusCB, BusOperationStatusCB busOperationStatusCB);
    virtual ~BusSerial();

    /// @brief Setup
    /// @param busNum - bus number
    /// @param config - configuration
    /// @return true if setup was successful
    virtual bool setup(BusNumType busNum, const RaftJsonIF& config) override final;

    /// @brief Loop
    virtual void loop() override final;

    /// @brief Clear
    /// @param incPolling - true to clear polling data (if relevant to this bus type)
    virtual void clear(bool incPolling) override final;

    /// @brief Pause
    /// @param pause - true to pause, false to resume
    virtual void pause(bool pause) override final
    {
    }

    /// @brief Check if paused
    /// @return true if paused
    virtual bool isPaused() const override final
    {
        return false;
    }

    /// @brief Get bus name
    virtual String getBusName() const override final
    {
        return _busName;
    }

    /// @brief Check if ready (for new requests)
    virtual bool isReady() const override final;

    /// @brief Request bus action
    /// @param busReqInfo - bus request information
    /// @return true if the request was added successfully
    virtual bool addRequest(BusRequestInfo& busReqInfo) override final;

    /// @brief Clear receive buffer
    virtual void rxDataClear() override final;

    /// @brief Received data bytes available
    /// @return number of bytes available to read
    virtual uint32_t rxDataBytesAvailable() const override final;

    /// @brief Get rx data
    /// @param pData - buffer to store the data (should be at least as big as maxLen)
    /// @param maxLen - maximum number of bytes to read
    /// @return number of bytes read
    virtual uint32_t rxDataGet(uint8_t* pData, uint32_t maxLen) override final;

    /// @brief Create function to create a new instance of this class
    /// @param busElemStatusCB - callback for bus element status changes
    /// @param busOperationStatusCB - callback for bus operation status changes
    /// @return pointer to new instance of this class
    static RaftBus* createFn(BusElemStatusCB busElemStatusCB, BusOperationStatusCB busOperationStatusCB)
    {
        return new BusSerial(busElemStatusCB, busOperationStatusCB);
    }

private:
    // Settings
    int _uartNum;
    int _rxPin;
    int _txPin;
    int _baudRate;
    String _busName;
    uint32_t _rxBufSize;
    uint32_t _txBufSize;
    uint32_t _minTimeBetweenSendsMs;
    uint32_t _lastSendTimeMs;

    // isInitialised
    bool _isInitialised;

    // Defaults
    static const uint32_t BAUD_RATE_DEFAULT = 115200;
    static const uint32_t RX_BUF_SIZE_DEFAULT = 256;
    static const uint32_t TX_BUF_SIZE_DEFAULT = 256;

    // Helpers
    bool serialInit();

    // Debug
    static constexpr const char* MODULE_PREFIX = "BusSerial";

};
