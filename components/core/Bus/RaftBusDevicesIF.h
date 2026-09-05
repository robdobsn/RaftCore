/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Bus Devices Interface
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <vector>
#include "RaftBusConsts.h"
#include "RaftArduino.h"
#include "RaftDeviceConsts.h"
#include "DevicePollingInfo.h"

// Forward declaration (full definition not needed by the interface)
class DeviceStatus;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handler serviced on the bus task for application work that drives the bus
/// @param pCtx opaque context registered alongside the handler
/// @brief Handler run on the bus worker task
/// @param pCtx caller context
/// @return true while the handler is mid-operation. The worker then holds off device polling
///         (but NOT scanning) for that iteration - see registerBusTaskServiceHandler().
typedef bool (*RaftBusTaskServiceFn)(void* pCtx);

/// @brief Verdict returned by a registered new-device identification handler
/// @note Device-agnostic: the bus has no knowledge of what the handler identifies
/// - NotMine:  the handler does not own this device; default identification proceeds unchanged
/// - Handled:  the handler has identified the device and set deviceStatus.deviceTypeIndex to a
///             valid device-type index. The bus completes the device status (init + polling +
///             data aggregator) from that index using the standard device-type records, so the
///             handler stays device-agnostic and the existing polling/decode pipeline is reused.
///             Default address-based identification is skipped.
/// - Deferred: the device is not yet ready to identify (e.g. mid-arbitration); leave it
///             unidentified and unpolled for now and retry on a later scan
enum class RaftDeviceIdentVerdict
{
    NotMine,
    Handled,
    Deferred
};

/// @brief New-device identification handler function
/// @param address address of the newly-detected device
/// @param deviceStatus (out) when returning Handled, set deviceStatus.deviceTypeIndex to the
///        identified device-type index (the bus fills in the remaining fields)
/// @param pCtx opaque context registered alongside the handler
/// @return identification verdict
typedef RaftDeviceIdentVerdict (*RaftNewDeviceIdentFn)(BusElemAddrType address, DeviceStatus& deviceStatus, void* pCtx);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Device decode state
/// @class RaftBusDeviceDecodeState
class RaftBusDeviceDecodeState
{
public:
    // Wrap-around tracking (used by generated decode functions)
    uint64_t lastReportTimestampUs = 0;
    uint64_t reportTimestampOffsetUs = 0;

    // Piecewise EMA timestamp reconstruction state
    double   emaLastSampleTimeUs = 0.0;     // Last assigned sample timestamp (absolute µs)
    double   emaIntervalUs = 0.0;           // EMA of actual sample interval (µs)
    double   emaPrevPollTimeUs = 0.0;       // Previous poll timestamp (absolute µs)
    bool     emaCalibrated = false;         // Whether initialization has occurred
    uint16_t emaCalibrationPolls = 0;       // Number of polls used for alpha selection
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Bus Devices Interface
/// @class RaftBusDevicesIF
class RaftBusDevicesIF
{
public:

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get list of device addresses attached to the bus
    /// @param pAddrList pointer to array to receive addresses
    /// @param onlyAddressesWithIdentPollResponses true to only return addresses with ident poll responses
    virtual void getDeviceAddresses(std::vector<BusElemAddrType>& addresses, bool onlyAddressesWithIdentPollResponses) const = 0;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type information by address
    /// @param address address of device to get information for
    /// @param includePlugAndPlayInfo true to include plug and play information
    /// @param deviceTypeIndex (out) device type index
    /// @return JSON string
    virtual String getDevTypeInfoJsonByAddr(BusElemAddrType address, bool includePlugAndPlayInfo, DeviceTypeIndexType& deviceTypeIndex) const = 0;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type information by device type name
    /// @param deviceType - device type name
    /// @param includePlugAndPlayInfo - true to include plug and play information
    /// @param deviceTypeIndex (out) device type index
    /// @return JSON string
    virtual String getDevTypeInfoJsonByTypeName(const String& deviceType, bool includePlugAndPlayInfo, DeviceTypeIndexType& deviceTypeIndex) const = 0;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type info JSON by device type index
    /// @param deviceTypeIdx device type index
    /// @param includePlugAndPlayInfo include plug and play info
    /// @return JSON string
    virtual String getDevTypeInfoJsonByTypeIdx(DeviceTypeIndexType deviceTypeIdx, bool includePlugAndPlayInfo) const = 0;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get queued device data in JSON format
    /// @return JSON string
    virtual String getQueuedDeviceDataJson() = 0;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get queued device data in binary format
    /// @param connMode connection mode (inc bus number)
    /// @return Binary data vector
    virtual std::vector<uint8_t> getQueuedDeviceDataBinary(uint32_t connMode) = 0;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get decoded poll responses
    /// @param address address of device to get data from
    /// @param pStructOut pointer to structure (or array of structures) to receive decoded data
    /// @param structOutSize size of structure (in bytes) to receive decoded data
    /// @param maxRecCount maximum number of records to decode
    /// @param decodeState decode state for this device
    /// @return number of records decoded
    /// @note the pStructOut should generally point to structures of the correct type for the device data and the
    ///       decodeState should be maintained between calls for the same device
    virtual uint32_t getDecodedPollResponses(BusElemAddrType address, 
                    void* pStructOut, uint32_t structOutSize, 
                    uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) const = 0;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get latest decoded poll response (non-destructive peek at most recent value)
    /// @param address address of device to get data from
    /// @param pStructOut pointer to structure to receive decoded data
    /// @param structOutSize size of structure (in bytes) to receive decoded data
    /// @param decodeState decode state for this device
    /// @return true if a value was successfully decoded
    virtual bool getLatestDecodedPollResponse(BusElemAddrType address,
                    void* pStructOut, uint32_t structOutSize,
                    RaftBusDeviceDecodeState& decodeState) const
    {
        return false;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Store poll results
    /// @param timeNowUs time in us (passed in to aid testing)
    /// @param address address
    /// @param pollResultData poll result data
    /// @param pPollInfo pointer to device polling info (maybe nullptr)
    /// @return true if result stored
    virtual bool handlePollResult(uint64_t timeNowUs, BusElemAddrType address, 
                            const std::vector<uint8_t>& pollResultData, const DevicePollingInfo* pPollInfo)
    {
        return false;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Register for device data notifications
    /// @param addrAndSlot address and slot
    /// @param dataChangeCB Callback for data change
    /// @param minTimeBetweenReportsMs Minimum time between reports (ms)
    /// @param pCallbackInfo Callback info (passed to the callback)
    virtual void registerForDeviceData(BusElemAddrType address, RaftDeviceDataChangeCB dataChangeCB, 
                uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo)
    {
    }

    /// @brief Send command to device on bus
    /// @param cmdJSON Command JSON string
    /// @param respMsg (out) response message from the device
    /// @return Result code
    /// @note The JSON string should include:
    ///       - "hexWr": hex string of data to write to the device
    ///       - "numToRd": number of bytes to read from the device (optional)
    virtual RaftRetCode sendCmdToDevice(RaftDeviceID deviceID, const char* cmdJSON, String* respMsg)
    {
        return RaftRetCode::RAFT_NOT_IMPLEMENTED;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Register a handler invoked before default identification for newly-detected devices
    /// @param newDeviceIdentFn handler function (nullptr to clear)
    /// @param pCtx opaque context passed to the handler
    /// @note Generic/device-agnostic: the handler decides whether it owns a device and may fully
    ///       populate its DeviceStatus (verdict Handled), decline (NotMine) or defer (Deferred).
    virtual void registerNewDeviceIdentHandler(RaftNewDeviceIdentFn newDeviceIdentFn, void* pCtx)
    {
        (void)newDeviceIdentFn;
        (void)pCtx;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Register a handler serviced on the bus task, for application work that must
    ///        drive the bus itself
    /// @param busTaskServiceFn handler function (nullptr to clear)
    /// @param pCtx opaque context passed to the handler
    /// @note Runs in the bus worker's own task, interleaved with scanning and polling, so a
    ///       handler may issue synchronous bus transactions safely - it IS the bus task, and
    ///       is therefore serialised against scanning and polling by construction. This is
    ///       what the new-device identification hook already relies on.
    ///
    ///       The alternative - driving the bus from another task - is unsafe: the bus has no
    ///       lock, only a pause flag with no notion of an owner, so two such callers corrupt
    ///       each other. Application work belongs here instead.
    ///
    ///       The handler MUST return promptly (a few ms). It shares the loop with polling, so
    ///       long-running work must be a state machine that yields, exactly as scanning is.
    ///
    ///       Returning true means "I am mid-operation": the worker then skips DEVICE POLLING
    ///       for that iteration, so a timed sequence of transactions does not queue behind
    ///       polling traffic. Use it only for work whose timing matters, and make sure it
    ///       becomes false again - existing devices are not polled while it is true.
    ///
    ///       Scanning is deliberately NOT suspended by this, and must not be: multiplexer
    ///       recovery is driven by the scanner (BusMultiplexers::taskService() is empty; it is
    ///       elemStateChange() from a scan that re-detects and re-initialises a mux wedged by
    ///       a device reset). A handler that resets devices depends on that recovery running.
    virtual void registerBusTaskServiceHandler(RaftBusTaskServiceFn busTaskServiceFn, void* pCtx)
    {
        (void)busTaskServiceFn;
        (void)pCtx;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get debug JSON
    /// @return JSON string
    virtual String getDebugJSON(bool includeBraces) const
    {
        if (!includeBraces)
            return "";
        return "{}";
    }
};
