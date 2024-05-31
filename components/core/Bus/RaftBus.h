/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Bus Base-class
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include <functional>
#include "RaftArduino.h"
#include "RaftBusConsts.h"
#include "RaftBusStats.h"
#include "RaftBusDevicesIF.h"

class BusRequestInfo;
class RaftBus;
class RaftJsonIF;

typedef std::function<void(RaftBus& bus, const std::vector<BusElemAddrAndStatus>& statusChanges)> BusElemStatusCB;
typedef std::function<void(RaftBus& bus, BusOperationStatus busOperationStatus)> BusOperationStatusCB;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Base class for a bus
/// @class RaftBus
class RaftBus
{
public:
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Constructor
    /// @param busElemStatusCB - callback for bus element status changes
    /// @param busOperationStatusCB - callback for bus operation status changes
    RaftBus(BusElemStatusCB busElemStatusCB, BusOperationStatusCB busOperationStatusCB)
        : _busElemStatusCB(busElemStatusCB), _busOperationStatusCB(busOperationStatusCB)
    {
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Destructor
    virtual ~RaftBus()
    {
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Setup
    /// @param config - configuration
    /// @return true if successful
    virtual bool setup(const RaftJsonIF& config)
    {
        return false;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Close
    virtual void close()
    {
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief loop (should be called frequently to service the bus)
    virtual void loop()
    {
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get bus devices interface
    virtual RaftBusDevicesIF* getBusDevicesIF()
    {
        return nullptr;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Clear
    virtual void clear(bool incPolling)
    {
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Pause
    /// @param pause - true to pause, false to resume
    virtual void pause(bool pause)
    {
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Is paused
    /// @return true if paused
    virtual bool isPaused() const
    {
        return false;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Hiatus for a period in ms (stop bus activity for a period of time)
    /// @param forPeriodMs - period in ms
    virtual void hiatus(uint32_t forPeriodMs)
    {
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Is hiatus
    /// @return true if in hiatus
    virtual bool isHiatus() const
    {
        return false;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Check if bus is operating ok
    /// @return bus operation status
    virtual BusOperationStatus isOperatingOk() const
    {
        return BusOperationStatus::BUS_OPERATION_OK;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Check if bus is ready for new requests
    /// @return true if ready
    virtual bool isReady() const
    {
        return false;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get bus name
    /// @return bus name
    virtual String getBusName() const
    {
        return "";
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Request an action (like regular polling of a device or sending a single message and getting a response)
    /// @param busReqInfo - bus request information
    /// @return true if action queued for processing
    virtual bool addRequest(BusRequestInfo& busReqInfo)
    {
        return false;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get bus statistics as a JSON string
    /// @return JSON string
    virtual String getBusStatsJSON() const
    {
        return _busStats.getStatsJSON(getBusName());
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Check if a bus element is responding
    /// @param address - address of element to check
    /// @param pIsValid - (out) set to true if address is valid
    /// @return true if element is responding
    virtual bool isElemResponding(uint32_t address, bool* pIsValid = nullptr) const
    {
        if (pIsValid)
            *pIsValid = false;
        return true;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Request a change to bus scanning activity
    /// @param enableSlowScan - true to enable slow scan
    /// @param requestFastScan - true to request a fast scan
    virtual void requestScan(bool enableSlowScan, bool requestFastScan)
    {
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Clear received data
    virtual void rxDataClear()
    {
    }
    
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Check if received data is available (for byte-oriented buses)
    /// @return true if data is available
    virtual uint32_t rxDataBytesAvailable() const
    {
        return 0;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get received data (for byte-oriented buses)
    /// @param pData - buffer to place data in
    /// @param maxLen - maximum number of bytes to place in buffer
    /// @return number of bytes placed in pData buffer (0 if no data available)
    virtual uint32_t rxDataGet(uint8_t* pData, uint32_t maxLen)
    {
        return 0;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get the bus operation status as a string
    /// @param busOperationStatus - bus operation status
    /// @return bus operation status as a string
    static const char *busOperationStatusToString(BusOperationStatus busOperationStatus)
    {
        switch (busOperationStatus)
        {
        case BUS_OPERATION_OK: return "Ok";
        case BUS_OPERATION_FAILING: return "Failing";
        default: return "Unknown";
        }
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Convert bus address to string
    /// @param addr - address
    /// @return address as a string
    virtual String addrToString(uint32_t addr) const
    {
        return "0x" + String(addr, 16);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Convert string to bus address
    /// @param addrStr - address as a string
    /// @return address
    virtual uint32_t stringToAddr(const String& addrStr) const
    {
        return strtol(addrStr.c_str(), NULL, 0);
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Return addresses of devices attached to the bus
    /// @param addresses - vector to store the addresses of devices
    /// @param onlyAddressesWithIdentPollResponses - true to only return addresses with ident poll responses
    /// @return true if there are any ident poll responses available
    virtual bool getBusElemAddresses(std::vector<uint32_t>& addresses, bool onlyAddressesWithIdentPollResponses) const
    {
        return false;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////    
    /// @brief Get bus element poll responses for a specific address
    /// @param address - address of device to get responses for
    /// @param isOnline - (out) true if device is online
    /// @param deviceTypeIndex - (out) device type index
    /// @param devicePollResponseData - (out) vector to store the device poll response data
    /// @param responseSize - (out) size of the response data
    /// @param maxResponsesToReturn - maximum number of responses to return (0 for no limit)
    /// @return number of responses returned
    virtual uint32_t getBusElemPollResponses(uint32_t address, bool& isOnline, uint16_t& deviceTypeIndex, 
                std::vector<uint8_t>& devicePollResponseData, 
                uint32_t& responseSize, uint32_t maxResponsesToReturn)
    {
        isOnline = false;
        deviceTypeIndex = 0;
        devicePollResponseData.clear();
        responseSize = 0;
        return 0;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get bus poll JSON for all detected bus elements
    /// @return JSON string
    virtual String getBusPollResponsesJson()
    {
        return "{}";
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get time of last bus status update
    /// @return time of last bus status update in ms
    virtual uint32_t getLastStatusUpdateMs(bool includeElemOnlineStatusChanges, bool includePollDataUpdates) const
    {
        return 0;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get bus status (JSON)
    /// @return JSON string
    virtual String getBusStatusJson() const
    {
        return "{}";
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Call bus element status callback
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    void callBusElemStatusCB(const std::vector<BusElemAddrAndStatus>& statusChanges)
    {
        if (_busElemStatusCB)
            _busElemStatusCB(*this, statusChanges);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Call bus operation status callback
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    void callBusOperationStatusCB(BusOperationStatus busOperationStatus)
    {
        if (_busOperationStatusCB)
            _busOperationStatusCB(*this, busOperationStatus);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    RaftBusStats& getBusStats()
    {
        return _busStats;
    }

protected:
    RaftBusStats _busStats;
    BusElemStatusCB _busElemStatusCB;
    BusOperationStatusCB _busOperationStatusCB;
};
