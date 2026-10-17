////////////////////////////////////////////////////////////////////////////////
//
// DeviceManager.h
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftSysMod.h"
#include "BusRequestResult.h"
#include "RaftDeviceConsts.h"
#include "RaftThreading.h"
#include "DeviceDataSubscribers.h"
#include "DeviceNames.h"
#include "DeviceFactory.h"
#include "DeviceTypeFileLoader.h"
#include "DeviceSettingsStore.h"
#include "DeviceTypeSettings.h"
#include <memory>
#include "DeviceTypeRecords.h"

class RaftBusDevicesIF;
#include <unordered_map>

class APISourceInfo;
class RaftBus;
class RaftDevice;
class DemoDevice;

class DeviceManager : public RaftSysMod
{
public:
    DeviceManager(const char *pModuleName, RaftJsonIF& sysConfig);
    virtual ~DeviceManager();

    /// @brief Create function (for use by SysManager factory)
    /// @param pModuleName - name of the module
    /// @param sysConfig - system configuration
    /// @return RaftSysMod* - pointer to the created module
    static RaftSysMod* create(const char* pModuleName, RaftJsonIF& sysConfig)
    {
        return new DeviceManager(pModuleName, sysConfig);
    }

    /// @brief Find device in device list by ID
    /// @param deviceID Device identifier
    /// @return pointer to device if found
    RaftDevice* getDevice(RaftDeviceID deviceID) const;

    /// @brief Get device by string lookup using device ID, config name, or device name map
    /// @param deviceStr Device ID as string, config device name, or name from DeviceNames map
    /// @param tryConfigName Whether to try config name lookup if ID lookup fails
    /// @return pointer to device if found, nullptr otherwise
    RaftDevice* getDevice(const String& deviceStr, bool tryConfigName = true) const;

    /// @brief Assign a friendly name to a bus device (identified by RaftDeviceID)
    /// @param deviceID Device identifier
    /// @param name Friendly name
    void setDeviceName(RaftDeviceID deviceID, const String& name);

    /// @brief Get friendly name for a device ID (from DeviceNames map)
    /// @param deviceID Device identifier
    /// @return friendly name, or deviceID.toString() if no name is mapped
    String getDeviceNameForID(RaftDeviceID deviceID) const;

    /// @brief Look up a friendly name for a device ID without the toString fallback
    /// @param deviceID Device identifier
    /// @return friendly name, or empty string if no name is mapped
    String lookupDeviceName(RaftDeviceID deviceID) const;

    /// @brief Resolve a device name to a RaftDeviceID using the DeviceNames map
    /// @param name Friendly device name
    /// @param deviceID (out) resolved device ID
    /// @return true if found in the name map
    bool resolveDeviceNameToID(const String& name, RaftDeviceID& deviceID) const;

    /// @brief Assign a role string to a device (identified by RaftDeviceID)
    /// @param deviceID Device identifier
    /// @param role Role string (e.g. "system"). Pass "normal" or empty to clear.
    void setDeviceRole(RaftDeviceID deviceID, const String& role);

    /// @brief Get the role assigned to a device ID
    /// @param deviceID Device identifier
    /// @return role string, or "normal" if no role is mapped
    String getDeviceRole(RaftDeviceID deviceID) const;

    /// @brief Get the device type key a device is expected to be (Devices entry "expectType")
    /// @param deviceID device ID
    /// @return the key, or empty if none is configured
    String getDeviceExpectedType(RaftDeviceID deviceID) const;

    /// @brief Check whether a device is tagged with role "system"
    /// @param deviceID Device identifier
    /// @return true if role is "system"
    bool isSystemDevice(RaftDeviceID deviceID) const;

    /// @brief Register for device data notifications (note that callbacks may occur on different threads)
    /// @param deviceID Device identifier
    /// @param dataChangeCB Callback for data change
    /// @param minTimeBetweenReportsMs Minimum time between reports (ms)
    /// @param pCallbackInfo Callback info (passed to the callback)
    /// @param unregister true to unregister the callback instead of registering
    void registerForDeviceData(RaftDeviceID deviceID, RaftDeviceDataChangeCB dataChangeCB, 
            uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo = nullptr, bool unregister = false);

    /// @brief Register for device data notifications (note that callbacks may occur on different threads)
    /// @param deviceTypeIndex Device type index
    /// @param dataChangeCB Callback for data change
    /// @param minTimeBetweenReportsMs Minimum time between reports (ms)
    /// @param pCallbackInfo Callback info (passed to the callback)
    /// @param unregister true to unregister the callback instead of registering
    void registerForDeviceData(DeviceTypeIndexType deviceTypeIndex, RaftDeviceDataChangeCB dataChangeCB,
            uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo = nullptr, bool unregister = false);

    /// @brief Register for device data notifications (note that callbacks may occur on different threads)
    /// @param deviceTypeKeyOrAddr Device type key, or a device address of the form "<bus>_<addr>[@slot]"
    /// @param dataChangeCB Callback for data change
    /// @param minTimeBetweenReportsMs Minimum time between reports (ms)
    /// @param pCallbackInfo Callback info (passed to the callback)
    /// @param unregister true to unregister the callback instead of registering
    void registerForDeviceData(const char* deviceTypeKeyOrAddr, RaftDeviceDataChangeCB dataChangeCB,
            uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo = nullptr, bool unregister = false);            
            
    /// @brief Ask for devices to be identified again (e.g. after a change to the device type records)
    /// @param request which devices (BusReIdentScope - Address is a bus address, so busNum names its bus)
    /// @param busNum bus number (for Address; ignored otherwise - every bus is asked)
    /// @return number of buses that accepted the request
    /// @note A device that is identified keeps its type until a different result is known; see
    ///       RaftBusDevicesIF::requestReIdentify
    uint32_t requestReIdentify(const BusReIdentRequest& request, BusNumType busNum = 0);

    /// @brief Set a device type's policy at runtime (not saved), and identify the devices it affects again: those
    ///        identified as it if it is no longer enabled, or those it now outranks if it is enabled
    /// @param deviceTypeKey device type key
    /// @param policy DeviceTypeRecord::POLICY_...
    /// @return the result (a protected type can't be disabled)
    DeviceTypeRecords::PolicyResult setTypePolicy(const char* deviceTypeKey, uint8_t policy);

    /// @brief A device's display name: its name if it has one, otherwise its type's display name and its device ID
    ///        (e.g. "Accelerometer 1_68")
    String getDeviceDisplayName(RaftDeviceID deviceID) const;

    /// @brief Note that the device type file has been changed by this device (or a change has been made that only
    ///        a restart applies) - devman/typefile then reports restartRequired until the next restart
    void markTypeFileChanged()
    {
        _typeFileChanged = true;
    }

    /// @brief Register for device status changes
    /// @param statusChangeCB Callback for status change
    void registerForDeviceStatusChange(RaftDeviceStatusChangeCB statusChangeCB);

    // Default minimum time between reports
    static constexpr uint32_t DEFAULT_MIN_TIME_BETWEEN_REPORTS_MS = 1000;

    // Get JSON debug string
    virtual String getDebugJSON() const override final;

    // Named value access (routes to devices using "DeviceName.paramName" syntax)
    virtual double getNamedValue(const char* pValueName, bool& isValid) override;
    virtual bool setNamedValue(const char* pValueName, double value) override;
    virtual String getNamedString(const char* pValueName, bool& isValid) override;
    virtual bool setNamedString(const char* pValueName, const char* value) override;

    // JSON command routing (routes to device specified in "device" field)
    virtual RaftRetCode receiveCmdJSON(const char* cmdJSON, String* pRespStr = nullptr) override;

protected:

    /// @brief Setup
    virtual void setup() override final;
    
    /// @brief Post-setup - called after setup of all sysMods complete
    virtual void postSetup() override final;

    /// @brief Loop (called frequently)
    virtual void loop() override final;

    /// @brief Add REST API endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& pEndpoints) override final;

private:

    // List of instantiated devices (static devices only - bus devices tracked by BusStatusMgr)
    struct DeviceListRecord
    {
        RaftDevice* pDevice = nullptr;
        bool isOnline = false;

        // Constructor
        DeviceListRecord(RaftDevice* pDev, bool online) :
            pDevice(pDev), isOnline(online) {}
    };
    
    // Device list is mutable to allow operations in const methods
    mutable std::list<DeviceListRecord> _staticDeviceList;
    static const uint32_t DEVICE_LIST_MAX_SIZE = 100;

    // Access mutex (mutable to allow locking in const methods)
    // This protects _staticDeviceList, _staticDispatchInstalled and _requestedDeviceStatusChangeCBList
    // It is only ever held for short periods (never while calling a callback or another module) so waiting
    // indefinitely is safe and avoids silently dropping work (e.g. status change callbacks) on contention
    mutable RaftMutex _accessMutex;
    static const uint32_t ACCESS_MUTEX_MAX_WAIT_MS = RAFT_MUTEX_WAIT_FOREVER;

    // Device data subscribers.  A bus (and a static device) holds ONE data
    // callback per device; DeviceManager owns it and fans each sample out to
    // every subscriber registered through registerForDeviceData, so two
    // subscribers to the same device no longer displace each other.
    DeviceDataSubscribers _deviceDataSubscribers;

    /// @brief Install the fan-out as a bus device's single data callback
    void installBusDataDispatcher(RaftBusDevicesIF& busDevicesIF, BusNumType busNum, BusElemAddrType address);

    /// @brief Install the fan-out on static devices that have a subscriber and do not have it yet
    /// @return number installed
    uint32_t installStaticDataDispatchers();
    std::vector<RaftDeviceID> _staticDispatchInstalled;

    // Requested device status change callbacks
    std::list<RaftDeviceStatusChangeCB> _requestedDeviceStatusChangeCBList;

    /// @brief Setup device instances
    /// @param pConfigPrefix Prefix for configuration
    /// @param devManConfig Device manager configuration
    void setupStaticDevices(const char* pConfigPrefix, RaftJsonIF& devManConfig);

    /// @brief Apply the type table settings (protected types, record policies) from the configuration
    void applyTypeTableConfig(RaftJsonIF& devManConfig);

    // Settings for device types chosen by the board or product (DevMan "typeSettings"): devconfig settings applied
    // when a device of the type is identified (under the device's own), and scan priorities
    DeviceTypeSettings _typeSettings;
    bool _applyingStoredSettings = false;   // devconfig applied from type or remembered settings (not remembered again)
    void applyTypeScanPriorities(bool reportUnknown);

    /// @brief Create a static device and add it to the static device list
    /// @param pCreateFn create function for the device's class
    /// @param pDevClass device class name
    /// @param devConf device configuration (its optional "name" and "role" are applied)
    /// @return the device, or nullptr if it could not be created
    RaftDevice* addStaticDevice(RaftDeviceCreateFn pCreateFn, const char* pDevClass, RaftJson& devConf);

    /// @brief Set up a static device and register its device type record (if it has one)
    /// @param pDevice device
    void setupStaticDevice(RaftDevice* pDevice);
    
    /// @brief Bus element status callback
    /// @param bus a reference to the bus which has elements with changed status
    /// @param statusChanges - list of status changes
    void busElemStatusCB(RaftBus& bus, const std::vector<BusAddrStatus>& statusChanges);

    /// @brief Bus operation status callback
    /// @param bus a reference to the bus which has changed status
    /// @param busOperationStatus - indicates bus ok/failing
    void busOperationStatusCB(RaftBus& bus, BusOperationStatus busOperationStatus);

    /// @brief Access to devices' data in JSON format
    /// @param topicIndex Topic index (embedded as _t field in output)
    /// @return JSON string
    String getDevicesDataJSON(uint16_t topicIndex) const;

    /// @brief Access to devices' data in binary format
    /// @param topicIndex Topic index (embedded in envelope header)
    /// @return Binary data vector
    std::vector<uint8_t> getDevicesDataBinary(uint16_t topicIndex);

    /// @brief Get devices' status hash
    /// @param stateHash hash of the currently available data
    void getDevicesHash(std::vector<uint8_t>& stateHash) const;

    /// @brief API callback
    /// @param reqStr Request string
    /// @param respStr Response string
    /// @param sourceInfo Source of the API request
    /// @return RaftRetCode Return code
    RaftRetCode apiDevMan(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);

    /// @brief Handle devman/typeinfo
    RaftRetCode apiDevManTypeInfo(const String &reqStr, String &respStr, const RaftJson& jsonParams);

    /// @brief Append the "deviceTypeKey" field for a device type index (nothing is appended if there is no record)
    /// @param jsonFields (in/out) JSON fields (without braces) to append to
    /// @param deviceTypeIndex device type index
    void appendDeviceTypeKey(String& jsonFields, DeviceTypeIndexType deviceTypeIndex) const;

    /// @brief Get the device type a request names: deviceTypeIndex=<n>, deviceTypeKey=<key>, or type=<key or index>
    ///        (the older form: a value starting with a digit is tried as an index, then as a key)
    /// @param jsonParams request parameters
    /// @param deviceTypeIndex (out) device type index (DEVICE_TYPE_INDEX_INVALID if not found)
    /// @param given (out) whether any of the parameters was given
    /// @return the record, or nullptr if none was given or it wasn't found
    const DeviceTypeRecord* getRequestedType(const RaftJson& jsonParams, DeviceTypeIndexType& deviceTypeIndex, bool& given) const;

    /// @brief Handle devman/cmdraw
    RaftRetCode apiDevManCmdRaw(const String &reqStr, String &respStr, const RaftJson& jsonParams,
                const APISourceInfo& sourceInfo);

    /// @brief Handle devman/cmdjson
    RaftRetCode apiDevManCmdJson(const String &reqStr, String &respStr, const RaftJson& jsonParams,
                const APISourceInfo& sourceInfo);

    /// @brief Form the response to a command sent to a bus device (read data or bus error)
    static void formBusCmdResponse(const char* reqStr, BusRequestResult& reqResult, String& respStr);

#ifdef DEVICE_MANAGER_ENABLE_DEMO_DEVICE
    /// @brief Handle devman/demo
    RaftRetCode apiDevManDemo(const String &reqStr, String &respStr, const RaftJson& jsonParams);
#endif

    /// @brief Handle devman/devconfig
    RaftRetCode apiDevManDevConfig(const String &reqStr, String &respStr, const RaftJson& jsonParams);

    /// @brief Handle devman/busname
    RaftRetCode apiDevManBusName(const String &reqStr, String &respStr, const RaftJson& jsonParams);
    RaftRetCode apiDevManBusStatus(const String &reqStr, String &respStr, const RaftJson& jsonParams);

    /// @brief Resolve a RaftDeviceID and RaftBus pointer from API params ("deviceid" OR "bus"+"addr")
    /// @param jsonParams JSON object containing the parameters for the command, expected to have ("bus" and "addr"), "deviceid" or "device" fields
    /// @param deviceID (out) resolved RaftDeviceID
    /// @param pBus (out) resolved RaftBus pointer (can be null if deviceID is for a static device or if bus not found)
    /// @param respStr (out) response string to be filled with error message if resolution fails
    /// @return RaftRetCode indicating
    RaftRetCode resolveDeviceIDAndBus(const RaftJson& jsonParams, RaftDeviceID& deviceID, RaftBus*& pBus, String& respStr);

    /// @brief Callback for command results
    /// @param reqResult Result of the command
    void cmdResultReportCallback(BusRequestResult& reqResult);

    // cmdraw read-back: single in-flight request; the API task waits for the bus
    // worker's callback to fill the buffer. The cmdId carries a generation count
    // so a late callback from an abandoned request can't satisfy a newer one.
    static const uint32_t CMDID_CMDRAW_BASE = 0x43520000;
    uint32_t _cmdRawInFlightCmdId = 0;
    volatile bool _cmdRawResultReady = false;
    RaftRetCode _cmdRawResult = RAFT_OK;
    std::vector<uint8_t> _cmdRawReadData;

    /// @brief Get device list frozen
    /// @param pDeviceList (out) list of devices (must be maxNumDevices long)
    /// @param maxNumDevices maximum number of devices to return
    /// @param onlyOnline true to only return online devices
    /// @param pDeviceOnlineArray pointer to array of device online flags (may be nullptr) - must be maxNumDevices long
    /// @return Number of devices
    uint32_t getStaticDeviceListFrozen(RaftDevice** pDevices, uint32_t maxDevices, bool onlyOnline, 
            bool *pDeviceOnlineArray = nullptr) const;

    /// @brief Call device status change callbacks
    /// @param pDevice Pointer to the device
    /// @param addrStatus Bus element address and status
    void callDeviceStatusChangeCBs(RaftDevice* pDevice, const BusAddrStatus& addrStatus);


    /// @brief Post setup helper - register for device data change callbacks
    /// @return number of devices registered for data change callbacks
    uint32_t postSetupRegisterDataCBs();

    /// @brief Device event callback
    /// @param device Device
    /// @param eventName Name of the event
    /// @param eventData Data associated with the event
    void deviceEventCB(RaftDevice& device, const char* eventName, const char* eventData);

    // Device names and device roles (for bus devices with friendly names), keyed by device ID
    DeviceNames _deviceNames;

    /// @brief Get a named value from a bus-discovered device using its cached poll data
    /// @param deviceID Device identifier on the bus
    /// @param paramName Attribute name (e.g. "ax", "temperature")
    /// @param isValid (out) true if value was found and valid
    /// @return attribute value
    double getBusDeviceNamedValue(RaftDeviceID deviceID, const String& paramName, bool& isValid);

    /// @brief Handle devman/setname API endpoint
    RaftRetCode apiDevManSetName(const String &reqStr, String &respStr, const RaftJson& jsonParams);

    /// @brief Handle devman/setrole API endpoint
    RaftRetCode apiDevManSetRole(const String &reqStr, String &respStr, const RaftJson& jsonParams);

    /// @brief Handle devman/listdevs API endpoint
    RaftRetCode apiDevManListDevs(const String &reqStr, String &respStr, const RaftJson& jsonParams);

    /// @brief Handle devman/slot API endpoint (set or query slot mode)
    RaftRetCode apiDevManSlot(const String &reqStr, String &respStr, const RaftJson& jsonParams);

    // Settings made with devconfig, by device (packed device ID), for the type they were made for - applied again when
    // the device is identified again as that type
    struct DevConfigSettings
    {
        DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
        std::vector<RaftJson::NameValuePair> values;
    };
    std::unordered_map<uint64_t, DevConfigSettings> _devConfigSettings;
    void rememberDevConfig(RaftDeviceID deviceID, DeviceTypeIndexType deviceTypeIndex, const RaftJson& jsonParams);
    void reapplyDevConfig(RaftDeviceID deviceID, DeviceTypeIndexType deviceTypeIndex);

    // Saved device settings (names, roles, devconfig settings, polling) - DevMan "deviceSettingsFile"
    DeviceSettingsStore _settingsStore;
    String _settingsFilePath;
    void loadDeviceSettings();
    bool saveDeviceSettings();

    // Devices whose polling is stopped (devman/devenable), by packed device ID - applied again when identified
    std::unordered_map<uint64_t, bool> _pollingStopped;
    void applyPollingSetting(RaftDeviceID deviceID);

    // Re-initialisations waiting to complete a deferred response (the result is set on the bus task)
    struct PendingReInit
    {
        uint32_t deferToken = 0;
        uint32_t startMs = 0;
        String reqStr;
        std::atomic<int> result{-1};
    };
    std::vector<std::shared_ptr<PendingReInit>> _pendingReInits;
    static constexpr uint32_t REINIT_TIMEOUT_MS = 5000;
    static constexpr uint32_t MAX_DEVICE_SETTINGS_FILE_BYTES = 16384;
    void servicePendingReInits();

    // File helpers ("/local/x.json" is file system "local", file "x.json")
    static void splitFsPath(const String& path, String& fsName, String& fileName);
    bool readFileText(const String& path, uint32_t maxBytes, String& text, bool& present);
    bool writeFileText(const String& path, const String& text);

    // Display name for a device of a type
    String displayNameFor(RaftDeviceID deviceID, DeviceTypeIndexType deviceTypeIndex) const;

    // Stage 10 endpoints
    RaftRetCode apiDevManTypes(const String &reqStr, String &respStr, const RaftJson& jsonParams);
    RaftRetCode apiDevManIdentInfo(const String &reqStr, String &respStr, const RaftJson& jsonParams);
    RaftRetCode apiDevManReInit(const String &reqStr, String &respStr, const RaftJson& jsonParams, const APISourceInfo& sourceInfo);
    RaftRetCode apiDevManDevEnable(const String &reqStr, String &respStr, const RaftJson& jsonParams);
    RaftRetCode apiDevManReserved(const String &reqStr, String &respStr, const RaftJson& jsonParams);

    // System configuration (for settings of other modules read for compatibility)
    RaftJsonIF& _sysConfig;

    // Device type file: extended records, overrides and disabled types loaded before devices are identified
    String _typeFilePath;
    bool _typeFileRequired = false;
    uint32_t _typeFileSourceId = UINT32_MAX;
    DeviceTypeFileLoader::Limits _typeFileLimits;
    DeviceTypeFileLoader::Result _typeFileResult;
    const char* _typeFileState = "none";            // loading, none, missing, error or applied
    bool _typeFilePresent = false;
    uint16_t _typeFileCrc16 = 0;
    uint32_t _typeFileRecordsAvailable = 0;         // extended records free before the file was loaded
    bool _typeFileChanged = false;                  // changed since it was loaded (markTypeFileChanged)

    /// @brief Check whether the device type file differs from the one loaded at startup (so a restart is needed to
    ///        apply it)
    bool isTypeFileRestartRequired();

    /// @brief Handle devman/reident (identify devices again)
    RaftRetCode apiDevManReIdent(const String &reqStr, String &respStr, const RaftJson& jsonParams);

    /// @brief Handle devman/typepolicy (set a device type's policy at runtime)
    RaftRetCode apiDevManTypePolicy(const String &reqStr, String &respStr, const RaftJson& jsonParams);

    /// @brief Read the device type file settings and register the file as a record source (before buses start)
    void setupTypeFile();

    /// @brief Load the device type file and mark its record source ready
    void loadTypeFile();

    /// @brief Handle devman/typefile (status) and devman/typefile/apply (restart to apply a new file)
    RaftRetCode apiDevManTypeFile(const String &reqStr, String &respStr, const RaftJson& jsonParams);

    /// @brief Handle devlib (status, with the fields of the feature/device-library branch's endpoint)
    RaftRetCode apiDevLib(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);

    // Last report time
    uint32_t _debugLastReportTimeMs = 0;

    // Envelope sequence counter for devbin messages
    uint8_t _devbinEnvelopeSeqCounter = 0;

    // Debug
    static constexpr const char* MODULE_PREFIX = "DevMan";
};
