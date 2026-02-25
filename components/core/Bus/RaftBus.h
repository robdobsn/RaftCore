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
#include "VirtualPinResult.h"
#include "BusAddrStatus.h"

class BusRequestInfo;
class RaftBus;
class RaftJsonIF;

typedef std::function<void(RaftBus& bus, const std::vector<BusAddrStatus>& statusChanges)> BusElemStatusCB;
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
    /// @param busNum - bus number
    /// @param config - configuration
    /// @return true if successful
    virtual bool setup(BusNumType busNum, const RaftJsonIF& config)
    {
        _busNum = busNum;
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
    /// @param incPolling - true to clear polling data (if relevant to this bus type)
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
    virtual bool isElemResponding(BusElemAddrType address, bool* pIsValid = nullptr) const
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
    /// @brief Set device polling interval for an address
    /// @param address Composite address
    /// @param pollIntervalUs Polling interval in microseconds
    /// @return true if applied
    virtual bool setDevicePollIntervalUs(BusElemAddrType address, uint64_t pollIntervalUs)
    {
        return false;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device polling interval for an address
    /// @param address Composite address
    /// @return Polling interval in microseconds (0 if not supported)
    virtual uint64_t getDevicePollIntervalUs(BusElemAddrType address) const
    {
        return 0;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Return addresses of devices attached to the bus
    /// @param addresses - vector to store the addresses of devices
    /// @param onlyAddressesWithIdentPollResponses - true to only return addresses with ident poll responses
    /// @return true if there are any ident poll responses available
    virtual bool getBusElemAddresses(std::vector<BusElemAddrType>& addresses, bool onlyAddressesWithIdentPollResponses) const
    {
        return false;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////    
    /// @brief Get bus element poll responses for a specific address
    /// @param address - address of device to get responses for
    /// @param onlineState - (out) device online state
    /// @param deviceTypeIndex - (out) device type index
    /// @param devicePollResponseData - (out) vector to store the device poll response data
    /// @param responseSize - (out) size of the response data
    /// @param maxResponsesToReturn - maximum number of responses to return (0 for no limit)
    /// @return number of responses returned
    virtual uint32_t getBusElemPollResponses(BusElemAddrType address, DeviceOnlineState& onlineState, uint16_t& deviceTypeIndex, 
                std::vector<uint8_t>& devicePollResponseData, 
                uint32_t& responseSize, uint32_t maxResponsesToReturn)
    {
        onlineState = DeviceOnlineState::OFFLINE;
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
    /// @brief Get latest timestamp of change to device info (online/offline, new data, etc)
    /// @param includeElemOnlineStatusChanges include changes in online status of elements
    /// @param includeDeviceDataUpdates include new data updates
    /// @return timestamp of most recent device info in ms
    virtual uint32_t getDeviceInfoTimestampMs(bool includeElemOnlineStatusChanges, bool includeDeviceDataUpdates) const
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
    /// @brief Set virtual pin levels on IO expander (pins must be on the same expander or on GPIO)
    /// @param numPins - number of pins to set
    /// @param pPinNums - array of pin numbers
    /// @param pLevels - array of levels (0 for low)
    /// @param pResultCallback - callback for result when complete/failed
    /// @param pCallbackData - callback data
    /// @return RAFT_OK if successful
    virtual RaftRetCode virtualPinsSet(uint32_t numPins, const int* pPinNums, const uint8_t* pLevels, 
        VirtualPinSetCallbackType pResultCallback = nullptr, void* pCallbackData = nullptr)
    {
        return RAFT_INVALID_DATA;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get virtual pin level on IO expander
    /// @param pinNum - pin number
    /// @param vPinCallback - callback for virtual pin changes
    /// @param pCallbackData - callback data
    /// @return RAFT_OK if successful
    virtual RaftRetCode virtualPinRead(int pinNum, VirtualPinReadCallbackType vPinCallback, void* pCallbackData = nullptr)
    {
        return RAFT_INVALID_DATA;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Enable bus slot
    /// @param slotNum - slot number (0 is the main bus)
    /// @param enablePower - true to enable, false to disable
    /// @param enableData - true to enable data, false to disable
    /// @return RAFT_OK if successful
    virtual RaftRetCode enableSlot(uint32_t slotNum, bool enablePower, bool enableData)
    {
        return RAFT_NOT_IMPLEMENTED;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get bus status as a stringCall bus element status callback
    /// @param busElemStatusCB - callback for bus element status changes
    void callBusElemStatusCB(const std::vector<BusAddrStatus>& statusChanges)
    {
        if (_busElemStatusCB)
            _busElemStatusCB(*this, statusChanges);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Call bus operation status callback
    /// @param busOperationStatus - bus operation status
    void callBusOperationStatusCB(BusOperationStatus busOperationStatus)
    {
        if (_busOperationStatusCB)
            _busOperationStatusCB(*this, busOperationStatus);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get bus stats
    RaftBusStats& getBusStats()
    {
        return _busStats;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get bus number
    /// @return bus number
    BusNumType getBusNum() const
    {        
        return _busNum;
    }

protected:
    BusNumType _busNum = RaftDeviceID::BUS_NUM_FIRST_BUS;
    RaftBusStats _busStats;
    BusElemStatusCB _busElemStatusCB;
    BusOperationStatusCB _busOperationStatusCB;
};
