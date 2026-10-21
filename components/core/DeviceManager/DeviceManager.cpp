////////////////////////////////////////////////////////////////////////////////
//
// DeviceManager.cpp
//
////////////////////////////////////////////////////////////////////////////////

#include <functional>
#include <algorithm>
#include <atomic>
#include "DeviceManager.h"
#include "DeviceTypeRecordDynamic.h"
#include "DeviceTypeRecords.h"
#include "RaftBusSystem.h"
#include "DeviceFactory.h"
#include "SysManager.h"
#include "RestAPIEndpointManager.h"
#include "DemoDevice.h"
#include "BusAddrStatus.h"
#include "RaftDevice.h"
#include "FileSystem.h"
#include "SpiramAwareAllocator.h"
#include "DeviceTypeDecodeVerifier.h"
#ifdef ESP_PLATFORM
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
#include "esp_heap_caps.h"
#define DEVMAN_TYPE_FILE_CHECK_TASK
#endif
#endif

// Warnings
#define WARN_ON_DEVICE_CLASS_NOT_FOUND
#define WARN_ON_DEVICE_INSTANTIATION_FAILED
#define WARN_ON_SETUP_DEVICE_FAILED
#define WARN_SYSMOD_RECV_CMD_JSON

// Debug
// #define DEBUG_BUS_OPERATION_STATUS_OK_CB
// #define DEBUG_BUS_ELEMENT_STATUS_CHANGES
// #define DEBUG_DEVICE_SETUP
// #define DEBUG_DEVICE_FACTORY
// #define DEBUG_LIST_DEVICES
// #define DEBUG_JSON_DEVICE_DATA
// #define DEBUG_BINARY_DEVICE_DATA
// #define DEBUG_JSON_DEVICE_HASH
// #define DEBUG_DEVMAN_API
// #define DEBUG_GET_DEVICE
// #define DEBUG_JSON_DEVICE_HASH_DETAIL
// #define DEBUG_MAKE_BUS_REQUEST_VERBOSE
// #define DEBUG_API_CMDRAW
// #define DEBUG_SYSMOD_GET_NAMED_VALUE
// #define DEBUG_SYSMOD_RECV_CMD_JSON
// #define DEBUG_LOOP_SHOW_DEVICES_INTERVAL_MS 1000
// #define DEBUG_DEVICE_CONFIG_API
// #define DEBUG_REGISTER_FOR_DEVICE_DATA
// #define DEBUG_DEVICE_NAMES

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
DeviceManager::DeviceManager(const char *pModuleName, RaftJsonIF& sysConfig)
    : RaftSysMod(pModuleName, sysConfig), _sysConfig(sysConfig)
{
    // Create mutex
    RaftMutex_init(_accessMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor
DeviceManager::~DeviceManager()
{
    // Delete mutex
    RaftMutex_destroy(_accessMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup
void DeviceManager::setup()
{
    // Limits for device type records added at runtime (extended records) - set before any is added
    // (0 = the default, which depends on whether the board has PSRAM)
    uint32_t maxExtendedRecords = modConfig().getLong("typeTable/maxExtendedRecords", 0);
    uint32_t maxExtendedBytes = modConfig().getLong("typeTable/maxExtendedBytes", 0);
    if (!deviceTypeRecords.setExtendedRecordLimits(maxExtendedRecords, maxExtendedBytes))
        LOG_W(MODULE_PREFIX, "setup type table limits not applied - records were added before setup");

    // Protected types and record policies from the SysType (before buses start identifying devices)
    applyTypeTableConfig(modConfig());

    // Settings for device types chosen by the board or product
    std::vector<String> typeSettingProblems;
    _typeSettings.parse(modConfig(), "typeSettings", typeSettingProblems);
    for (const String& problem : typeSettingProblems)
        LOG_W(MODULE_PREFIX, "setup typeSettings %s - ignored", problem.c_str());
    applyTypeScanPriorities(false);

    // Device type file: buses don't identify devices until it has been loaded (in postSetup, once the file system
    // is mounted)
    setupTypeFile();

    // Setup buses
    raftBusSystem.setup("Buses", modConfig(),
            std::bind(&DeviceManager::busElemStatusCB, this, std::placeholders::_1, std::placeholders::_2),
            std::bind(&DeviceManager::busOperationStatusCB, this, std::placeholders::_1, std::placeholders::_2)
    );

    // Load device names from config first so that setupStaticDevices can overwrite
    // entries for any device that provides a name in its Devices array entry.
    _deviceNames.loadFromConfig(modConfig());

    // Setup device classes (these are the keys into the device factory)
    setupStaticDevices("Devices", modConfig());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Apply the type table settings from the configuration
/// @param devManConfig DeviceManager configuration
/// @note Protected types: a Devices entry with role "system" and "expectType" protects that device type key - it
///       can't be disabled, overridden or replaced. Policies: "typePolicy" maps device type keys to "enabled",
///       "disabled" or "disabledClaim"
void DeviceManager::applyTypeTableConfig(RaftJsonIF& devManConfig)
{
    // Protected types
    std::vector<String> deviceConfigs;
    devManConfig.getArrayElems("Devices", deviceConfigs);
    for (RaftJson devConf : deviceConfigs)
    {
        String expectType = devConf.getString("expectType", "");
        if ((expectType.length() > 0) && devConf.getString("role", "").equalsIgnoreCase("system"))
            deviceTypeRecords.setProtected(expectType.c_str());
    }

    // Policies
    std::vector<String> policyKeys;
    devManConfig.getKeys("typePolicy", policyKeys);
    for (const String& key : policyKeys)
    {
        String policyStr = devManConfig.getString(("typePolicy/" + key).c_str(), "");
        uint8_t policy = DeviceTypeRecord::POLICY_ENABLED;
        if (policyStr.equalsIgnoreCase("disabled"))
        {
            policy = DeviceTypeRecord::POLICY_DISABLED;
        }
        else if (policyStr.equalsIgnoreCase("disabledClaim"))
        {
            policy = DeviceTypeRecord::POLICY_DISABLED_CLAIM;
        }
        else if (!policyStr.equalsIgnoreCase("enabled"))
        {
            LOG_W(MODULE_PREFIX, "applyTypeTableConfig typePolicy %s: unknown policy %s", key.c_str(), policyStr.c_str());
            continue;
        }
        DeviceTypeRecords::PolicyResult result = deviceTypeRecords.setPolicy(key.c_str(), policy);
        if (result != DeviceTypeRecords::PolicyResult::Set)
        {
            LOG_W(MODULE_PREFIX, "applyTypeTableConfig typePolicy %s %s: %s", key.c_str(), policyStr.c_str(),
                        DeviceTypeRecords::getPolicyResultStr(result));
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set the scan priorities of DevMan "typeSettings" on their types
/// @param reportUnknown log the settings for keys that aren't device types (after the type file is loaded)
void DeviceManager::applyTypeScanPriorities(bool reportUnknown)
{
    for (const DeviceTypeSettings::Entry& entry : _typeSettings.all())
    {
        DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
        bool known = deviceTypeRecords.getRecord(entry.typeKey.c_str(), deviceTypeIndex) != nullptr;
        if (known && (entry.scanPriority != DeviceTypeSettings::SCAN_PRIORITY_UNSET))
            deviceTypeRecords.setScanPriority(entry.typeKey.c_str(), (uint8_t)entry.scanPriority);
        if (!known && reportUnknown)
            LOG_I(MODULE_PREFIX, "typeSettings %s: not a device type in this build", entry.typeKey.c_str());
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Post setup
/// @note This handles post-setup for statically added devices (dynamic devices are handled separately)
void DeviceManager::postSetup()
{
    // Load the device type file (setup() of every SysMod has run, so the file system is mounted) - devices are
    // identified once it is loaded. One bulk update of the type table: the bus scanners (already running) rebuild
    // their lists once, not for each record added
    {
        DeviceTypeRecords::BulkUpdate bulkUpdate(deviceTypeRecords);
        loadTypeFile();
        loadDeviceSettings();

        // Scan priorities for types the device type file added
        applyTypeScanPriorities(true);
    }

    // Register JSON data source (message generator and state detector functions)
    getSysManager()->registerDataSource("Publish", "devjson", 
        [this](uint16_t topicIndex, CommsChannelMsg& msg) {
            String statusStr = getDevicesDataJSON(topicIndex);
            msg.setFromBuffer((uint8_t*)statusStr.c_str(), statusStr.length());
            return true;
        },
        [this](uint16_t topicIndex, std::vector<uint8_t>& stateHash) {
            return getDevicesHash(stateHash);
        }
    );    

    // Register binary data source
    getSysManager()->registerDataSource("Publish", "devbin", 
        [this](uint16_t topicIndex, CommsChannelMsg& msg) {
            std::vector<uint8_t> binaryData = getDevicesDataBinary(topicIndex);
            msg.setFromBuffer(binaryData.data(), binaryData.size());
            return true;
        },
        [this](uint16_t topicIndex, std::vector<uint8_t>& stateHash) {
            return getDevicesHash(stateHash);
        }
    );

    // Get a frozen copy of the static device list (null-pointers excluded)
    RaftDevice* pStaticDeviceListFrozen[DEVICE_LIST_MAX_SIZE];
    uint32_t numDevices = getStaticDeviceListFrozen(pStaticDeviceListFrozen, DEVICE_LIST_MAX_SIZE, false);

    // Call postSetup for each device
    for (uint32_t devIdx = 0; devIdx < numDevices; devIdx++)
    {
        pStaticDeviceListFrozen[devIdx]->postSetup();
    }

    // Register for device data change callbacks
#ifdef DEBUG_DEVICE_SETUP
    uint32_t numDevCBsRegistered = 
#endif
    postSetupRegisterDataCBs();

    // Register for device events
    for (uint32_t devIdx = 0; devIdx < numDevices; devIdx++)
    {
        pStaticDeviceListFrozen[devIdx]->registerForDeviceStatusChange(
            std::bind(&DeviceManager::deviceEventCB, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
        );
    }

    // Debug
#ifdef DEBUG_DEVICE_SETUP
    LOG_I(MODULE_PREFIX, "postSetup %d devices registered %d CBs", numDevices, numDevCBsRegistered);
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Loop (called frequently to handle device and bus servicing)
void DeviceManager::loop()
{
    // Service the buses (which will handle device status changes and data updates via callbacks)
    raftBusSystem.loop();

    // Complete re-initialisations that have finished
    if (!_pendingReInits.empty())
        servicePendingReInits();

    // A type file check or apply in progress
    if (_pTypeFileJob)
        serviceTypeFileJob();

#ifdef RAFT_RUNTIME_DECODE_VERIFY
    // Make the runtime decoders the decode verifier compares with the compiled ones
    deviceTypeDecodeVerifier.loop(deviceTypeRecords);
#endif

    // Get a frozen copy of the static device list for online devices
    RaftDevice* pStaticDeviceListFrozen[DEVICE_LIST_MAX_SIZE];
    uint32_t numDevices = getStaticDeviceListFrozen(pStaticDeviceListFrozen, DEVICE_LIST_MAX_SIZE, true);

    // Loop through the devices
    for (uint32_t devIdx = 0; devIdx < numDevices; devIdx++)
    {
        // Handle device loop
        pStaticDeviceListFrozen[devIdx]->loop();
    }

#if defined(DEBUG_LOOP_SHOW_DEVICES_INTERVAL_MS)
    if (Raft::isTimeout(millis(), _debugLastReportTimeMs, DEBUG_LOOP_SHOW_DEVICES_INTERVAL_MS))
    {
        // Get device list again this time including offline devices for debug reporting
        RaftDevice* pDeviceListAnyStatus[DEVICE_LIST_MAX_SIZE];
        bool deviceOnlineArray[DEVICE_LIST_MAX_SIZE];
        uint32_t numDevicesAnyStatus = getStaticDeviceListFrozen(pDeviceListAnyStatus, DEVICE_LIST_MAX_SIZE, false, deviceOnlineArray);
        LOG_I(MODULE_PREFIX, "Loop device list:");
        for (uint32_t devIdx = 0; devIdx < numDevicesAnyStatus; devIdx++)
        {
            RaftDevice* pDevice = pDeviceListAnyStatus[devIdx];

#ifdef DEBUG_INCLUDE_RAFT_DEVICE_CLASS_NAME
            LOG_I(MODULE_PREFIX, "  Device %d: ID %s class %s typeIdx %s status %s", 
                            devIdx, 
                            pDevice->getDeviceID().toString().c_str(),
                            pDevice->getDeviceClassName().c_str(),
                            pDevice->getDeviceTypeIndex() == DEVICE_TYPE_INDEX_INVALID ? "INVALID" : String(pDevice->getDeviceTypeIndex()).c_str(),
                            deviceOnlineArray[devIdx] ? "online" : "offline");
#else
            LOG_I(MODULE_PREFIX, "  Device %d: ID %s typeIdx %s status %s", 
                            devIdx, 
                            pDevice->getDeviceID().toString().c_str(),
                            pDevice->getDeviceTypeIndex() == DEVICE_TYPE_INDEX_INVALID ? "INVALID" : String(pDevice->getDeviceTypeIndex()).c_str(),
                            deviceOnlineArray[devIdx] ? "online" : "offline");
#endif
        }
        _debugLastReportTimeMs = millis();
    }
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Bus operation status callback
/// @param bus a reference to the bus which has changed status
/// @param busOperationStatus - indicates bus ok/failing
void DeviceManager::busOperationStatusCB(RaftBus& bus, BusOperationStatus busOperationStatus)
{
    // Debug
#ifdef DEBUG_BUS_OPERATION_STATUS_OK_CB
    LOG_I(MODULE_PREFIX, "busOperationStatusInfo %s %s", bus.getBusName().c_str(), 
        RaftBus::busOperationStatusToString(busOperationStatus));
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Bus element status callback
/// @param bus a reference to the bus which has changed status
/// @param statusChanges - an array of status changes (online/offline) for bus elements
void DeviceManager::busElemStatusCB(RaftBus& bus, const std::vector<BusAddrStatus>& statusChanges)
{
    // Check if the deviceID or deviceTypeIndex of any of the status changes matches a registered device data change callback and if so 
    // register with the bus devices interface to receive data updates for the relevant device data change callbacks
    for (const BusAddrStatus& addrStatus : statusChanges)
    {
        // Dispatch to listeners registered via DeviceManager::registerForDeviceStatusChange.
        // This must happen for all transitions (ONLINE/OFFLINE/PENDING_DELETION and for
        // isNewlyIdentified-only events), not just ONLINE, so that listeners like
        // RaftROS auto-publish can detach on offline and attach on identify.  Bus-
        // discovered devices don't normally have a RaftDevice in _staticDeviceList,
        // so fall back to a minimal stub carrying just the RaftDeviceID — listeners
        // dispatch on the ID + BusAddrStatus fields, not on derived RaftDevice state.
        {
            RaftDeviceID busDevID(bus.getBusNum(), addrStatus.address);
            RaftDevice* pDevice = getDevice(busDevID);
            if (pDevice)
            {
                callDeviceStatusChangeCBs(pDevice, addrStatus);
            }
            else
            {
                RaftDevice stubBusDevice("BusDevice", "{}", busDevID);
                callDeviceStatusChangeCBs(&stubBusDevice, addrStatus);
            }
        }

        // Check the device is online
        if (addrStatus.onlineState != DeviceOnlineState::ONLINE)
            continue;

        // A device that is expected to be a particular type (Devices entry "expectType") and was identified as
        // something else is reported - its users must not treat it as the expected type
        if (addrStatus.isNewlyIdentified)
        {
            RaftDeviceID busDevID(bus.getBusNum(), addrStatus.address);
            String expectedType = _deviceNames.getExpectedType(busDevID);
            const DeviceTypeRecord* pRec = deviceTypeRecords.getRecord(addrStatus.deviceTypeIndex);
            if ((expectedType.length() > 0) && (!pRec || !pRec->deviceType || !expectedType.equals(pRec->deviceType)))
            {
                LOG_E(MODULE_PREFIX, "busElemStatusCB device %s expected to be %s but identified as %s",
                            busDevID.toString().c_str(), expectedType.c_str(),
                            (pRec && pRec->deviceType) ? pRec->deviceType : "unknown");
            }
        }

        // Identity changes of a device that stays online are rare and matter to everything using it
        if ((addrStatus.identEvent == BusIdentEvent::TypeChanged) || (addrStatus.identEvent == BusIdentEvent::Lost))
        {
            LOG_I(MODULE_PREFIX, "busElemStatusCB device %s %s typeIdx %d (was %d)",
                        RaftDeviceID(bus.getBusNum(), addrStatus.address).toString().c_str(),
                        BusIdentSchedule::getEventStr(addrStatus.identEvent),
                        addrStatus.deviceTypeIndex == DEVICE_TYPE_INDEX_INVALID ? -1 : (int)addrStatus.deviceTypeIndex,
                        addrStatus.prevDeviceTypeIndex == DEVICE_TYPE_INDEX_INVALID ? -1 : (int)addrStatus.prevDeviceTypeIndex);
        }

        // Settings made with devconfig are applied again when a device is identified again as the same type
        if ((addrStatus.identEvent == BusIdentEvent::Identified) || (addrStatus.identEvent == BusIdentEvent::TypeChanged))
        {
            reapplyDevConfig(RaftDeviceID(bus.getBusNum(), addrStatus.address), addrStatus.deviceTypeIndex);
            applyPollingSetting(RaftDeviceID(bus.getBusNum(), addrStatus.address));
        }

        // Own the device's single bus data callback and fan it out to every
        // subscriber (registerForDeviceData), whenever they register - so a
        // subscription made after the device came online also takes effect,
        // and two subscribers to one device both receive its data.  Installed
        // after the status listeners above, as they may subscribe from there.
        RaftBusDevicesIF* pBusDevicesIF = bus.getBusDevicesIF();
        if (!pBusDevicesIF)
            continue;
        installBusDataDispatcher(*pBusDevicesIF, bus.getBusNum(), addrStatus.address);

#ifdef DEBUG_BUS_ELEMENT_STATUS_CHANGES
        LOG_I(MODULE_PREFIX, "busElemStatusCB bus %s address %x onlineState %s isNewlyIdentified %s deviceTypeIndex %u subscribers %d",
                    bus.getBusName().c_str(), addrStatus.address,
                    BusAddrStatus::getOnlineStateStr(addrStatus.onlineState),
                    addrStatus.isNewlyIdentified ? "Y" : "N",
                    addrStatus.deviceTypeIndex,
                    (int)_deviceDataSubscribers.count());
#endif

        // Dispatch of status-change listeners now happens at the top of
        // the loop body (before the ONLINE-only early-continue) so that
        // OFFLINE / PENDING_DELETION transitions are also delivered.
    }

#ifdef DEBUG_BUS_ELEMENT_STATUS_CHANGES
    LOG_I(MODULE_PREFIX, "busElemStatusCB bus %s numChanges %d", bus.getBusName().c_str(), statusChanges.size());
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup devices defined in the SysType configuration
/// @param pConfigPrefix prefix for the device configuration
/// @param devManConfig configuration for the device manager
/// @note Each entry in the configured array may be either:
///       (a) A static device entry — identified by the presence of a "class" field.
///           The device is instantiated via the factory and (if provided) its "name"
///           and "role" fields are applied to the DeviceManager's name/role maps.
///       (b) A bus-tagging entry — identified by the absence of a "class" field.
///           No factory instantiation occurs; the entry exists purely to associate a
///           friendly name and/or role with a bus-discovered device identified by
///           "bus" + "addr". An "enable":false entry is skipped entirely.
void DeviceManager::setupStaticDevices(const char* pConfigPrefix, RaftJsonIF& devManConfig)
{
    // Get devices config
    std::vector<String> deviceConfigs;
    devManConfig.getArrayElems(pConfigPrefix, deviceConfigs);
    for (RaftJson devConf : deviceConfigs)
    {
        // Check if enable is explicitly set to false
        if (!devConf.getBool("enable", true))
            continue;

        // Optional friendly name and role apply to both static and bus entries
        String name = devConf.getString("name", "");
        String role = devConf.getString("role", "");

        // Get class of device (presence indicates a static-device entry)
        String devClass = devConf.getString("class", "");

        if (devClass.length() == 0)
        {
            // ----- Bus-tagging entry (class absent) -----
            // Must identify a bus device via "bus" + "addr"
            String busName = devConf.getString("bus", "");
            String addrStr = devConf.getString("addr", "");
            if (busName.length() == 0 || addrStr.length() == 0)
                continue;
            RaftBus* pBus = raftBusSystem.getBusByName(busName, true);
            if (!pBus)
                continue;
            BusElemAddrType addr = (BusElemAddrType)strtoul(addrStr.c_str(), nullptr, 0);
            RaftDeviceID deviceID(pBus->getBusNum(), addr);
            if (name.length() > 0)
                setDeviceName(deviceID, name);
            if (role.length() > 0)
                setDeviceRole(deviceID, role);
            String expectType = devConf.getString("expectType", "");
            if (expectType.length() > 0)
                _deviceNames.setExpectedType(deviceID, expectType);
            continue;
        }

        // ----- Static-device entry (class present) -----

        // Find the device class in the factory
        const DeviceFactory::RaftDeviceClassDef* pDeviceClassDef = deviceFactory.findDeviceClass(devClass.c_str());
        if (!pDeviceClassDef)
        {
#ifdef WARN_ON_DEVICE_CLASS_NOT_FOUND
            LOG_W(MODULE_PREFIX, "setupStaticDevices %s class %s not found", pConfigPrefix, devClass.c_str());
#endif
            continue;
        }

        // Create the device
        RaftDevice* pDevice = addStaticDevice(pDeviceClassDef->pCreateFn, devClass.c_str(), devConf);
        if (!pDevice)
        {
#ifdef WARN_ON_DEVICE_INSTANTIATION_FAILED
            LOG_E(MODULE_PREFIX, "setupStaticDevices %s class %s create failed devConf %s", 
                        pConfigPrefix, devClass.c_str(), devConf.c_str());
#endif
            continue;
        }

        // Debug
#ifdef DEBUG_DEVICE_FACTORY
        {
            LOG_I(MODULE_PREFIX, "setup class %s devConf %s", 
                        devClass.c_str(), devConf.c_str());
        }
#endif

    }

    // Now call setup on instantiated devices
    for (auto& devRec : _staticDeviceList)
    {
        if (devRec.pDevice)
            setupStaticDevice(devRec.pDevice);
    }

    // Give each SysMod the opportunity to add endpoints and comms channels and to keep a
    // pointer to the CommsCoreIF that can be used to send messages
    for (auto& devRec : _staticDeviceList)
    {
        if (devRec.pDevice)
        {
            if (getRestAPIEndpointManager())
                devRec.pDevice->addRestAPIEndpoints(*getRestAPIEndpointManager());
            if (getCommsCore())
                devRec.pDevice->addCommsChannels(*getCommsCore());
        }            
    }

#ifdef DEBUG_LIST_DEVICES
    uint32_t deviceIdx = 0;
    for (auto& devRec : _staticDeviceList)
    {
        LOG_I(MODULE_PREFIX, "Device %d: %s", deviceIdx++, 
                devRec.pDevice ? devRec.pDevice->getConfiguredDeviceName().c_str() : "UNKNOWN");
            
    }
    if (_staticDeviceList.size() == 0)
        LOG_I(MODULE_PREFIX, "No devices found");
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Create a static device and add it to the static device list
/// @param pCreateFn create function for the device's class
/// @param pDevClass device class name
/// @param devConf device configuration (its optional "name" and "role" are applied)
/// @return the device, or nullptr if it could not be created
/// @note the device is given the next direct-connection device ID; call setupStaticDevice() to set it up
RaftDevice* DeviceManager::addStaticDevice(RaftDeviceCreateFn pCreateFn, const char* pDevClass, RaftJson& devConf)
{
    RaftDevice* pDevice = pCreateFn ? pCreateFn(pDevClass, devConf.c_str()) : nullptr;
    if (!pDevice)
        return nullptr;

    // Set deviceID and associate with this DeviceManager
    RaftDeviceID deviceID;
    if (!RaftMutex_lock(_accessMutex, ACCESS_MUTEX_MAX_WAIT_MS))
    {
        delete pDevice;
        return nullptr;
    }
    deviceID = RaftDeviceID(RaftDeviceID::BUS_NUM_DIRECT_CONN, _staticDeviceList.size());
    pDevice->setDeviceID(deviceID);
    pDevice->setDeviceManager(this);
    _staticDeviceList.push_back({pDevice, true});
    RaftMutex_unlock(_accessMutex);

    // Apply name and role (if provided) to DeviceManager's central maps
    String name = devConf.getString("name", "");
    String role = devConf.getString("role", "");
    if (name.length() > 0)
        setDeviceName(deviceID, name);
    if (role.length() > 0)
        setDeviceRole(deviceID, role);
    return pDevice;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set up a static device and register its device type record (if it has one)
/// @param pDevice device
void DeviceManager::setupStaticDevice(RaftDevice* pDevice)
{
#ifdef DEBUG_DEVICE_SETUP            
    LOG_I(MODULE_PREFIX, "setup pDevice %p name %s", pDevice, pDevice->getConfiguredDeviceName().c_str());
#endif
    // Setup device
    pDevice->setup();

    // See if the device has a device type record
    DeviceTypeRecordDynamic devTypeRec;
    if (pDevice->getDeviceTypeRecord(devTypeRec))
    {
        // Add the device type record to the device type records. If it can't be added the device has
        // no type (never index 0, which is another record)
        DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
        DeviceTypeRecords::AddResult addResult = deviceTypeRecords.addRecord(devTypeRec, deviceTypeIndex);
        if (addResult == DeviceTypeRecords::AddResult::Conflict)
        {
            LOG_E(MODULE_PREFIX, "setup device %s type %s differs from an existing type with the same key - "
                        "give this device a distinct type in its configuration",
                        pDevice->getConfiguredDeviceName().c_str(), devTypeRec.deviceTypeName.c_str());
        }
        else if ((addResult != DeviceTypeRecords::AddResult::Added) &&
                    (addResult != DeviceTypeRecords::AddResult::AlreadyPresent) &&
                    (addResult != DeviceTypeRecords::AddResult::Replaced))
        {
            DeviceTypeRecords::ExtendedRecordStats stats = deviceTypeRecords.getExtendedRecordStats();
            LOG_E(MODULE_PREFIX, "setup device %s type %s not added (%s, %d of %d records, %d of %d bytes)",
                        pDevice->getConfiguredDeviceName().c_str(), devTypeRec.deviceTypeName.c_str(),
                        DeviceTypeRecords::getAddResultStr(addResult), (int)stats.count, (int)stats.maxRecords,
                        (int)stats.bytesUsed, (int)stats.maxBytes);
        }
        pDevice->setDeviceTypeIndex(deviceTypeIndex);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get devices' data as JSON
/// @param topicIndex Topic index (embedded as integer _t field)
/// @return JSON string
String DeviceManager::getDevicesDataJSON(uint16_t topicIndex) const
{
    // Pre-allocate string capacity to avoid multiple reallocations
    // Estimate: ~200 bytes per device/bus element
    String jsonStr;
    jsonStr.reserve(1024);
    
    // Start JSON object with topic index and version
    jsonStr += "{\"_t\":";
    jsonStr += String(topicIndex);
    jsonStr += ",\"_v\":1";
    bool hasDeviceData = false;

    // Check all buses for data
    for (RaftBus* pBus : raftBusSystem.getBusList())
    {
        if (!pBus)
            continue;
        // Get device interface
        RaftBusDevicesIF* pDevicesIF = pBus->getBusDevicesIF();
        if (!pDevicesIF)
            continue;
        String jsonRespStr = pDevicesIF->getQueuedDeviceDataJson();

        // Check for empty string or empty JSON object
        if (jsonRespStr.length() > 2)
        {
            char prefix[256];
            snprintf(prefix, sizeof(prefix), ",\"%d\":", (int)pBus->getBusNum());
            jsonStr += prefix;
            jsonStr += jsonRespStr;
            hasDeviceData = true;
        }
    }

    // Get a frozen copy of the static device list (null-pointers excluded)
    RaftDevice* pStaticDeviceListFrozen[DEVICE_LIST_MAX_SIZE];
    uint32_t numDevices = getStaticDeviceListFrozen(pStaticDeviceListFrozen, DEVICE_LIST_MAX_SIZE, true);

    // Loop through the all devices
    for (uint32_t devIdx = 0; devIdx < numDevices; devIdx++)
    {
        RaftDevice* pDevice = pStaticDeviceListFrozen[devIdx];
        String jsonRespStr = pDevice->getStatusJSON();

        // Check for empty string or empty JSON object
        if (jsonRespStr.length() > 2)
        {
            // Add the response under the key for BUS_NUM_DIRECT_CONN and the device index (e.g. "0", "1", etc.)
            char prefix[50];
            snprintf(prefix, sizeof(prefix), ",\"%d\":", (int)RaftDeviceID::BUS_NUM_DIRECT_CONN);
            jsonStr += prefix;
            jsonStr += jsonRespStr;
            hasDeviceData = true;
        }
    }

#ifdef DEBUG_JSON_DEVICE_DATA
    LOG_I(MODULE_PREFIX, "getDevicesDataJSON %s", jsonStr.c_str());
#endif

    // Return empty string if no device data
    if (!hasDeviceData)
        return "";

    jsonStr += "}";
    return jsonStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get devices' data as binary
/// @param topicIndex Topic index (embedded in envelope header)
/// @return Binary data vector
std::vector<uint8_t> DeviceManager::getDevicesDataBinary(uint16_t topicIndex)
{
    std::vector<uint8_t> binaryData;
    binaryData.reserve(502);

    // Envelope header: magic+version byte (0xDB = devbin) followed by topic index and envelope sequence counter
    binaryData.push_back(0xDB);
    binaryData.push_back(topicIndex <= 0xFE ? (uint8_t)topicIndex : 0xFF);
    binaryData.push_back(_devbinEnvelopeSeqCounter++);

    // Add bus data
    for (RaftBus* pBus : raftBusSystem.getBusList())
    {
        if (!pBus)
            continue;
        RaftBusDevicesIF* pDevicesIF = pBus->getBusDevicesIF();
        if (!pDevicesIF)
            continue;

        // Add the bus data
        std::vector<uint8_t> busBinaryData = pDevicesIF->getQueuedDeviceDataBinary(pBus->getBusNum());
        binaryData.insert(binaryData.end(), busBinaryData.begin(), busBinaryData.end());
    }

    // Get a frozen copy of the device list (null-pointers excluded)
    RaftDevice* pStaticDeviceListFrozen[DEVICE_LIST_MAX_SIZE];
    uint32_t numDevices = getStaticDeviceListFrozen(pStaticDeviceListFrozen, DEVICE_LIST_MAX_SIZE, true);

    // Loop through the devices
    for (uint32_t devIdx = 0; devIdx < numDevices; devIdx++)
    {
        RaftDevice* pDevice = pStaticDeviceListFrozen[devIdx];
        std::vector<uint8_t> deviceBinaryData = pDevice->getStatusBinary();
        binaryData.insert(binaryData.end(), deviceBinaryData.begin(), deviceBinaryData.end());

#ifdef DEBUG_BINARY_DEVICE_DATA
        LOG_I(MODULE_PREFIX, "getDevicesDataBinary DEV %s hex %s", 
                pDevice->getConfiguredDeviceName().c_str(), Raft::getHexStr(deviceBinaryData.data(), deviceBinaryData.size()).c_str());
#endif        
    }

    return binaryData;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check for change of devices' data
/// @param stateHash hash of the currently available data
void DeviceManager::getDevicesHash(std::vector<uint8_t>& stateHash) const
{
    // Initialize hash to two bytes
    stateHash.assign(2, 0);

    // Check all buses for data
    for (RaftBus* pBus : raftBusSystem.getBusList())
    {
        // Check bus
        if (pBus)
        {
            // Check bus status
            uint32_t identPollLastMs = pBus->getDeviceInfoTimestampMs(true, true);
            stateHash[0] ^= (identPollLastMs & 0xff);
            stateHash[1] ^= ((identPollLastMs >> 8) & 0xff);

#ifdef DEBUG_JSON_DEVICE_HASH_DETAIL
            LOG_I(MODULE_PREFIX, "getDevicesHash %s ms %d %02x%02x", 
                    pBus->getBusName().c_str(), (int)identPollLastMs, stateHash[0], stateHash[1]);
#endif
        }
    }

    // Get a frozen copy of the device list (null-pointers excluded)
    RaftDevice* pStaticDeviceListFrozen[DEVICE_LIST_MAX_SIZE];
    uint32_t numDevices = getStaticDeviceListFrozen(pStaticDeviceListFrozen, DEVICE_LIST_MAX_SIZE, true);

    // Loop through the devices
    for (uint32_t devIdx = 0; devIdx < numDevices; devIdx++)
    {
        // Check device status
        RaftDevice* pDevice = pStaticDeviceListFrozen[devIdx];
        uint32_t deviceStateHash = pDevice->getDeviceStateHash();
        stateHash[0] ^= (deviceStateHash & 0xff);
        stateHash[1] ^= ((deviceStateHash >> 8) & 0xff);

#ifdef DEBUG_JSON_DEVICE_HASH_DETAIL
        LOG_I(MODULE_PREFIX, "getDevicesHash %s hash %08x %02x%02x", 
                pDevice->getConfiguredDeviceName().c_str(), deviceStateHash, stateHash[0], stateHash[1]);
#endif
    }

    // Debug
#ifdef DEBUG_JSON_DEVICE_HASH
    LOG_I(MODULE_PREFIX, "getDevicesHash => %02x%02x", stateHash[0], stateHash[1]);
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get JSON status string
/// @return JSON string
String DeviceManager::getDebugJSON() const
{
    // JSON strings
    String jsonStrBus;

    // Check all buses for data
    for (RaftBus* pBus : raftBusSystem.getBusList())
    {
        if (!pBus)
            continue;
        // Get device interface
        RaftBusDevicesIF* pDevicesIF = pBus->getBusDevicesIF();
        if (!pDevicesIF)
            continue;
        String jsonRespStr = pDevicesIF->getDebugJSON(true);

        // Check for empty string or empty JSON object
        if (jsonRespStr.length() > 2)
        {
            jsonStrBus += (jsonStrBus.length() == 0 ? "\"" : ",\"") + pBus->getBusName() + "\":" + jsonRespStr;
        }
    }

    // Get a frozen copy of the device list (null-pointers excluded)
    RaftDevice* pStaticDeviceListFrozen[DEVICE_LIST_MAX_SIZE];
    bool deviceOnlineArray[DEVICE_LIST_MAX_SIZE];
    uint32_t numDevices = getStaticDeviceListFrozen(pStaticDeviceListFrozen, DEVICE_LIST_MAX_SIZE, false, deviceOnlineArray);

    // Loop through the devices
    String jsonStrDev;
    for (uint32_t devIdx = 0; devIdx < numDevices; devIdx++)
    {
        RaftDevice* pDevice = pStaticDeviceListFrozen[devIdx];
        String jsonRespStr = pDevice->getDebugJSON(false);

        // Check for empty string or empty JSON object
        if (jsonRespStr.length() > 2)
        {
            jsonStrDev += (jsonStrDev.length() == 0 ? "\"" : ",\"") + String(pDevice->getDeviceTypeIndex()) + "\":{\"online\":" + (deviceOnlineArray[devIdx] ? "1" : "0") + "," + jsonRespStr + "}";
        }
    }

    return "{" + (jsonStrBus.length() == 0 ? (jsonStrDev.length() == 0 ? "" : jsonStrDev) : (jsonStrDev.length() == 0 ? jsonStrBus : jsonStrBus + "," + jsonStrDev)) + "}";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get named value from device
/// @param pValueName Name in format "DeviceName.paramName"
/// @param isValid (out) true if value is valid
/// @return double value
/// @note This supports static devices and bus devices with names from the DeviceNames map
double DeviceManager::getNamedValue(const char* pValueName, bool& isValid)
{
    if (!pValueName)
        return 0.0;
    // Parse valueName as "deviceName.paramName"
    String valueNameStr(pValueName);
    int dotPos = valueNameStr.indexOf('.');
    if (dotPos > 0)
    {
        String deviceName = valueNameStr.substring(0, dotPos);
        String paramName = valueNameStr.substring(dotPos + 1);

        // Try static device first (existing path)
        RaftDevice* pDevice = getDevice(deviceName.c_str());
        if (pDevice) 
        {
            double val = pDevice->getNamedValue(paramName.c_str(), isValid);
#ifdef DEBUG_SYSMOD_GET_NAMED_VALUE
            LOG_I("DeviceManager", "getNamedValue: device=%s param=%s result: %f (valid=%d)", 
                        deviceName.c_str(), paramName.c_str(), val, isValid);
#endif
            return val;
        }

        // Try named bus device
        RaftDeviceID namedDeviceID;
        if (_deviceNames.resolveName(deviceName, namedDeviceID))
        {
            double val = getBusDeviceNamedValue(namedDeviceID, paramName, isValid);
#ifdef DEBUG_SYSMOD_GET_NAMED_VALUE
            LOG_I("DeviceManager", "getNamedValue busDevice: name=%s param=%s result: %f (valid=%d)",
                        deviceName.c_str(), paramName.c_str(), val, isValid);
#endif
            return val;
        }

        // Try raw address as RaftDeviceID fallback
        RaftDeviceID deviceID = RaftDeviceID::fromString(deviceName);
        if (deviceID.isValid() && deviceID.getBusNum() != RaftDeviceID::BUS_NUM_DIRECT_CONN)
        {
            double val = getBusDeviceNamedValue(deviceID, paramName, isValid);
#ifdef DEBUG_SYSMOD_GET_NAMED_VALUE
            LOG_I("DeviceManager", "getNamedValue busDeviceRaw: id=%s param=%s result: %f (valid=%d)",
                        deviceName.c_str(), paramName.c_str(), val, isValid);
#endif
            return val;
        }
    }
#ifdef DEBUG_SYSMOD_GET_NAMED_VALUE
    LOG_W("DeviceManager", "getNamedValue failed: valueName=%s", pValueName);
#endif
    isValid = false;
    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set named value in device
/// @param pValueName Name in format "DeviceName.paramName"
/// @param value Value to set
/// @return true if set successfully
/// @note This supports static devices and bus devices with names from the DeviceNames map
bool DeviceManager::setNamedValue(const char* pValueName, double value)
{

    if (!pValueName)
         return false;
    // Parse valueName as "deviceName.paramName"
    String valueNameStr(pValueName);
    int dotPos = valueNameStr.indexOf('.');
    if (dotPos > 0)
    {
        String deviceName = valueNameStr.substring(0, dotPos);
        String paramName = valueNameStr.substring(dotPos + 1);

        // Try static device first
        RaftDevice* pDevice = getDevice(deviceName.c_str());
        if (pDevice)
        {
            pDevice->setNamedValue(paramName.c_str(), value);
            return true;
        }

        // Try named bus device — resolve name to deviceID
        RaftDeviceID deviceID;
        if (!_deviceNames.resolveName(deviceName, deviceID))
            deviceID = RaftDeviceID::fromString(deviceName);

        if (deviceID.isValid() && deviceID.getBusNum() != RaftDeviceID::BUS_NUM_DIRECT_CONN)
        {
            // For bus devices, route to the bus via sendCmdToDevice
            // The bus device action routing is device-type-specific and would require
            // DeviceTypeRecord lookup — for now log and return false
            // (full action routing is a future enhancement)
#ifdef DEBUG_SYSMOD_GET_NAMED_VALUE
            LOG_I("DeviceManager", "setNamedValue: bus device %s.%s = %f (bus device set not yet implemented)",
                        deviceName.c_str(), paramName.c_str(), value);
#endif
        }
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get named string from device
/// @param pValueName Name in format "DeviceName.paramName"
/// @param isValid (out) true if value is valid
/// @return String value
String DeviceManager::getNamedString(const char* pValueName, bool& isValid)
{
    // TODO - this only supports static devices ...
    if (!pValueName)
        return "";
    // Parse valueName as "deviceName.paramName"
    String valueNameStr(pValueName);
    int dotPos = valueNameStr.indexOf('.');
    if (dotPos > 0)
    {
        String deviceName = valueNameStr.substring(0, dotPos);
        String paramName = valueNameStr.substring(dotPos + 1);
        RaftDevice* pDevice = getDevice(deviceName.c_str());
        if (pDevice)
        {
            return pDevice->getNamedString(paramName.c_str(), isValid);
        }
    }
    isValid = false;
    return "";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set named string in device
/// @param pValueName Name in format "DeviceName.paramName"
/// @param value Value to set
/// @return true if set successfully
/// @note This only supports static devices (i.e. devices defined in config and created in setupStaticDevices)
bool DeviceManager::setNamedString(const char* pValueName, const char* value)
{
    if(!pValueName || !value)
        return false;

    // Parse valueName as "deviceName.paramName"
    String valueNameStr(pValueName);
    int dotPos = valueNameStr.indexOf('.');
    if (dotPos > 0)
    {
        String deviceName = valueNameStr.substring(0, dotPos);
        String paramName = valueNameStr.substring(dotPos + 1);
        RaftDevice* pDevice = getDevice(deviceName.c_str());
        if (pDevice)
        {
            return pDevice->setNamedString(paramName.c_str(), value);
        }
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Receive JSON command and route to device
/// @param cmdJSON JSON command with optional "device" field
/// @param pRespStr (optional) pointer to string to receive response (if nullptr then response is not returned)
/// @return RaftRetCode
RaftRetCode DeviceManager::receiveCmdJSON(const char* cmdJson, String* pRespStr)
{
    // Get the deviceID and bus
    RaftDeviceID deviceID;
    RaftBus* pBus = nullptr;
    String errorStr;
    RaftJson cmdJsonObj(cmdJson);
    RaftRetCode retc = resolveDeviceIDAndBus(cmdJsonObj, deviceID, pBus, errorStr);
    if (retc != RAFT_OK)
    {
        if (pRespStr)
            *pRespStr = errorStr;
        return retc;
    }

    // Get static device if present
    RaftDevice* pDevice = getDevice(deviceID);
    if (pDevice) 
    {
        RaftRetCode ret = pDevice->sendCmdJSON(cmdJson);
#ifdef DEBUG_SYSMOD_RECV_CMD_JSON
        LOG_I("DeviceManager", "receiveCmdJSON result: %d", ret);
#else
        return ret;
#endif
    }

    // Send to bus if bus specified and device not found or device is on a bus
    if (pBus)
    {
        RaftBusDevicesIF* pDevicesIF = pBus->getBusDevicesIF();
        if (pDevicesIF)
        {
            RaftRetCode ret = pDevicesIF->sendCmdToDevice(deviceID, cmdJson, pRespStr);

#ifdef DEBUG_SYSMOD_RECV_CMD_JSON
            LOG_I("DeviceManager", "receiveCmdJSON bus result: %d", ret);
#endif
            return ret;
        }
    }

#ifdef WARN_SYSMOD_RECV_CMD_JSON
    LOG_W("DeviceManager", "receiveCmdJSON failed: no device specified");
#endif
    // No device specified, not handled
    if (pRespStr)
        *pRespStr = "failNoDeviceSpecified";
    return RAFT_INVALID_OPERATION;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Add REST API endpoints for device manager
/// @param endpointManager reference to the REST API endpoint manager to add endpoints to
void DeviceManager::addRestAPIEndpoints(RestAPIEndpointManager &endpointManager)
{
    // REST API endpoints
    endpointManager.addEndpoint("devman", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                            std::bind(&DeviceManager::apiDevMan, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                            " devman/typeinfo?deviceTypeKey=<key>|deviceTypeIndex=<n>|type=<typeKeyOrIndex> - Get type info,"
                            " devman/typeinfo?deviceid=<deviceId> - Get type info for a specific device (includes name/role sidecar fields when set),"
                            " devman/cmdraw?deviceid=<deviceId>&hexWr=<hexWriteData>&numToRd=<numBytesToRead>&msgKey=<msgKey> - Send raw command to device,"
                            " devman/cmdjson?body=<jsonCommand> - Send JSON command to device (requires 'device' field in JSON),"
                            " devman/devconfig?deviceid=<deviceId>&intervalUs=<microseconds>&numSamples=<count>&sampleRateHz=<hz>&busHz=<hz>&busHzSlots=<csv> - device configuration,"
                            " devman/busname?busnum=<busNumber> - Get bus name from bus number,"
                            " devman/demo?deviceTypeKey=<key>&rate=<sampleRateMs>&duration=<durationMs>&offlineIntvS=<N>&offlineDurS=<M> - Start demo device,"
                            " devman/setname?deviceid=<deviceId>&name=<friendlyName> - Assign friendly name to a device,"
                            " devman/setrole?deviceid=<deviceId>&role=<role> - Assign role (e.g. system) to a device (in-memory),"
                            " devman/listdevs?role=<roleFilter> - Enumerate instantiated/detected devices (optional role filter),"
                            " devman/slot?bus=<busNameOrNum>&slot=<n>&mode=<i2c|serial-full|serial-half> - Set slot mode (omit mode to query),"
                            " devman/busstatus[?busnum=<n>&reset=1] - Per-bus diagnostics (poll-response integrity, poll timing, worker activity, identification timing; reset=1 restarts poll timing after reporting),"
                            " devman/typefile[?entries=1] - Device type file status (path, state, counts, first error, CRC, capacity; entries: what happened to each entry),"
                            " devman/typefile/apply[?file=<path>] - Restart to apply a changed device type file (file: check that file first and use it as the device type file if nothing in it is rejected),"
                            " devman/typefile/check[?file=<path>] - What a device type file would do at the next startup (per entry), without changing anything,"
                            " devman/reident?deviceid=<deviceId>|deviceTypeKey=<key>|deviceTypeIndex=<n>|type=<typeKeyOrIndex>|all=1|unidentified=1 - Identify devices again (an identified device keeps its type until a different one is found),"
                            " devman/typepolicy?deviceTypeKey=<key>|deviceTypeIndex=<n>|type=<typeKeyOrIndex>&policy=<enabled|disabled|disabledClaim>&save=<0|1> - Set a device type's policy and identify the devices it affects again (save=1 also writes it to the type file, applied at the next restart),"
                            " devman/types?offset=<n>&count=<n> - Device type table (index, key, source, policy),"
                            " devman/identinfo?deviceid=<deviceId> - Identification state of a device (attempts, candidates in order),"
                            " devman/reinit?deviceid=<deviceId> - Run a device's initialisation again,"
                            " devman/devenable?deviceid=<deviceId>&poll=<0|1>&save=<0|1> - Stop or resume polling a device,"
                            " devman/reserved - Addresses claimed by device type records,"
#ifdef RAFT_RUNTIME_DECODE_VERIFY
                            " devman/decodeverify[?reset=1] - Compiled and runtime decoders compared on every poll result, and init values (debug build: RAFT_RUNTIME_DECODE_VERIFY; reset=1 clears the results after reporting),"
#endif
                            " Note: typeKeyOrIndex can be either a device type key or a device type index"
                            " Note: deviceId=<deviceId> can be replaced with bus=<busNameOrNumber>&addr=<addr>");
    endpointManager.addEndpoint("devlib", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                            std::bind(&DeviceManager::apiDevLib, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                            "Device type file status (as devman/typefile, with the fields of the device library endpoint)");
    LOG_I(MODULE_PREFIX, "addRestAPIEndpoints added devman");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief REST API endpoint for device manager operations
/// @param reqStr request string containing the command and parameters
/// @param respStr (out) response string to be filled with the result
/// @param sourceInfo information about the source of the API call
/// @return RaftRetCode indicating success or failure of the operation
RaftRetCode DeviceManager::apiDevMan(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    (void)sourceInfo;

    // Get request parameters
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    RestAPIEndpointManager::getParamsAndNameValues(reqStr.c_str(), params, nameValues);
    RaftJson jsonParams = RaftJson::getJSONFromNVPairs(nameValues, true);

    // Get command
    String cmdName = reqStr;
    if (params.size() > 1)
        cmdName = params[1];

    if (cmdName.equalsIgnoreCase("typeinfo"))
        return apiDevManTypeInfo(reqStr, respStr, jsonParams);
    if (cmdName.equalsIgnoreCase("cmdraw"))
        return apiDevManCmdRaw(reqStr, respStr, jsonParams, sourceInfo);
    if (cmdName.equalsIgnoreCase("cmdjson"))
        return apiDevManCmdJson(reqStr, respStr, jsonParams, sourceInfo);
#ifdef DEVICE_MANAGER_ENABLE_DEMO_DEVICE
    if (cmdName.equalsIgnoreCase("demo"))
        return apiDevManDemo(reqStr, respStr, jsonParams);
#endif
    if (cmdName.equalsIgnoreCase("devconfig"))
        return apiDevManDevConfig(reqStr, respStr, jsonParams);
    if (cmdName.equalsIgnoreCase("busname"))
        return apiDevManBusName(reqStr, respStr, jsonParams);
    if (cmdName.equalsIgnoreCase("setname"))
        return apiDevManSetName(reqStr, respStr, jsonParams);
    if (cmdName.equalsIgnoreCase("setrole"))
        return apiDevManSetRole(reqStr, respStr, jsonParams);
    if (cmdName.equalsIgnoreCase("listdevs"))
        return apiDevManListDevs(reqStr, respStr, jsonParams);
    if (cmdName.equalsIgnoreCase("slot"))
        return apiDevManSlot(reqStr, respStr, jsonParams);
    if (cmdName.equalsIgnoreCase("busstatus"))
        return apiDevManBusStatus(reqStr, respStr, jsonParams);
    if (cmdName.equalsIgnoreCase("typefile"))
        return apiDevManTypeFile(reqStr, respStr, jsonParams, sourceInfo);
#ifdef RAFT_RUNTIME_DECODE_VERIFY
    if (cmdName.equalsIgnoreCase("decodeverify"))
    {
        String json = deviceTypeDecodeVerifier.getJson();
        if (jsonParams.getLong("reset", 0) != 0)
            deviceTypeDecodeVerifier.reset();
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, json.c_str());
    }
#endif
    if (cmdName.equalsIgnoreCase("reident"))
        return apiDevManReIdent(reqStr, respStr, jsonParams);
    if (cmdName.equalsIgnoreCase("types"))
        return apiDevManTypes(reqStr, respStr, jsonParams);
    if (cmdName.equalsIgnoreCase("identinfo"))
        return apiDevManIdentInfo(reqStr, respStr, jsonParams);
    if (cmdName.equalsIgnoreCase("reinit"))
        return apiDevManReInit(reqStr, respStr, jsonParams, sourceInfo);
    if (cmdName.equalsIgnoreCase("devenable"))
        return apiDevManDevEnable(reqStr, respStr, jsonParams);
    if (cmdName.equalsIgnoreCase("reserved"))
        return apiDevManReserved(reqStr, respStr, jsonParams);
    if (cmdName.equalsIgnoreCase("typepolicy"))
        return apiDevManTypePolicy(reqStr, respStr, jsonParams);

    return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failUnknownCmd");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Report per-bus status JSON (bus-specific diagnostics, e.g. poll-response
///        integrity counters, poll timing, worker activity). Optional "busnum" selects one bus;
///        otherwise all buses are reported. Buses with nothing to report return an empty object.
///        Optional "reset=1" resets the statistics that measure over a period after reporting them.
RaftRetCode DeviceManager::apiDevManBusStatus(const String &reqStr, String &respStr, const RaftJson& jsonParams)
{
    // NOTE: BUS_NUM_FIRST_BUS is uint32_t, so comparing a negative "not specified"
    // sentinel against it directly promotes to unsigned and is always true. Cast to
    // int so the sentinel behaves as intended.
    const int busNum = jsonParams.getLong("busnum", -1);
    const bool filterByBusNum = (busNum >= (int)RaftDeviceID::BUS_NUM_FIRST_BUS);
    const bool resetAfterReport = jsonParams.getBool("reset", false);
    String busesJson;
    for (RaftBus* pBus : raftBusSystem.getBusList())
    {
        if (!pBus)
            continue;
        if (filterByBusNum && ((int)pBus->getBusNum() != busNum))
            continue;
        if (busesJson.length() > 0)
            busesJson += ",";
        busesJson += "\"" + pBus->getBusName() + "\":" + pBus->getBusStatusJson();
        if (resetAfterReport)
            pBus->resetBusStatus();
    }
    if (filterByBusNum && (busesJson.length() == 0))
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusNotFound");

    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true,
                                   ("\"buses\":{" + busesJson + "}").c_str());
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get type information for a device
/// @param reqStr request string containing the command and parameters
/// @param respStr (out) response string to be filled with the result
/// @param jsonParams JSON object containing the parameters for the command, expected to have a "type" field which can be either a device type key or a device type index
/// @return RaftRetCode indicating success or failure of the operation, with the response containing the device type information in JSON format if successful
RaftRetCode DeviceManager::apiDevManTypeInfo(const String &reqStr, String &respStr, const RaftJson& jsonParams)
{
    // Per-device lookup mode: caller supplies "deviceid" or ("bus" + "addr").
    // In this mode we look up the device's actual installed type index and emit
    // optional sidecar fields "name" and "role" (omitted when at their default values).
    // NOTE: a request with only "bus" (no "addr") is NOT per-device mode — it falls
    // through to the legacy type key/index lookup for backward compatibility (e.g. the
    // WebUI client passes `?bus=<name>&type=<type>` to scope a type lookup).
    // Both modes also return "deviceTypeKey", the device type key of the record.
    String deviceIdStr = jsonParams.getString("deviceid", "");
    String busName = jsonParams.getString("bus", "");
    String addrStr = jsonParams.getString("addr", "");
    if ((deviceIdStr.length() > 0) || ((busName.length() > 0) && (addrStr.length() > 0)))
    {
        RaftDeviceID deviceID = RaftDeviceID::INVALID;
        if (deviceIdStr.length() > 0)
        {
            deviceID = RaftDeviceID::fromString(deviceIdStr);
        }
        else
        {
            RaftBus* pBus = raftBusSystem.getBusByName(busName, true);
            if (!pBus)
                return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusNotFound");
            BusElemAddrType addr = (BusElemAddrType)strtoul(addrStr.c_str(), nullptr, 0);
            deviceID = RaftDeviceID(pBus->getBusNum(), addr);
        }
        if (!deviceID.isValid())
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failInvalidDeviceId");

        // Resolve device type index. For static devices use the in-memory RaftDevice;
        // for bus devices use the bus's detection state (undetected -> not found).
        DeviceTypeIndexType resolvedTypeIndex = DEVICE_TYPE_INDEX_INVALID;
        if (deviceID.getBusNum() == RaftDeviceID::BUS_NUM_DIRECT_CONN)
        {
            RaftDevice* pDevice = getDevice(deviceID);
            if (!pDevice)
                return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failDeviceNotFound");
            resolvedTypeIndex = pDevice->getDeviceTypeIndex();
        }
        else
        {
            RaftBus* pBus = raftBusSystem.getBusByNumber(deviceID.getBusNum());
            if (!pBus)
                return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusNotFound");
            resolvedTypeIndex = pBus->getDeviceTypeIndex(deviceID.getAddress());
            if (resolvedTypeIndex == DEVICE_TYPE_INDEX_INVALID)
                return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failDeviceNotFound");
        }

        String devInfo = deviceTypeRecords.getDevTypeInfoJsonByTypeIdx(resolvedTypeIndex, false);
        bool hasDevInfo = ((devInfo.length() > 0) && (devInfo != "{}"));

        // Build response. For static devices (and any others) without a type-record entry,
        // omit "devinfo" but still return name/role/dtIdx so callers can still query
        // per-instance metadata.
        String extras;
        if (hasDevInfo)
            extras = "\"devinfo\":" + devInfo + ",\"dtIdx\":" + String(resolvedTypeIndex);
        else
            extras = "\"dtIdx\":" + String((int)resolvedTypeIndex);
        appendDeviceTypeKey(extras, resolvedTypeIndex);
        appendDecoderKind(extras, resolvedTypeIndex);
        String name = lookupDeviceName(deviceID);
        if (name.length() > 0)
            RaftJson::appendStringField(extras, "name", name.c_str());
        String role = getDeviceRole(deviceID);
        if (!role.equalsIgnoreCase("normal"))
            RaftJson::appendStringField(extras, "role", role.c_str());
        RaftJson::appendStringField(extras, "displayName", displayNameFor(deviceID, resolvedTypeIndex).c_str());
        if (jsonParams.getBool("full", false) && deviceTypeRecords.getRecord(resolvedTypeIndex))
            extras += ",\"record\":{" + DeviceTypeFileLoader::recordToFileEntry(*deviceTypeRecords.getRecord(resolvedTypeIndex),
                        deviceTypeRecords.getPolicy(resolvedTypeIndex)) + "}";
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, extras.c_str());
    }

    // Type lookup mode (deviceTypeKey, deviceTypeIndex, or type=<key or index>)
    DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
    bool typeGiven = false;
    getRequestedType(jsonParams, deviceTypeIndex, typeGiven);
    if (!typeGiven)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failTypeMissing");
    String devInfo;
    if (deviceTypeIndex != DEVICE_TYPE_INDEX_INVALID)
        devInfo = deviceTypeRecords.getDevTypeInfoJsonByTypeIdx(deviceTypeIndex, false);
    if ((devInfo.length() == 0) || (devInfo == "{}"))
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failTypeNotFound");

    String extras = "\"devinfo\":" + devInfo + ",\"dtIdx\":" + String(deviceTypeIndex);
    appendDeviceTypeKey(extras, deviceTypeIndex);
    appendDecoderKind(extras, deviceTypeIndex);
    // The record in the type file format (so it can be copied into a type file)
    if (jsonParams.getBool("full", false) && deviceTypeRecords.getRecord(deviceTypeIndex))
        extras += ",\"record\":{" + DeviceTypeFileLoader::recordToFileEntry(*deviceTypeRecords.getRecord(deviceTypeIndex),
                    deviceTypeRecords.getPolicy(deviceTypeIndex)) + "}";
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, extras.c_str());
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Append the "decoder" field for a device type index: where its on-device poll decoder comes from -
///        "compiled", "runtime" (made from its attributes, RAFT_RUNTIME_DECODE) or "none" (only clients decode its
///        data). Nothing is appended if there is no record
/// @param jsonFields (in/out) JSON fields (without braces) to append to
/// @param deviceTypeIndex device type index
void DeviceManager::appendDecoderKind(String& jsonFields, DeviceTypeIndexType deviceTypeIndex) const
{
    if (!deviceTypeRecords.getRecord(deviceTypeIndex))
        return;
    RaftJson::appendStringField(jsonFields, "decoder",
                DeviceTypeRecords::getDecoderKindStr(deviceTypeRecords.getDecoderKind(deviceTypeIndex)));
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Append the "deviceTypeKey" field for a device type index (nothing is appended if there is no record)
/// @param jsonFields (in/out) JSON fields (without braces) to append to
/// @param deviceTypeIndex device type index
void DeviceManager::appendDeviceTypeKey(String& jsonFields, DeviceTypeIndexType deviceTypeIndex) const
{
    DeviceTypeRecord devTypeRec;
    if (deviceTypeRecords.getDeviceInfo(deviceTypeIndex, devTypeRec) && devTypeRec.deviceType && devTypeRec.deviceType[0])
    {
        RaftJson::appendStringField(jsonFields, "deviceTypeKey", devTypeRec.deviceType);
        // Where the record comes from (base, override or extended) and its policy (enabled, disabled, disabledClaim)
        RaftJson::appendStringField(jsonFields, "src",
                    DeviceTypeRecords::getRecordSourceStr(deviceTypeRecords.getRecordSource(deviceTypeIndex)));
        RaftJson::appendStringField(jsonFields, "policy",
                    DeviceTypeRecord::getPolicyStr(deviceTypeRecords.getPolicy(deviceTypeIndex)));
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get the device type a request names: deviceTypeIndex=<n>, deviceTypeKey=<key>, or type=<key or index>
/// @param jsonParams request parameters
/// @param deviceTypeIndex (out) device type index (DEVICE_TYPE_INDEX_INVALID if not found)
/// @param given (out) whether any of the parameters was given
/// @return the record, or nullptr if none was given or it wasn't found
const DeviceTypeRecord* DeviceManager::getRequestedType(const RaftJson& jsonParams, DeviceTypeIndexType& deviceTypeIndex,
            bool& given) const
{
    deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
    given = true;

    // Canonical forms
    String indexStr = jsonParams.getString("deviceTypeIndex", "");
    if (indexStr.length() > 0)
    {
        if (!isdigit(indexStr[0]))
            return nullptr;
        DeviceTypeIndexType idx = (DeviceTypeIndexType)indexStr.toInt();
        const DeviceTypeRecord* pRec = deviceTypeRecords.getRecord(idx);
        if (pRec)
            deviceTypeIndex = idx;
        return pRec;
    }
    String keyStr = jsonParams.getString("deviceTypeKey", "");
    if (keyStr.length() > 0)
        return deviceTypeRecords.getRecord(keyStr.c_str(), deviceTypeIndex);

    // Older form: a value starting with a digit is tried as an index, then as a key
    String typeKeyOrIndex = jsonParams.getString("type", "");
    if (typeKeyOrIndex.length() == 0)
    {
        given = false;
        return nullptr;
    }
    if (isdigit(typeKeyOrIndex[0]))
    {
        DeviceTypeIndexType idx = (DeviceTypeIndexType)typeKeyOrIndex.toInt();
        const DeviceTypeRecord* pRec = deviceTypeRecords.getRecord(idx);
        if (pRec)
        {
            deviceTypeIndex = idx;
            return pRec;
        }
    }
    return deviceTypeRecords.getRecord(typeKeyOrIndex.c_str(), deviceTypeIndex);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Resolve a RaftDeviceID and RaftBus pointer from API params ("deviceid" OR "bus"+"addr")
/// @param jsonParams JSON object containing the parameters for the command, expected to have ("bus" and "addr"), "deviceid" or "device" fields
/// @param deviceID (out) resolved RaftDeviceID
/// @param pBus (out) resolved RaftBus pointer (can be null if deviceID is for a static device or if bus not found)
/// @param respStr (out) response string to be filled with error message if resolution fails
/// @return RaftRetCode indicating
RaftRetCode DeviceManager::resolveDeviceIDAndBus(const RaftJson& jsonParams, RaftDeviceID& deviceID, RaftBus*& pBus, String& respStr)
{
    // Check for device name for static devices
    pBus = nullptr;
    deviceID = RaftDeviceID::INVALID;
    String deviceName = jsonParams.getString("device", "");
    if (deviceName.length() != 0)
    {
        RaftDevice* pDevice = getDevice(deviceName.c_str());
        if (pDevice)
            deviceID = pDevice->getDeviceID();
    }

    // Check for deviceid param
    if (!deviceID.isValid())
    {
        String deviceIdStr = jsonParams.getString("deviceid", deviceName.c_str());
        if (deviceIdStr.length() != 0)
        {
            deviceID = RaftDeviceID::fromString(deviceIdStr.c_str());
        }
        else
        {
            // Get address
            String addrStr = jsonParams.getString("addr", "");
            if (addrStr.length() == 0)
                respStr = "failAddrMissing";

            // Get bus name
            String busName = jsonParams.getString("bus", "");
            if (busName.length() == 0)
                respStr = "failBusMissing";

            // Get bus
            if (respStr.length() == 0)
            {
                RaftBus* pBusForName = raftBusSystem.getBusByName(busName, true);
                if (!pBusForName)
                    respStr = "failBusNotFound";
                else
                    deviceID = RaftDeviceID(pBusForName->getBusNum(), RaftDeviceID::fromString(addrStr.c_str()).getAddress());
            }
        }
   }

    // Check deviceID is valid
    if (!deviceID.isValid())
        respStr = "failInvalidDeviceID";
    else 
        pBus = raftBusSystem.getBusByNumber(deviceID.getBusNum());
    return respStr.length() == 0 ? RAFT_OK : RAFT_INVALID_OPERATION;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief REST API endpoint for sending raw command to device
/// @param reqStr request string containing the command and parameters
/// @param respStr (out) response string to be filled with the result
/// @param jsonParams JSON object containing the parameters for the command, expected to have ("bus" and "addr") or "deviceid", "hexWr", and "numToRd" fields
/// @param sourceInfo source of the request (responded to when the bus transaction completes if the transport supports deferred responses)
/// @return RaftRetCode indicating success or failure of the operation, with the response containing the result of the command if successful
///         or RAFT_RESP_DEFERRED if the response will be sent when the bus transaction completes
RaftRetCode DeviceManager::apiDevManCmdRaw(const String &reqStr, String &respStr, const RaftJson& jsonParams,
            const APISourceInfo& sourceInfo)
{
    // Maybe need to handle non-bus devices in here?


    // Resolve deviceID and bus from params
    RaftDeviceID deviceID;
    RaftBus* pBus = nullptr;
    String errorStr;
    RaftRetCode retc = resolveDeviceIDAndBus(jsonParams, deviceID, pBus, errorStr);
    if (retc != RAFT_OK)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, errorStr.c_str());

    // A valid deviceID may resolve to no bus (e.g. a directly-connected device such as
    // power management or LEDs). There is no bus to address so report rather than deref.
    if (!pBus)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failDeviceNotOnBus");

    // Data to write and number of bytes to read
    String hexWriteData = jsonParams.getString("hexWr", "");
    int numBytesToRead = jsonParams.getLong("numToRd", 0);

    // Convert hex string to byte vector
    uint32_t numBytesToWrite = hexWriteData.length() / 2;
    std::vector<uint8_t> writeVec;
    writeVec.resize(numBytesToWrite);
    uint32_t writeBytesLen = Raft::getBytesFromHexStr(hexWriteData.c_str(), writeVec.data(), numBytesToWrite);
    writeVec.resize(writeBytesLen);

    // Generation-stamped cmdId (used for read-back matching)
    static uint32_t cmdRawGeneration = 0;

    // The response is sent when the bus result arrives (rather than waiting here) if the transport supports
    // deferred responses - so the response reports the actual result of a write as well as the data read.
    // The bus result callback is made on the main task (from the bus loop()).
    RestAPIEndpointManager* pRestAPIEndpointManager = getRestAPIEndpointManager();
    if (sourceInfo.canDefer && pRestAPIEndpointManager)
    {
        // Defer the response (timeoutMs is the time allowed for the bus transaction, 0 = default)
        uint32_t timeoutMs = jsonParams.getLong("timeoutMs", 0);
        if (pRestAPIEndpointManager->deferResponse(reqStr.c_str(), sourceInfo, timeoutMs) == RAFT_RESP_DEFERRED)
        {
            // Bus request which completes the deferred response with the result (and any data read) or the bus error
            uint32_t deferToken = sourceInfo.deferToken;
            String reqStrCopy = reqStr;
            uint32_t cmdId = CMDID_CMDRAW_BASE + (++cmdRawGeneration & 0xFFFF);
            HWElemReq hwElemReq = {writeVec, numBytesToRead, (int)cmdId, "cmdraw", 0};
            BusRequestInfo busReqInfo("", deviceID.getAddress());
            busReqInfo.set(BUS_REQ_TYPE_STD, hwElemReq, 0,
                    [pRestAPIEndpointManager, deferToken, reqStrCopy](void* /*pCallbackData*/, BusRequestResult& reqResult)
                        {
                            String deferredRespStr;
                            formBusCmdResponse(reqStrCopy.c_str(), reqResult, deferredRespStr);
                            pRestAPIEndpointManager->completeDeferred(deferToken, deferredRespStr);
                        },
                    nullptr);

            // Send request to bus - if it can't be queued then respond now
            if (!pBus->addRequest(busReqInfo))
            {
                pRestAPIEndpointManager->getDeferredResponses().cancel(deferToken);
                return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusQueueFull");
            }
#ifdef DEBUG_API_CMDRAW
            LOG_I(MODULE_PREFIX, "apiDevManCmdRaw deferred token %d hexWriteData %s numToRead %d", 
                        deferToken, hexWriteData.c_str(), numBytesToRead);
#endif
            return RAFT_RESP_DEFERRED;
        }

        // Response could not be deferred (e.g. too many pending) so continue with the immediate path
    }

    // Create hardware element request
    uint32_t cmdId = CMDID_CMDRAW_BASE + (++cmdRawGeneration & 0xFFFF);
    _cmdRawInFlightCmdId = cmdId;
    _cmdRawResultReady = false;
    HWElemReq hwElemReq = {writeVec, numBytesToRead, (int)cmdId, "cmdraw", 0};

    // Create bus request info with callback to receive response
    BusRequestInfo busReqInfo("", deviceID.getAddress());
    busReqInfo.set(BUS_REQ_TYPE_STD, hwElemReq, 0,
            [](void* pCallbackData, BusRequestResult& reqResult)
                {
                    if (pCallbackData)
                        ((DeviceManager*)pCallbackData)->cmdResultReportCallback(reqResult);
                },
            this);

#ifdef DEBUG_MAKE_BUS_REQUEST_VERBOSE
    String outStr;
    Raft::getHexStrFromBytes(hwElemReq._writeData.data(),
                hwElemReq._writeData.size() > 16 ? 16 : hwElemReq._writeData.size(),
                outStr);
    LOG_I(MODULE_PREFIX, "apiHWDevice deviceId %s len %d data %s ...",
                    deviceID.toString().c_str(),
                    hwElemReq._writeData.size(),
                    outStr.c_str());
#endif

    // Send request to bus
    bool rslt = pBus->addRequest(busReqInfo);
    if (!rslt)
        LOG_W(MODULE_PREFIX, "apiHWDevice failed send raw command");

#ifdef DEBUG_API_CMDRAW
    LOG_I(MODULE_PREFIX, "apiHWDevice hexWriteData %s numToRead %d", hexWriteData.c_str(), numBytesToRead);
#endif

    // If a read was requested, wait (bounded) for the result callback and return the read bytes in
    // the response. This generally runs on the main task so the wait is capped at a short long-stop to
    // avoid stalling servicing.
    // Note that the result callback for a (non-poll) bus request is delivered from the bus's loop() function
    // which is only called from the main task. So when this is running on the main task the bus must be
    // serviced while waiting or the result could never be received (and the callback then also runs on this
    // task so there is no concurrent access to the result data)
    if (rslt && (numBytesToRead > 0))
    {
        static const uint32_t CMDRAW_READ_TIMEOUT_MAX_MS = 20;
        uint32_t timeoutMs = jsonParams.getLong("timeoutMs", CMDRAW_READ_TIMEOUT_MAX_MS);
        if (timeoutMs > CMDRAW_READ_TIMEOUT_MAX_MS)
            timeoutMs = CMDRAW_READ_TIMEOUT_MAX_MS;
        uint32_t startMs = millis();
        bool isMainTask = RaftThread_isMainTask();
        while (!_cmdRawResultReady && !Raft::isTimeout(millis(), startMs, timeoutMs))
        {
            if (isMainTask)
                pBus->loop();
            if (_cmdRawResultReady)
                break;
            RaftThread_sleep(1);
        }
        if (!_cmdRawResultReady)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "readTimeout");
        if (_cmdRawResult != RAFT_OK)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, Raft::getRetCodeStr(_cmdRawResult));
        String hexRead;
        Raft::getHexStrFromBytes(_cmdRawReadData.data(), _cmdRawReadData.size(), hexRead);
        String otherJson = "\"rdData\":\"" + hexRead + "\"";
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, otherJson.c_str());
    }

    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, rslt);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Form the response to a command sent to a bus device
/// @param reqStr request string
/// @param reqResult result of the bus request
/// @param respStr (out) response - ok (with the data read as "rdData" hex if a read was requested) or the bus error
void DeviceManager::formBusCmdResponse(const char* reqStr, BusRequestResult& reqResult, String& respStr)
{
    if (reqResult.isResultOk() && (reqResult.getReadDataLen() == 0))
    {
        Raft::setJsonBoolResult(reqStr, respStr, true);
    }
    else if (reqResult.isResultOk())
    {
        String hexRead;
        Raft::getHexStrFromBytes(reqResult.getReadData(), reqResult.getReadDataLen(), hexRead);
        String otherJson = "\"rdData\":\"" + hexRead + "\"";
        Raft::setJsonBoolResult(reqStr, respStr, true, otherJson.c_str());
    }
    else
    {
        Raft::setJsonErrorResult(reqStr, respStr, Raft::getRetCodeStr(reqResult.getResult()));
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief REST API endpoint for sending JSON command to device
/// @param reqStr request string containing the command and parameters
/// @param respStr (out) response string to be filled with the result
/// @param jsonParams JSON object containing the parameters for the command, expected to have a "body" field with the JSON command
/// @return RaftRetCode indicating success or failure of the operation, with the response containing the result of the command if successful
/// @param sourceInfo source of the request (a bus command is responded to later if the transport supports deferred responses)
/// @note the body is sent to the device as-is and is expected to contain a "device" field to route the command to the correct device. 
///       If the body is for a bus device (and the transport supports deferred responses) then the response is sent when the 
///       bus request completes and contains the result (with the data read as "rdData" if "numToRd" is specified) or the bus error.
///       Otherwise the response is a boolean indicating whether the command was successfully sent to the device, and any result 
///       from the command execution would be expected to be obtained via other means (e.g. device data updates, separate API calls, etc.)
RaftRetCode DeviceManager::apiDevManCmdJson(const String &reqStr, String &respStr, const RaftJson& jsonParams,
            const APISourceInfo& sourceInfo)
{
    String cmdJSON = jsonParams.getString("body", "");
    if (cmdJSON.length() == 0)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failMissingBody");

    // A command to a bus device is responded to when the bus result arrives if the transport supports deferred 
    // responses - so the response reports the actual result (the result callback is made on the main task)
    RestAPIEndpointManager* pRestAPIEndpointManager = getRestAPIEndpointManager();
    RaftJson cmdJsonObj(cmdJSON);
    if (sourceInfo.canDefer && pRestAPIEndpointManager)
    {
        // Only bus devices (static devices handle commands themselves)
        RaftDeviceID deviceID;
        RaftBus* pBus = nullptr;
        String errorStr;
        RaftBusDevicesIF* pDevicesIF = nullptr;
        if ((resolveDeviceIDAndBus(cmdJsonObj, deviceID, pBus, errorStr) == RAFT_OK) && pBus && !getDevice(deviceID))
            pDevicesIF = pBus->getBusDevicesIF();

        // Defer the response (timeoutMs is the time allowed for the bus transaction, 0 = default)
        uint32_t timeoutMs = jsonParams.getLong("timeoutMs", cmdJsonObj.getLong("timeoutMs", 0));
        if (pDevicesIF && (pRestAPIEndpointManager->deferResponse(reqStr.c_str(), sourceInfo, timeoutMs) == RAFT_RESP_DEFERRED))
        {
            // Send the command - the result completes the deferred response
            uint32_t deferToken = sourceInfo.deferToken;
            String reqStrCopy = reqStr;
            RaftRetCode sendRslt = pDevicesIF->sendCmdToDeviceAsync(deviceID, cmdJSON.c_str(),
                    [pRestAPIEndpointManager, deferToken, reqStrCopy](BusRequestResult& reqResult)
                        {
                            String deferredRespStr;
                            formBusCmdResponse(reqStrCopy.c_str(), reqResult, deferredRespStr);
                            pRestAPIEndpointManager->completeDeferred(deferToken, deferredRespStr);
                        });
#ifdef DEBUG_DEVMAN_API
            LOG_I(MODULE_PREFIX, "apiDevMan cmdjson deferred token %d sendRslt %s cmdJSON %s", 
                        deferToken, Raft::getRetCodeStr(sendRslt), cmdJSON.c_str());
#endif
            if (sendRslt == RAFT_OK)
                return RAFT_RESP_DEFERRED;

            // Not sent so respond now (or continue with the immediate path if the bus doesn't support this)
            pRestAPIEndpointManager->getDeferredResponses().cancel(deferToken);
            if (sendRslt == RAFT_BUSY)
                return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusQueueFull");
            if (sendRslt != RAFT_NOT_IMPLEMENTED)
                return Raft::setJsonErrorResult(reqStr.c_str(), respStr, Raft::getRetCodeStr(sendRslt));
        }
    }

    RaftRetCode retc = receiveCmdJSON(cmdJSON.c_str());

#ifdef DEBUG_DEVMAN_API
    LOG_I(MODULE_PREFIX, "apiDevMan cmdjson retc %d cmdJSON %s", retc, cmdJSON.c_str());
#endif

    if (retc == RAFT_OK)
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
    if (retc == RAFT_INVALID_OBJECT)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failDeviceNotFound");
    if (retc == RAFT_INVALID_OPERATION)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failNoDeviceSpecified");
    return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failCmdFailed");
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief REST API endpoint for starting a demo device that generates sample data
/// @param reqStr request string containing the command and parameters
/// @param respStr (out) response string to be filled with the result
/// @param jsonParams JSON object containing the parameters for the command, expected to have optional fields "type" (device type), 
///        "rate" (sample rate in ms), "duration" (duration of data generation in ms), "offlineIntvS" (interval in seconds to go offline), and 
///        "offlineDurS" (duration in seconds to stay offline)
/// @return RaftRetCode indicating success or failure of the operation, with the response containing the parameters of the started demo device if successful
#ifdef DEVICE_MANAGER_ENABLE_DEMO_DEVICE
RaftRetCode DeviceManager::apiDevManDemo(const String &reqStr, String &respStr, const RaftJson& jsonParams)
{
    String deviceType = jsonParams.getString("deviceTypeKey", jsonParams.getString("type", "").c_str());
    if (deviceType.length() == 0)
        deviceType = "ACCDEMO";

    uint32_t sampleRateMs = jsonParams.getLong("rate", 100);
    uint32_t durationMs = jsonParams.getLong("duration", 0);
    uint32_t offlineIntvS = jsonParams.getLong("offlineIntvS", 0);
    uint32_t offlineDurS = jsonParams.getLong("offlineDurS", 10);

    if (sampleRateMs < 10)
        sampleRateMs = 10;
    if (sampleRateMs > 60000)
        sampleRateMs = 60000;
    if (offlineDurS < 1)
        offlineDurS = 1;

    // One demo device per type - it is named after its type
    if (getDevice(deviceType, true))
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failDemoDeviceExists");

    String configStr = "{";
    RaftJson::appendStringField(configStr, "name", deviceType.c_str());
    RaftJson::appendStringField(configStr, "type", deviceType.c_str());
    configStr += ",\"sampleRateMs\":" + String(sampleRateMs) +
                ",\"durationMs\":" + String(durationMs) +
                ",\"offlineIntvS\":" + String(offlineIntvS) +
                ",\"offlineDurS\":" + String(offlineDurS) + "}";
    RaftJson jsonConfig = configStr.c_str();
    RaftDevice* pDevice = addStaticDevice(DemoDevice::create, "DemoDevice", jsonConfig);
    if (!pDevice)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failDemoDeviceCreate");
    setupStaticDevice(pDevice);
    pDevice->postSetup();

    String resultStr = "\"demoStarted\":true";
    RaftJson::appendStringField(resultStr, "type", deviceType.c_str());
    resultStr += ",\"rate\":" + String(sampleRateMs) +
                ",\"duration\":" + String(durationMs) +
                ",\"offlineIntvS\":" + String(offlineIntvS) +
                ",\"offlineDurS\":" + String(offlineDurS);
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, resultStr.c_str());
}
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief REST API endpoint for setting device configuration such as polling interval
/// @param reqStr request string containing the command and parameters
/// @param respStr (out) response string to be filled with the result
/// @param jsonParams JSON object containing the parameters for the command, expected to have fields "bus" (bus name), "device" (device name), and 
///        "intervalUs" (polling interval in microseconds), "numSamples" (number of poll result samples to store),
///        "sampleRateHz" (device internal sample rate in Hz - uses _conf actions from DeviceTypeRecords)
/// @return RaftRetCode indicating success or failure of the operation
RaftRetCode DeviceManager::apiDevManDevConfig(const String &reqStr, String &respStr, const RaftJson& jsonParams)
{
    // Handle non-bus devices in here?

    // Resolve deviceID and bus from params
    RaftDeviceID deviceID;
    RaftBus* pBus = nullptr;
    String errorStr;
    RaftRetCode retc = resolveDeviceIDAndBus(jsonParams, deviceID, pBus, errorStr);
    if (retc != RAFT_OK)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, errorStr.c_str());

    // A valid deviceID may resolve to no bus (e.g. a directly-connected device such as
    // power management or LEDs). Bus poll settings do not apply so report rather than deref.
    if (!pBus)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failDeviceNotOnBus");

    BusElemAddrType addr = deviceID.getAddress();

    // Check if poll interval is provided
    String intervalUsStr = jsonParams.getString("intervalUs", "");
    if (intervalUsStr.length() > 0)
    {
        uint64_t intervalUs = strtoull(intervalUsStr.c_str(), nullptr, 10);
        if (intervalUs == 0)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failInvalidInterval");

#ifdef DEBUG_DEVICE_CONFIG_API
        LOG_I(MODULE_PREFIX, "pollrate set req deviceID %s intervalUs %llu (rateHz %.3f)",
                deviceID.toString().c_str(),
                (unsigned long long)intervalUs,
                intervalUs == 0 ? 0 : 1000000.0 / intervalUs);
#endif

        // Set poll interval for the device on the bus
        if (!pBus->setDevicePollIntervalUs(addr, intervalUs))
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failUnsupportedBus");
    }

    // Check if numSamples is provided
    String numSamplesStr = jsonParams.getString("numSamples", "");
    if (numSamplesStr.length() > 0)
    {
        uint32_t numSamples = strtoul(numSamplesStr.c_str(), nullptr, 10);
        if (numSamples == 0)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failInvalidNumSamples");

#ifdef DEBUG_DEVICE_CONFIG_API
        LOG_I(MODULE_PREFIX, "numSamples set req deviceID %s numSamples %u",
                deviceID.toString().c_str(),
                (unsigned)numSamples);
#endif

        // Set num samples for the device on the bus
        if (!pBus->setDeviceNumSamples(addr, numSamples))
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failUnsupportedBus");
    }

    // Check if busHz is provided (per-device poll bus frequency override)
    String busHzStr = jsonParams.getString("busHz", "");
    if (busHzStr.length() > 0)
    {
        uint32_t busHz = strtoul(busHzStr.c_str(), nullptr, 10);

#ifdef DEBUG_DEVICE_CONFIG_API
        LOG_I(MODULE_PREFIX, "busHz set req deviceID %s busHz %u",
                deviceID.toString().c_str(),
                (unsigned)busHz);
#endif

        // Set poll bus frequency for the device on the bus (0 = revert to bus default)
        if (!pBus->setDevicePollBusHz(addr, busHz))
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failUnsupportedBus");
    }

    // Check if busHzSlots is provided (comma-separated list of slot numbers, e.g. "4,5,6")
    String busHzSlotsStr = jsonParams.getString("busHzSlots", "");
    if (busHzSlotsStr.length() > 0)
    {
        uint64_t slotMask = 0;
        int startPos = 0;
        while (startPos <= (int)busHzSlotsStr.length())
        {
            int commaPos = busHzSlotsStr.indexOf(',', startPos);
            if (commaPos < 0)
                commaPos = busHzSlotsStr.length();
            String slotStr = busHzSlotsStr.substring(startPos, commaPos);
            slotStr.trim();
            if (slotStr.length() > 0)
            {
                int slotNum = slotStr.toInt();
                if (slotNum >= 0 && slotNum < 64)
                    slotMask |= (1ULL << slotNum);
            }
            startPos = commaPos + 1;
        }

#ifdef DEBUG_DEVICE_CONFIG_API
        LOG_I(MODULE_PREFIX, "busHzSlots set req deviceID %s slotMask 0x%llx",
                deviceID.toString().c_str(),
                (unsigned long long)slotMask);
#endif

        if (!pBus->setDevicePollBusHzSlotMask(addr, slotMask))
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failUnsupportedBus");
    }

    // Check if sampleRateHz is provided
    String sampleRateHzStr = jsonParams.getString("sampleRateHz", "");
    bool sampleRateConfigured = false;
    if (sampleRateHzStr.length() > 0)
    {
        double sampleRateHz = strtod(sampleRateHzStr.c_str(), nullptr);
        if (sampleRateHz <= 0)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failInvalidSampleRate");

        // Get device type index for the address
        DeviceTypeIndexType deviceTypeIndex = pBus->getDeviceTypeIndex(addr);
        if (deviceTypeIndex == DEVICE_TYPE_INDEX_INVALID)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failDeviceTypeNotFound");

        // Get device type record
        DeviceTypeRecord devTypeRec;
        if (!deviceTypeRecords.getDeviceInfo(deviceTypeIndex, devTypeRec) || !devTypeRec.devInfoJson)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failNoDeviceInfo");

        // Parse devInfoJson for _conf actions
        RaftJson devInfoJsonDoc(devTypeRec.devInfoJson);
        int actionsArrayLen = 0;
        if (devInfoJsonDoc.getType("actions", actionsArrayLen) != RaftJsonIF::RAFT_JSON_ARRAY || actionsArrayLen == 0)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failSampleRateNotSupported");

        // Iterate actions looking for _conf.* entries
        bool anyConfActionFound = false;
        bool anyRateMatched = false;
        bool anyWriteWanted = false;
        bool anyWriteSent = false;
        for (int i = 0; i < actionsArrayLen; i++)
        {
            String idxStr = String(i);
            String actionName = devInfoJsonDoc.getString(("actions[" + idxStr + "]/n").c_str(), "");
            if (!actionName.startsWith("_conf."))
                continue;
            anyConfActionFound = true;

            // Look up sampleRateHz in this action's map
            // Map entries can be objects {"w":"hex", "i":intervalUs, "s":numSamples} or plain strings "hex". An object
            // without "w" only sets the polling (for a device whose rate is how often it's polled, e.g. the SCD30)
            String mapPath = "actions[" + idxStr + "]/map/" + sampleRateHzStr;
            int mapEntryLen = 0;
            bool isObjectEntry = devInfoJsonDoc.getType(mapPath.c_str(), mapEntryLen) == RaftJsonIF::RAFT_JSON_OBJECT;

            // Object format: map/<rate>/w; plain string format: map/<rate>
            String mappedHexValue = devInfoJsonDoc.getString((isObjectEntry ? mapPath + "/w" : mapPath).c_str(), "");
            if (!isObjectEntry && (mappedHexValue.length() == 0))
                continue;
            anyRateMatched = true;
            if (mappedHexValue.length() > 0)
                anyWriteWanted = true;

            // Apply map-based polling config defaults (only if not explicitly provided in API call)
            long mapIntervalUs = devInfoJsonDoc.getLong((mapPath + "/i").c_str(), 0);
            long mapNumSamples = devInfoJsonDoc.getLong((mapPath + "/s").c_str(), 0);
            if (mapIntervalUs > 0 && intervalUsStr.length() == 0)
            {
                pBus->setDevicePollIntervalUs(addr, mapIntervalUs);
#ifdef DEBUG_DEVICE_CONFIG_API
                LOG_I(MODULE_PREFIX, "sampleRate map intervalUs %ld for deviceID %s",
                        mapIntervalUs, deviceID.toString().c_str());
#endif
            }
            if (mapNumSamples > 0 && numSamplesStr.length() == 0)
            {
                pBus->setDeviceNumSamples(addr, mapNumSamples);
#ifdef DEBUG_DEVICE_CONFIG_API
                LOG_I(MODULE_PREFIX, "sampleRate map numSamples %ld for deviceID %s",
                        mapNumSamples, deviceID.toString().c_str());
#endif
            }
            long mapBusHz = devInfoJsonDoc.getLong((mapPath + "/h").c_str(), 0);
            if (mapBusHz > 0 && busHzStr.length() == 0)
            {
                pBus->setDevicePollBusHz(addr, mapBusHz);
#ifdef DEBUG_DEVICE_CONFIG_API
                LOG_I(MODULE_PREFIX, "sampleRate map busHz %ld for deviceID %s",
                        mapBusHz, deviceID.toString().c_str());
#endif
            }

            // Apply map-based bus Hz slot mask (only if not explicitly provided in API call)
            // The map entry can contain "hSlots" as a JSON array, e.g. "hSlots":[4,5,6]
            if (busHzSlotsStr.length() == 0)
            {
                String hSlotsPath = mapPath + "/hSlots";
                int mapSlotsArrayLen = 0;
                if (devInfoJsonDoc.getType(hSlotsPath.c_str(), mapSlotsArrayLen) == RaftJsonIF::RAFT_JSON_ARRAY && mapSlotsArrayLen > 0)
                {
                    uint64_t mapSlotMask = 0;
                    for (int slotIdx = 0; slotIdx < mapSlotsArrayLen; slotIdx++)
                    {
                        String slotPath = hSlotsPath + "[" + String(slotIdx) + "]";
                        int slotNum = devInfoJsonDoc.getLong(slotPath.c_str(), -1);
                        if (slotNum >= 0 && slotNum < 64)
                            mapSlotMask |= (1ULL << slotNum);
                    }
                    pBus->setDevicePollBusHzSlotMask(addr, mapSlotMask);
#ifdef DEBUG_DEVICE_CONFIG_API
                    LOG_I(MODULE_PREFIX, "sampleRate map busHzSlotMask 0x%llx for deviceID %s",
                            (unsigned long long)mapSlotMask, deviceID.toString().c_str());
#endif
                }
            }

            // Get write prefix (may be empty for consolidated multi-write actions)
            String writePrefix = devInfoJsonDoc.getString(("actions[" + idxStr + "]/w").c_str(), "");

            // Handle &-separated multi-write map values (e.g. "1048&114C&0a26")
            // Each segment is a self-contained register+data hex string
            int segStart = 0;
            while (segStart <= (int)mappedHexValue.length())
            {
                int segEnd = mappedHexValue.indexOf('&', segStart);
                if (segEnd < 0)
                    segEnd = mappedHexValue.length();
                String segment = mappedHexValue.substring(segStart, segEnd);
                segStart = segEnd + 1;

                if (segment.length() == 0)
                    continue;

                String hexWriteData = writePrefix + segment;
                uint32_t numBytes = hexWriteData.length() / 2;
                std::vector<uint8_t> writeVec(numBytes);
                uint32_t writeBytesLen = Raft::getBytesFromHexStr(hexWriteData.c_str(), writeVec.data(), numBytes);
                writeVec.resize(writeBytesLen);

                // Create and send bus request
                static const uint32_t CMDID_DEVCONFIG = 101;
                HWElemReq hwElemReq = {writeVec, 0, CMDID_DEVCONFIG, "devconfig", 0};
                BusRequestInfo busReqInfo("", addr);
                busReqInfo.set(BUS_REQ_TYPE_STD, hwElemReq, 0, nullptr, nullptr);
                if (pBus->addRequest(busReqInfo))
                    anyWriteSent = true;

#ifdef DEBUG_DEVICE_CONFIG_API
                LOG_I(MODULE_PREFIX, "sampleRate action %s deviceID %s hexWr %s",
                        actionName.c_str(), deviceID.toString().c_str(), hexWriteData.c_str());
#endif
            }
        }

        if (!anyConfActionFound)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failSampleRateNotSupported");
        if (!anyRateMatched || (anyWriteWanted && !anyWriteSent))
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failInvalidSampleRate");
        sampleRateConfigured = true;
    }

    // Read back
    uint64_t pollIntervalUs = pBus->getDevicePollIntervalUs(addr);
    uint32_t numSamplesResult = pBus->getDeviceNumSamples(addr);
    uint32_t busHzResult = pBus->getDevicePollBusHz(addr);
    uint64_t busHzSlotMaskResult = pBus->getDevicePollBusHzSlotMask(addr);
    if (pollIntervalUs == 0 && numSamplesResult == 0 && busHzResult == 0 && !sampleRateConfigured)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failUnsupportedBus");

#ifdef DEBUG_DEVICE_CONFIG_API
        LOG_I(MODULE_PREFIX, "devconfig applied deviceID %s intervalUs %llu (rateHz %.3f) numSamples %u busHz %u slotMask 0x%llx sampleRateHz %s",
                deviceID.toString().c_str(),
                (unsigned long long)pollIntervalUs,
                pollIntervalUs == 0 ? 0 : 1000000.0 / pollIntervalUs,
                (unsigned)numSamplesResult,
                (unsigned)busHzResult,
                (unsigned long long)busHzSlotMaskResult,
                sampleRateHzStr.c_str());
#endif

    String extra = "\"deviceID\":\"" + deviceID.toString() + "\",\"pollIntervalUs\":" + String(pollIntervalUs) +
                   ",\"numSamples\":" + String(numSamplesResult) +
                   ",\"busHz\":" + String(busHzResult);
    // Add busHzSlots as a JSON array of slot numbers
    if (busHzSlotMaskResult != 0)
    {
        extra += ",\"busHzSlots\":[";
        bool first = true;
        for (int i = 0; i < 64; i++)
        {
            if (busHzSlotMaskResult & (1ULL << i))
            {
                if (!first) extra += ",";
                extra += String(i);
                first = false;
            }
        }
        extra += "]";
    }
    if (sampleRateConfigured)
        extra += ",\"sampleRateHz\":" + sampleRateHzStr;

    // Remember the settings, to apply again when the device is identified again as the same type (after it is
    // re-identified, re-initialised or absent for a while) - not when they are being applied from what is stored
    if (!_applyingStoredSettings)
        rememberDevConfig(deviceID, pBus->getDeviceTypeIndex(addr), jsonParams);

    // Save the settings (for this device's type) if asked
    if (jsonParams.getBool("save", false))
    {
        auto it = _devConfigSettings.find(DeviceNames::packDeviceIDKey(deviceID));
        const DeviceTypeRecord* pRec = deviceTypeRecords.getRecord(pBus->getDeviceTypeIndex(addr));
        if ((it != _devConfigSettings.end()) && pRec && pRec->deviceType)
        {
            DeviceSettingsStore::Settings& saved = _settingsStore.getOrAdd(deviceID.toString());
            saved.typeKey = pRec->deviceType;
            saved.devConfig = it->second.values;
            extra += String(",\"saved\":") + (saveDeviceSettings() ? "true" : "false");
        }
    }
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, extra.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Remember devconfig settings for a device (merged with those already remembered for the same type)
/// @param deviceID device
/// @param deviceTypeIndex the device's type now
/// @param jsonParams the settings (intervalUs, numSamples, busHz, busHzSlots, sampleRateHz)
void DeviceManager::rememberDevConfig(RaftDeviceID deviceID, DeviceTypeIndexType deviceTypeIndex, const RaftJson& jsonParams)
{
    if (deviceTypeIndex == DEVICE_TYPE_INDEX_INVALID)
        return;
    DevConfigSettings& settings = _devConfigSettings[DeviceNames::packDeviceIDKey(deviceID)];
    if (settings.deviceTypeIndex != deviceTypeIndex)
        settings = DevConfigSettings();
    settings.deviceTypeIndex = deviceTypeIndex;
    static const char* SETTING_NAMES[] = {"intervalUs", "numSamples", "busHz", "busHzSlots", "sampleRateHz"};
    for (const char* pName : SETTING_NAMES)
    {
        String value = jsonParams.getString(pName, "");
        if (value.length() == 0)
            continue;
        bool replaced = false;
        for (auto& setting : settings.values)
        {
            if (setting.name == pName)
            {
                setting.value = value;
                replaced = true;
            }
        }
        if (!replaced)
            settings.values.push_back(RaftJson::NameValuePair(pName, value));
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Apply remembered devconfig settings to a device that has been identified (again)
/// @param deviceID device
/// @param deviceTypeIndex the type it has been identified as (settings for another type are forgotten)
void DeviceManager::reapplyDevConfig(RaftDeviceID deviceID, DeviceTypeIndexType deviceTypeIndex)
{
    // The device's own settings, if made for this type (settings for another type are forgotten)
    std::vector<RaftJson::NameValuePair> deviceValues;
    auto it = _devConfigSettings.find(DeviceNames::packDeviceIDKey(deviceID));
    if (it != _devConfigSettings.end())
    {
        if (it->second.deviceTypeIndex == deviceTypeIndex)
            deviceValues = it->second.values;
        else
            _devConfigSettings.erase(it);
    }

    // Under them, the settings for the type (DevMan "typeSettings")
    const DeviceTypeRecord* pRec = deviceTypeRecords.getRecord(deviceTypeIndex);
    const DeviceTypeSettings::Entry* pTypeEntry = pRec ? _typeSettings.find(pRec->deviceType) : nullptr;
    std::vector<RaftJson::NameValuePair> nameValues = DeviceTypeSettings::layer(
                pTypeEntry ? pTypeEntry->devConfig : std::vector<RaftJson::NameValuePair>(), deviceValues);
    if (nameValues.empty())
        return;
    nameValues.push_back(RaftJson::NameValuePair("deviceid", deviceID.toString()));
    RaftJson jsonParams = RaftJson::getJSONFromNVPairs(nameValues, true);
    String respStr;
    _applyingStoredSettings = true;
    apiDevManDevConfig("devman/devconfig", respStr, jsonParams);
    _applyingStoredSettings = false;
    LOG_I(MODULE_PREFIX, "reapplyDevConfig %s (%s%s): %s", deviceID.toString().c_str(),
                pTypeEntry && !pTypeEntry->devConfig.empty() ? "type settings" : "",
                deviceValues.empty() ? "" : (pTypeEntry && !pTypeEntry->devConfig.empty() ? ", device settings" : "device settings"),
                respStr.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief REST API endpoint for getting bus name from bus number
/// @param reqStr request string containing the command and parameters
/// @param respStr (out) response string to be filled with the result
/// @param jsonParams JSON object containing the parameters for the command, expected to have a "busnum" field with the bus number
/// @return RaftRetCode indicating success or failure of the operation, with the response containing the bus name if successful
RaftRetCode DeviceManager::apiDevManBusName(const String &reqStr, String &respStr, const RaftJson& jsonParams)
{
    int busNum = jsonParams.getLong("busnum", -1);
    if (busNum < RaftDeviceID::BUS_NUM_FIRST_BUS)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failInvalidBusNum");

    RaftBus* pBus = raftBusSystem.getBusByNumber(busNum);
    if (!pBus)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusNotFound");
    String busName = pBus->getBusName();

#ifdef DEBUG_DEVMAN_API
    LOG_I(MODULE_PREFIX, "apiDevMan busname busNum %d busName %s", busNum, busName.c_str());
#endif

    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true,
                                   ("\"busName\":\"" + busName + "\"").c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Register for device data notifications (note that callbacks may occur on different threads)
/// @param deviceID Device identifier
/// @param dataChangeCB Callback for data change
/// @param minTimeBetweenReportsMs Minimum time between reports (ms)
/// @param pCallbackInfo Optional callback info (passed to the callback)
/// @param unregister true to unregister the callback instead of registering
void DeviceManager::registerForDeviceData(RaftDeviceID deviceID, RaftDeviceDataChangeCB dataChangeCB, 
        uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo, bool unregister)
{
    if (unregister)
    {
        // Returns only once no call to this subscriber is in progress on another task
        _deviceDataSubscribers.removeForDevice(deviceID, pCallbackInfo);
#ifdef DEBUG_REGISTER_FOR_DEVICE_DATA
        LOG_I(MODULE_PREFIX, "registerForDeviceData unregister deviceID %s callbackInfo %p", 
                deviceID.toString().c_str(), pCallbackInfo);
#endif
        return;
    }
#ifdef DEBUG_REGISTER_FOR_DEVICE_DATA
    LOG_I(MODULE_PREFIX, "registerForDeviceData register deviceID %s callbackInfo %p minTimeBetweenReportsMs %u", 
            deviceID.toString().c_str(), pCallbackInfo, minTimeBetweenReportsMs);
#endif
    _deviceDataSubscribers.addForDevice(deviceID, dataChangeCB, minTimeBetweenReportsMs, pCallbackInfo);
    installStaticDataDispatchers();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Register for device data notifications (note that callbacks may occur on different threads)
/// @param deviceTypeIndex Device type index
/// @param dataChangeCB Callback for data change
/// @param minTimeBetweenReportsMs Minimum time between reports (ms)
/// @param pCallbackInfo Optional callback info (passed to the callback)
/// @param unregister true to unregister the callback instead of registering
void DeviceManager::registerForDeviceData(DeviceTypeIndexType deviceTypeIndex, RaftDeviceDataChangeCB dataChangeCB,
        uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo, bool unregister)
{
    if (unregister)
    {
        // Returns only once no call to this subscriber is in progress on another task
        _deviceDataSubscribers.removeForType(deviceTypeIndex, pCallbackInfo);
#ifdef DEBUG_REGISTER_FOR_DEVICE_DATA
        LOG_I(MODULE_PREFIX, "registerForDeviceData unregister deviceTypeIndex %u callbackInfo %p", 
                deviceTypeIndex, pCallbackInfo);
#endif
        return;
    }
#ifdef DEBUG_REGISTER_FOR_DEVICE_DATA
    LOG_I(MODULE_PREFIX, "registerForDeviceData register deviceTypeIndex %u callbackInfo %p minTimeBetweenReportsMs %u", 
            deviceTypeIndex, pCallbackInfo, minTimeBetweenReportsMs);
#endif
    _deviceDataSubscribers.addForType(deviceTypeIndex, dataChangeCB, minTimeBetweenReportsMs, pCallbackInfo);
    installStaticDataDispatchers();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Register for device data notifications (note that callbacks may occur on different threads)
/// @param deviceTypeKeyOrAddr Device type key, or a device address of the form "<bus>_<addr>[@slot]"
/// @param dataChangeCB Callback for data change
/// @param minTimeBetweenReportsMs Minimum time between reports (ms)
/// @param pCallbackInfo Optional callback info (passed to the callback)
/// @param unregister true to unregister the callback instead of registering
void DeviceManager::registerForDeviceData(const char* deviceTypeKeyOrAddr, RaftDeviceDataChangeCB dataChangeCB,
        uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo, bool unregister)
{
    // The identifier may be a device address of the form "<bus>_<addr>[@slot]"
    // (e.g. "I2CA_0x6a@0") rather than a device type key. If it resolves to a
    // real bus + address, register by device ID instead. That path is deferred
    // until the device at that address comes online (see busElemStatusCB), so it
    // works even when called before the device has been identified.
    {
        String idStr(deviceTypeKeyOrAddr);
        int usPos = idStr.indexOf('_');
        if (usPos > 0)
        {
            String busPart = idStr.substring(0, usPos);
            String addrPart = idStr.substring(usPos + 1);
            // An "@slot" suffix gives the slot (the address in a device ID carries the slot above the 8-bit address)
            uint32_t slotNum = 0;
            int atPos = addrPart.indexOf('@');
            if (atPos >= 0)
            {
                slotNum = strtoul(addrPart.substring(atPos + 1).c_str(), nullptr, 10);
                addrPart = addrPart.substring(0, atPos);
            }
            // Strip an optional 0x prefix from the address
            if (addrPart.startsWith("0x") || addrPart.startsWith("0X"))
                addrPart = addrPart.substring(2);
            // Only treat as an address if the bus resolves (by name or number)
            // and the address starts with a hex digit
            RaftBus* pNamedBus = raftBusSystem.getBusByName(busPart, true);
            char c0 = addrPart.length() > 0 ? addrPart[0] : '\0';
            bool addrIsHex = (c0 >= '0' && c0 <= '9') || (c0 >= 'a' && c0 <= 'f') || (c0 >= 'A' && c0 <= 'F');
            if (pNamedBus && addrIsHex)
            {
                BusElemAddrType address = (BusElemAddrType)strtoul(addrPart.c_str(), nullptr, 16);
                if (slotNum > 0)
                    address = (address & 0xff) | ((slotNum & 0x3f) << 8);
                RaftDeviceID deviceID(pNamedBus->getBusNum(), address);
                if (deviceID.isValid())
                {
                    registerForDeviceData(deviceID, dataChangeCB, minTimeBetweenReportsMs, pCallbackInfo, unregister);
                    return;
                }
            }
        }
    }

    // A device name
    {
        RaftDeviceID namedDeviceID;
        if (_deviceNames.resolveName(deviceTypeKeyOrAddr, namedDeviceID))
        {
            registerForDeviceData(namedDeviceID, dataChangeCB, minTimeBetweenReportsMs, pCallbackInfo, unregister);
            return;
        }
    }

    // Get device type index for the device type key
    DeviceTypeRecord devTypeRec;
    DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
    deviceTypeRecords.getDeviceInfo(String(deviceTypeKeyOrAddr), devTypeRec, deviceTypeIndex);
    if (deviceTypeIndex == DEVICE_TYPE_INDEX_INVALID)
    {
        LOG_W(MODULE_PREFIX, "registerForDeviceData failed: invalid device type key %s", deviceTypeKeyOrAddr);
        return;
    }

#ifdef DEBUG_REGISTER_FOR_DEVICE_DATA
    LOG_I(MODULE_PREFIX, "registerForDeviceData deviceTypeKey %s deviceTypeIndex %u", deviceTypeKeyOrAddr, deviceTypeIndex);
#endif

    // Add to requests for device data changes
    registerForDeviceData(deviceTypeIndex, dataChangeCB, minTimeBetweenReportsMs, pCallbackInfo, unregister);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Register for device status changes
/// @param statusChangeCB Callback for status change
void DeviceManager::registerForDeviceStatusChange(RaftDeviceStatusChangeCB statusChangeCB)
{
    // Add to requests for device status changes
    if (RaftMutex_lock(_accessMutex, ACCESS_MUTEX_MAX_WAIT_MS))
    {
        _requestedDeviceStatusChangeCBList.push_back(statusChangeCB);
        RaftMutex_unlock(_accessMutex);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get a frozen version of device list only including online devices
/// @param pDeviceList (out) list of devices (must be maxNumDevices long)
/// @param maxNumDevices maximum number of devices to return
/// @param onlyOnline true to only return online devices
/// @param deviceOnlineArray pointer to array of device online flags (may be nullptr) - must be maxNumDevices long
/// @return number of devices
uint32_t DeviceManager::getStaticDeviceListFrozen(RaftDevice** pDevices, uint32_t maxDevices, bool onlyOnline, 
        bool* pDeviceOnlineArray) const
{
    if (!RaftMutex_lock(_accessMutex, ACCESS_MUTEX_MAX_WAIT_MS))
        return 0;
    
    uint32_t numDevices = 0;
    for (auto& devRec : _staticDeviceList)
    {
        if (numDevices >= maxDevices)
            break;
        if (pDeviceOnlineArray)
            pDeviceOnlineArray[numDevices] = devRec.isOnline;
        if (devRec.pDevice && (!onlyOnline || devRec.isOnline))
        {
            pDevices[numDevices++] = devRec.pDevice;
        }
    }
    RaftMutex_unlock(_accessMutex);
    return numDevices;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Find device in device list
/// @param deviceID ID of the device
/// @return pointer to device if found
RaftDevice* DeviceManager::getDevice(RaftDeviceID deviceID) const
{
    if (!RaftMutex_lock(_accessMutex, ACCESS_MUTEX_MAX_WAIT_MS))
        return nullptr;
    for (auto& devRec : _staticDeviceList)
    {
        if (devRec.pDevice && (devRec.pDevice->idMatches(deviceID)))
        {
            RaftMutex_unlock(_accessMutex);
            return devRec.pDevice;
        }
    }
    RaftMutex_unlock(_accessMutex);
    return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device by lookup the device ID as string or configured device name
/// @param deviceStr Device string (device ID as string or configured device name)
/// @param tryConfigName Whether to try config name lookup if ID lookup fails
/// @return pointer to device if found, nullptr otherwise
RaftDevice* DeviceManager::getDevice(const String& deviceStr, bool tryConfigName) const
{
    // Convert the device string to a RaftDeviceID and find the device
    RaftDeviceID deviceID = RaftDeviceID::fromString(deviceStr);
    if (deviceID.isValid())
    {
        RaftDevice* pDevice = getDevice(deviceID);
        if (pDevice)
            return pDevice;
    }
    
    // Try to match the device string to a configured device name
    if (!RaftMutex_lock(_accessMutex, ACCESS_MUTEX_MAX_WAIT_MS))
        return nullptr;
    for (auto& devRec : _staticDeviceList)
    {
        if (devRec.pDevice && devRec.pDevice->getConfiguredDeviceName().equalsIgnoreCase(deviceStr))
        {
            RaftMutex_unlock(_accessMutex);
            return devRec.pDevice;
        }
    }
    RaftMutex_unlock(_accessMutex);

    // Try the DeviceNames map (for bus devices with friendly names)
    RaftDeviceID namedDeviceID;
    if (_deviceNames.resolveName(deviceStr, namedDeviceID))
    {
        // Found the name — try to find the device by the mapped ID
        RaftDevice* pDevice = getDevice(namedDeviceID);
        if (pDevice)
            return pDevice;
    }

    // Not found
    return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief call device status change callbacks
/// @param pDevice pointer to the device
/// @param addrStatus bus element status
void DeviceManager::callDeviceStatusChangeCBs(RaftDevice* pDevice, const BusAddrStatus& addrAndStatus)
{
    // Obtain a lock & make a copy of the device status change callbacks
    if (!RaftMutex_lock(_accessMutex, ACCESS_MUTEX_MAX_WAIT_MS))
        return;
    std::vector<RaftDeviceStatusChangeCB> statusChangeCallbacks(_requestedDeviceStatusChangeCBList.begin(), _requestedDeviceStatusChangeCBList.end());
    RaftMutex_unlock(_accessMutex);

    // Call the device status change callbacks
    for (RaftDeviceStatusChangeCB statusChangeCB : statusChangeCallbacks)
    {
        statusChangeCB(*pDevice, addrAndStatus);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Post setup helper - register for device data change callbacks
/// @return number of devices registered for data change callbacks
uint32_t DeviceManager::postSetupRegisterDataCBs()
{
    return installStaticDataDispatchers();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Install the fan-out as a bus device's single data callback.  Idempotent: the bus replaces its
/// callback with an identical one (same callback info), so it is safe on every status change.
/// @param busDevicesIF bus devices interface
/// @param busNum bus number
/// @param address device address on the bus
void DeviceManager::installBusDataDispatcher(RaftBusDevicesIF& busDevicesIF, BusNumType busNum, BusElemAddrType address)
{
    const RaftDeviceID deviceID(busNum, address);
    busDevicesIF.registerForDeviceData(address,
        [this, deviceID](uint16_t deviceTypeIndex, std::vector<uint8_t> data, const void*) {
#ifdef RAFT_RUNTIME_DECODE_VERIFY
            // Decode with the compiled and a runtime decoder and compare (devman/decodeverify)
            deviceTypeDecodeVerifier.checkDecode(deviceID, deviceTypeIndex, deviceTypeRecords.getRecord(deviceTypeIndex),
                        deviceTypeRecords.getDecoderKind(deviceTypeIndex) == DeviceTypeRecords::DecoderKind::Compiled, data);
#endif
            _deviceDataSubscribers.dispatch(deviceID, deviceTypeIndex, std::move(data), millis());
        },
        /*minTimeBetweenReportsMs=*/0, /*pCallbackInfo=*/&_deviceDataSubscribers);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Install the fan-out on static devices that have a subscriber and do not have it yet.  Only
/// devices with a subscriber: a static device's registerForDeviceData may reach a bus that does not exist.
/// @return number of static devices newly installed
uint32_t DeviceManager::installStaticDataDispatchers()
{
    std::vector<RaftDevice*> toInstall;
    if (!RaftMutex_lock(_accessMutex, RAFT_MUTEX_WAIT_FOREVER))
        return 0;
    for (auto& devRec : _staticDeviceList)
    {
        if (!devRec.pDevice)
            continue;
        const RaftDeviceID deviceID = devRec.pDevice->getDeviceID();
        if (std::find(_staticDispatchInstalled.begin(), _staticDispatchInstalled.end(), deviceID) != _staticDispatchInstalled.end())
            continue;
        if (!_deviceDataSubscribers.hasSubscriber(deviceID, devRec.pDevice->getDeviceTypeIndex()))
            continue;
        _staticDispatchInstalled.push_back(deviceID);
        toInstall.push_back(devRec.pDevice);
    }
    RaftMutex_unlock(_accessMutex);

    // Outside the lock: a device may call back into DeviceManager
    for (RaftDevice* pDevice : toInstall)
    {
        const RaftDeviceID deviceID = pDevice->getDeviceID();
        pDevice->registerForDeviceData(
            [this, deviceID](uint16_t deviceTypeIndex, std::vector<uint8_t> data, const void*) {
                _deviceDataSubscribers.dispatch(deviceID, deviceTypeIndex, std::move(data), millis());
            },
            /*minTimeBetweenReportsMs=*/0, /*pCallbackInfo=*/&_deviceDataSubscribers);
    }
    return toInstall.size();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Device event callback
/// @param device Device
/// @param eventName Name of the event
/// @param eventData Data associated with the event
void DeviceManager::deviceEventCB(RaftDevice& device, const char* eventName, const char* eventData)
{
    // Get sys manager
    SysManagerIF* pSysMan = getSysManager();
    if (!pSysMan)
        return;
    String cmdStr = "{\"msgType\":\"sysevent\",\"msgName\":\"" + String(eventName) + "\"";
    if (eventData)
        cmdStr += eventData;
    cmdStr += "}";
    pSysMan->sendCmdJSON(
        "SysMan",
        cmdStr.c_str()
    );
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Assign a friendly name to a bus device
/// @param deviceID Device identifier
/// @param name Friendly name
void DeviceManager::setDeviceName(RaftDeviceID deviceID, const String& name)
{
    _deviceNames.setName(deviceID, name);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get friendly name for a device ID
/// @param deviceID Device identifier
/// @return friendly name if mapped, otherwise deviceID.toString()
String DeviceManager::getDeviceNameForID(RaftDeviceID deviceID) const
{
    return _deviceNames.getNameForID(deviceID);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Look up a friendly name for a device ID without the toString fallback
/// @param deviceID Device identifier
/// @return friendly name if mapped, otherwise empty string
String DeviceManager::lookupDeviceName(RaftDeviceID deviceID) const
{
    return _deviceNames.lookupName(deviceID);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Assign a role to a device ID
/// @param deviceID Device identifier
/// @param role Role string (e.g. "system"). Pass empty or "normal" to remove the mapping.
void DeviceManager::setDeviceRole(RaftDeviceID deviceID, const String& role)
{
    _deviceNames.setRole(deviceID, role);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get role assigned to a device ID
/// @param deviceID Device identifier
/// @return role string, or "normal" if no role mapped
String DeviceManager::getDeviceRole(RaftDeviceID deviceID) const
{
    return _deviceNames.getRole(deviceID);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get the device type key a device is expected to be (Devices entry "expectType")
String DeviceManager::getDeviceExpectedType(RaftDeviceID deviceID) const
{
    return _deviceNames.getExpectedType(deviceID);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check whether device has role "system"
bool DeviceManager::isSystemDevice(RaftDeviceID deviceID) const
{
    return _deviceNames.isSystem(deviceID);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Resolve a device name to a RaftDeviceID using the DeviceNames map
/// @param name Friendly device name
/// @param deviceID (out) resolved device ID
/// @return true if found in the name map
bool DeviceManager::resolveDeviceNameToID(const String& name, RaftDeviceID& deviceID) const
{
    return _deviceNames.resolveName(name, deviceID);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get a named value from a bus-discovered device using its cached poll data
/// @param deviceID Device identifier on the bus
/// @param paramName Attribute name (e.g. "ax", "temperature")
/// @param isValid (out) true if value was found and valid
/// @return attribute value (with divisor/addend applied)
double DeviceManager::getBusDeviceNamedValue(RaftDeviceID deviceID, const String& paramName, bool& isValid)
{
    isValid = false;

    // Get the bus
    RaftBus* pBus = raftBusSystem.getBusByNumber(deviceID.getBusNum());
    if (!pBus)
        return 0.0;

    // Get device type index for the address
    BusElemAddrType addr = deviceID.getAddress();
    DeviceTypeIndexType deviceTypeIndex = pBus->getDeviceTypeIndex(addr);
    if (deviceTypeIndex == DEVICE_TYPE_INDEX_INVALID)
        return 0.0;

    // Get device type record
    DeviceTypeRecord devTypeRec;
    if (!deviceTypeRecords.getDeviceInfo(deviceTypeIndex, devTypeRec))
        return 0.0;

    // Must have poll field descriptors
    if (!devTypeRec.pollFieldDescs || devTypeRec.pollFieldCount == 0 || devTypeRec.pollStructSize == 0)
        return 0.0;

    // Find the matching field by name
    const AttrFieldDesc* pMatchedField = nullptr;
    for (uint16_t i = 0; i < devTypeRec.pollFieldCount; i++)
    {
        if (paramName.equalsIgnoreCase(devTypeRec.pollFieldDescs[i].name))
        {
            pMatchedField = &devTypeRec.pollFieldDescs[i];
            break;
        }
    }
    if (!pMatchedField)
        return 0.0;

    // Get decoded poll data from the bus
    RaftBusDevicesIF* pDevicesIF = pBus->getBusDevicesIF();
    if (!pDevicesIF)
        return 0.0;

    // Allocate stack buffer for decoded struct
    // Limit stack allocation to a reasonable maximum
    if (devTypeRec.pollStructSize > 4096)
        return 0.0;
    uint8_t decodedBuf[devTypeRec.pollStructSize];
    memset(decodedBuf, 0, devTypeRec.pollStructSize);

    // Peek the latest value: neither drains the ring buffer (which would starve the publish
    // pipeline) nor takes the "new" state another reader of getLatestDecodedPollResponse() uses,
    // and gives the current value between polls rather than nothing
    RaftBusDeviceDecodeState decodeState;
    if (!pDevicesIF->peekLatestDecodedPollResponse(addr, decodedBuf, devTypeRec.pollStructSize, decodeState))
        return 0.0;

    // Read the value from the decoded buffer at the field's offset
    double rawVal = 0.0;
    const uint8_t* pField = decodedBuf + pMatchedField->offset;
    switch (pMatchedField->type)
    {
        case AttrType::Float:   { float v; memcpy(&v, pField, sizeof(v)); rawVal = v; break; }
        case AttrType::Int32:   { int32_t v; memcpy(&v, pField, sizeof(v)); rawVal = v; break; }
        case AttrType::Uint32:  { uint32_t v; memcpy(&v, pField, sizeof(v)); rawVal = v; break; }
        case AttrType::Int16:   { int16_t v; memcpy(&v, pField, sizeof(v)); rawVal = v; break; }
        case AttrType::Uint16:  { uint16_t v; memcpy(&v, pField, sizeof(v)); rawVal = v; break; }
        case AttrType::Int8:    { int8_t v = *reinterpret_cast<const int8_t*>(pField); rawVal = v; break; }
        case AttrType::Uint8:   { rawVal = *pField; break; }
        case AttrType::Bool:    { rawVal = (*pField) ? 1.0 : 0.0; break; }
    }

    // Apply what is still to be applied - the field table's divisor and addend are 1 and 0
    // where the decode has already scaled the value (see DecodeGenerator get_field_desc_def)
    if (pMatchedField->divisor != 0.0f && pMatchedField->divisor != 1.0f)
        rawVal /= pMatchedField->divisor;
    rawVal += pMatchedField->addend;

    isValid = true;
    return rawVal;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle devman/setname API endpoint
/// @param reqStr request string
/// @param respStr (out) response string
/// @param jsonParams JSON object containing "deviceid" and "name" fields
/// @return RaftRetCode
RaftRetCode DeviceManager::apiDevManSetName(const String &reqStr, String &respStr, const RaftJson& jsonParams)
{
    // Get device ID and name from params
    String deviceIDStr = jsonParams.getString("deviceid", "");
    String name = jsonParams.getString("name", "");

    if (deviceIDStr.length() == 0)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failDeviceIdMissing");
    if (name.length() == 0)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failNameMissing");

    RaftDeviceID deviceID = RaftDeviceID::fromString(deviceIDStr);
    if (!deviceID.isValid())
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failInvalidDeviceId");

    setDeviceName(deviceID, name);
    if (jsonParams.getBool("save", false))
    {
        _settingsStore.getOrAdd(deviceID.toString()).name = name;
        if (!saveDeviceSettings())
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failSave");
    }

#ifdef DEBUG_DEVICE_NAMES
    LOG_I(MODULE_PREFIX, "apiDevManSetName: %s -> %s", deviceIDStr.c_str(), name.c_str());
#endif

    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle devman/setrole API endpoint
/// @param reqStr request string
/// @param respStr (out) response string
/// @param jsonParams JSON object containing "deviceid" and "role" fields
/// @return RaftRetCode
/// @note In-memory only; persistence is not handled by this endpoint
RaftRetCode DeviceManager::apiDevManSetRole(const String &reqStr, String &respStr, const RaftJson& jsonParams)
{
    String deviceIDStr = jsonParams.getString("deviceid", "");
    String role = jsonParams.getString("role", "");

    if (deviceIDStr.length() == 0)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failDeviceIdMissing");

    RaftDeviceID deviceID = RaftDeviceID::fromString(deviceIDStr);
    if (!deviceID.isValid())
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failInvalidDeviceId");

    setDeviceRole(deviceID, role);
    if (jsonParams.getBool("save", false))
    {
        _settingsStore.getOrAdd(deviceID.toString()).role = role.equalsIgnoreCase("normal") ? String() : role;
        if (!saveDeviceSettings())
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failSave");
    }
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle devman/listdevs API endpoint
/// @param reqStr request string
/// @param respStr (out) response string
/// @param jsonParams optional "role" filter
/// @return RaftRetCode
/// @note Enumerates instantiated static devices and bus-detected devices only.
///       Undetected bus addresses are intentionally not reported.
RaftRetCode DeviceManager::apiDevManListDevs(const String &reqStr, String &respStr, const RaftJson& jsonParams)
{
    String roleFilter = jsonParams.getString("role", "");

    String devsJson;
    devsJson.reserve(256);
    devsJson += "[";
    bool first = true;

    auto appendDev = [&](RaftDeviceID deviceID, DeviceTypeIndexType dtIdx) {
        String role = getDeviceRole(deviceID);
        if (roleFilter.length() > 0 && !roleFilter.equalsIgnoreCase(role))
            return;
        if (!first)
            devsJson += ",";
        first = false;
        devsJson += "{\"deviceid\":\"";
        devsJson += deviceID.toString();
        devsJson += "\"";
        String name = lookupDeviceName(deviceID);
        if (name.length() > 0)
            RaftJson::appendStringField(devsJson, "name", name.c_str());
        RaftJson::appendStringField(devsJson, "displayName", displayNameFor(deviceID, dtIdx).c_str());
        if (!role.equalsIgnoreCase("normal"))
        {
            devsJson += ",\"role\":\"";
            devsJson += role;
            devsJson += "\"";
        }
        if (dtIdx != DEVICE_TYPE_INDEX_INVALID)
        {
            devsJson += ",\"dtIdx\":";
            devsJson += String(dtIdx);
        }
        devsJson += "}";
    };

    // Static devices
    if (RaftMutex_lock(_accessMutex, ACCESS_MUTEX_MAX_WAIT_MS))
    {
        for (auto& devRec : _staticDeviceList)
        {
            if (devRec.pDevice)
                appendDev(devRec.pDevice->getDeviceID(), devRec.pDevice->getDeviceTypeIndex());
        }
        RaftMutex_unlock(_accessMutex);
    }

    // Bus-detected devices
    for (RaftBus* pBus : raftBusSystem.getBusList())
    {
        if (!pBus)
            continue;
        RaftBusDevicesIF* pDevicesIF = pBus->getBusDevicesIF();
        if (!pDevicesIF)
            continue;
        // Ask for every known address rather than only those currently holding poll responses.
        // Poll responses are consumed by whatever is streaming device data, so filtering on them
        // makes an identified, online device vanish from this list whenever its samples have just
        // been read - and the slower a device polls, the more often it is missing. Measured on an
        // Axiom while streaming at 10Hz, a 500ms-poll sensor appeared in 7 of 40 consecutive
        // calls. Identity and online state are the stable facts, so filter on those. Undetected
        // addresses are still not reported, as documented above.
        std::vector<BusElemAddrType> addresses;
        pDevicesIF->getDeviceAddresses(addresses, false);
        for (BusElemAddrType addr : addresses)
        {
            DeviceTypeIndexType deviceTypeIndex = pBus->getDeviceTypeIndex(addr);
            if (deviceTypeIndex == DEVICE_TYPE_INDEX_INVALID)
                continue;
            if (!pBus->isElemResponding(addr))
                continue;
            RaftDeviceID deviceID(pBus->getBusNum(), addr);
            appendDev(deviceID, deviceTypeIndex);
        }
    }

    devsJson += "]";

    String extras = "\"devices\":";
    extras += devsJson;
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, extras.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle devman/slot API endpoint - set or query slot mode
/// @param reqStr request string
/// @param respStr (out) response string
/// @param jsonParams expected fields: "bus" (name or number), "slot" (1-based), optional "mode"
/// @return RaftRetCode
RaftRetCode DeviceManager::apiDevManSlot(const String &reqStr, String &respStr, const RaftJson& jsonParams)
{
    // Resolve bus
    String busName = jsonParams.getString("bus", "");
    if (busName.length() == 0)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusMissing");
    RaftBus* pBus = raftBusSystem.getBusByName(busName, true);
    if (!pBus)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusNotFound");

    // Slot number is required for set; optional for query (omit -> return all slots)
    String slotStr = jsonParams.getString("slot", "");
    String modeStr = jsonParams.getString("mode", "");

    // Query mode (no "mode" param) - return JSON describing slots on this bus
    if (modeStr.length() == 0)
    {
        String slotsJson = pBus->getSlotModesJson();
        String extra = "\"bus\":\"" + pBus->getBusName() + "\",\"slots\":" + slotsJson;
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, extra.c_str());
    }

    // Set mode - slot must be present
    if (slotStr.length() == 0)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failSlotMissing");
    int slotNum = slotStr.toInt();
    if (slotNum <= 0)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failInvalidSlot");

    RaftRetCode retc = pBus->setSlotMode((uint32_t)slotNum, modeStr.c_str());
    if (retc == RAFT_NOT_IMPLEMENTED)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusNoSlotControl");
    if (retc != RAFT_OK)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failSetMode");

    String extra = "\"bus\":\"" + pBus->getBusName() + "\",\"slot\":" + String(slotNum)
                 + ",\"mode\":\"" + modeStr + "\"";
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, extra.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Callback for command results
/// @param reqResult Result of the command
void DeviceManager::cmdResultReportCallback(BusRequestResult& reqResult)
{
    // Capture read data for a waiting cmdraw API call (this runs on the task which calls the bus's loop()
    // function - i.e. normally the main task; the ready flag is set only after the data is stored)
    if ((_cmdRawInFlightCmdId != 0) && (reqResult.getCmdId() == _cmdRawInFlightCmdId) && !_cmdRawResultReady)
    {
        if (reqResult.getReadData() && (reqResult.getReadDataLen() > 0))
            _cmdRawReadData.assign(reqResult.getReadData(), reqResult.getReadData() + reqResult.getReadDataLen());
        else
            _cmdRawReadData.clear();
        _cmdRawResult = reqResult.getResult();
        _cmdRawResultReady = true;
    }
#ifdef DEBUG_CMD_RESULT_CALLBACK
    LOG_I(MODULE_PREFIX, "cmdResultReportCallback len %d", reqResult.getReadDataLen());
    Raft::logHexBuf(reqResult.getReadData(), reqResult.getReadDataLen(), MODULE_PREFIX, "cmdResultReportCallback");
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Read the device type file settings and register the file as a record source (before buses start)
/// @note "typeFile" (default: DeviceLibrary/devLibFile, or /local/devlib.json; empty for none), "typeFileRequired"
///       (a missing file is an error), "typeFileTimeoutMs" (identification goes ahead without the file after this),
///       "typeFileLimits" (maxFileBytes, maxReadBytes, maxSamples, maxRecordRingBytes)
void DeviceManager::setupTypeFile()
{
    // Saved device settings file (loaded in postSetup)
    _settingsFilePath = modConfig().getString("deviceSettingsFile", "/local/devsettings.json");

    if (modConfig().contains("typeFile"))
        _typeFilePath = modConfig().getString("typeFile", "");
    else
        _typeFilePath = _sysConfig.getString("DeviceLibrary/devLibFile", "/local/devlib.json");
    _typeFileRequired = modConfig().getBool("typeFileRequired", false);
    _typeFileLimits.maxFileBytes = modConfig().getLong("typeFileLimits/maxFileBytes", _typeFileLimits.maxFileBytes);
    _typeFileLimits.maxReadBytesPerCommand = modConfig().getLong("typeFileLimits/maxReadBytes", _typeFileLimits.maxReadBytesPerCommand);
    _typeFileLimits.maxSamples = modConfig().getLong("typeFileLimits/maxSamples", _typeFileLimits.maxSamples);
    _typeFileLimits.maxRecordRingBytes = modConfig().getLong("typeFileLimits/maxRecordRingBytes", _typeFileLimits.maxRecordRingBytes);
    if (_typeFilePath.length() == 0)
        return;
    _typeFileState = "loading";
    _typeFileSourceId = deviceTypeRecords.registerRecordSource("typeFile",
                modConfig().getLong("typeFileTimeoutMs", DeviceTypeRecords::RECORD_SOURCE_TIMEOUT_MS_DEFAULT));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Load the device type file and mark its record source ready
void DeviceManager::loadTypeFile()
{
    if (_typeFileSourceId == UINT32_MAX)
        return;
    DeviceTypeRecords::ExtendedRecordStats statsBefore = deviceTypeRecords.getExtendedRecordStats();
    _typeFileRecordsAvailable = statsBefore.maxRecords > statsBefore.count ? statsBefore.maxRecords - statsBefore.count : 0;

    // "/local/devlib.json" is file system "local", file "devlib.json"; a name without a file system is on the default
    String fsName;
    String fileName = _typeFilePath;
    if (_typeFilePath.startsWith("/"))
    {
        int nextSlash = _typeFilePath.indexOf('/', 1);
        if (nextSlash > 1)
        {
            fsName = _typeFilePath.substring(1, nextSlash);
            fileName = _typeFilePath.substring(nextSlash + 1);
        }
    }

    // Read and apply
    uint32_t fileLen = 0;
    _typeFilePresent = fileSystem.getFileInfo(fsName, fileName, fileLen);
    if (!_typeFilePresent)
    {
        _typeFileState = _typeFileRequired ? "missing" : "none";
        if (_typeFileRequired)
        {
            _typeFileResult.firstError = "file not found";
            LOG_E(MODULE_PREFIX, "loadTypeFile %s not found (required) - using the compiled records only", _typeFilePath.c_str());
        }
    }
    else if (fileLen > _typeFileLimits.maxFileBytes)
    {
        _typeFileState = "error";
        _typeFileResult.rejected++;
        _typeFileResult.firstError = "file is " + String((int)fileLen) + " bytes, limit " + String((int)_typeFileLimits.maxFileBytes);
    }
    else
    {
        // getFileContents refuses a file of maxLen - 1 bytes or more, and null-terminates what it returns
        uint8_t* pContents = fileSystem.getFileContents(fsName, fileName, _typeFileLimits.maxFileBytes + 2);
        if (!pContents)
        {
            _typeFileState = "error";
            _typeFileResult.rejected++;
            _typeFileResult.firstError = "file could not be read";
        }
        else
        {
            _typeFileCrc16 = DeviceTypeFileLoader::crc16(pContents, strlen((const char*)pContents));
            _typeFileResult.keepRecordOutcomes = true;
            DeviceTypeFileLoader::applyJson(deviceTypeRecords, (const char*)pContents, _typeFileLimits, _typeFileResult);
            // The records the file added or replaced (a file being checked replaces them - devman/typefile/check)
            for (const DeviceTypeFileLoader::RecordOutcome& outcome : _typeFileResult.records)
                if ((outcome.outcome == DeviceTypeFileLoader::Outcome::Added) ||
                            (outcome.outcome == DeviceTypeFileLoader::Outcome::Replaced))
                    _typeFileKeys.push_back(outcome.key);
            _typeFileState = _typeFileResult.fileValid ? "applied" : "error";
            SpiramAwareAllocator<uint8_t> allocator;
            allocator.deallocate(pContents, 0);
        }
    }

    // Devices can now be identified
    deviceTypeRecords.markRecordSourceReady(_typeFileSourceId);
    if (_typeFilePresent)
    {
        LOG_I(MODULE_PREFIX, "loadTypeFile %s %s crc %04x added %d replaced %d unchanged %d disabled %d rejected %d warnings %d%s%s",
                    _typeFilePath.c_str(), _typeFileState, _typeFileCrc16, (int)_typeFileResult.added,
                    (int)_typeFileResult.replaced, (int)_typeFileResult.unchanged, (int)_typeFileResult.disabled,
                    (int)_typeFileResult.rejected, (int)_typeFileResult.warnings,
                    _typeFileResult.firstError.length() > 0 ? " first error: " : "", _typeFileResult.firstError.c_str());
    }
    else
    {
        LOG_I(MODULE_PREFIX, "loadTypeFile no device type file at %s", _typeFilePath.c_str());
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle devman/typefile (status) and devman/typefile/apply (restart to apply a new file)
RaftRetCode DeviceManager::apiDevManTypeFile(const String &reqStr, String &respStr, const RaftJson& jsonParams,
            const APISourceInfo& sourceInfo)
{
    String action = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 2);
    // devman/typefile?entries=1: the query isn't an action
    if (action.indexOf('=') >= 0)
        action = "";
    String stagedPath = jsonParams.getString("file", "");

    // Check a file: what it would do at the next startup, without changing anything
    if (action.equalsIgnoreCase("check"))
    {
        if (stagedPath.length() == 0)
            stagedPath = _typeFilePath;

        // With a deferred reply where the caller can take one (the main loop isn't held up)
        if (_pTypeFileJob)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusy");
        RaftRetCode jobRslt = startTypeFileJob(false, reqStr, stagedPath, sourceInfo, respStr);
        if (jobRslt == RAFT_RESP_DEFERRED)
            return jobRslt;
        DeviceTypeFileLoader::Result checkResult;
        String fileError;
        String json;
        if (!checkTypeFile(stagedPath, checkResult, json, fileError))
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, fileError.c_str());
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, json.c_str());
    }

    // Apply: a changed file is applied by a restart (the reply is sent first). With file=<path>, that file (e.g. one
    // just uploaded under another name) is checked first, and becomes the device type file only if nothing in it is
    // rejected
    if (action.equalsIgnoreCase("apply"))
    {
        if (_typeFilePath.length() == 0)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failNoTypeFile");
        if ((stagedPath.length() > 0) && (stagedPath != _typeFilePath))
        {
            // With a deferred reply where the caller can take one (the main loop isn't held up)
            if (_pTypeFileJob)
                return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusy");
            RaftRetCode jobRslt = startTypeFileJob(true, reqStr, stagedPath, sourceInfo, respStr);
            if (jobRslt == RAFT_RESP_DEFERRED)
                return jobRslt;
            DeviceTypeFileLoader::Result checkResult;
            String fileError;
            String json;
            if (!checkTypeFile(stagedPath, checkResult, json, fileError))
                return Raft::setJsonErrorResult(reqStr.c_str(), respStr, fileError.c_str());
            if (!checkResult.fileValid || (checkResult.rejected > 0))
            {
                Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failCheck");
                // Include what the check found (the result is {"req":..,"rslt":"fail","error":..} - add the fields)
                if (respStr.endsWith("}"))
                    respStr = respStr.substring(0, respStr.length() - 1) + "," + json + "}";
                return RAFT_INVALID_DATA;
            }
            String text;
            bool present = false;
            if (!readFileText(stagedPath, _typeFileLimits.maxFileBytes, text, present) || !present)
                return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failFileRead");
            if (!writeFileText(_typeFilePath, text))
                return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failTypeFileWrite");
            String fsName, fileName;
            splitFsPath(stagedPath, fsName, fileName);
            fileSystem.deleteFile(fsName, fileName);
            LOG_I(MODULE_PREFIX, "apiDevManTypeFile apply %s as %s", stagedPath.c_str(), _typeFilePath.c_str());
        }
        LOG_I(MODULE_PREFIX, "apiDevManTypeFile apply - restarting to load %s", _typeFilePath.c_str());
        if (getSysManager())
            getSysManager()->systemRestart();
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, "\"restarting\":true");
    }
    if (action.length() > 0)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failUnknownCmd");

    // Status
    DeviceTypeRecords::ExtendedRecordStats stats = deviceTypeRecords.getExtendedRecordStats();
    char crcHex[8];
    snprintf(crcHex, sizeof(crcHex), "%04x", (unsigned)_typeFileCrc16);
    String json;
    RaftJson::appendStringField(json, "file", _typeFilePath.c_str());
    RaftJson::appendStringField(json, "state", _typeFileState);
    json += ",\"present\":" + String(_typeFilePresent ? "true" : "false");
    json += ",\"applied\":" + String(_typeFileResult.anyApplied() ? "true" : "false");
    json += ",\"crc\":\"" + String(crcHex) + "\"";
    json += ",\"added\":" + String((int)_typeFileResult.added);
    json += ",\"replaced\":" + String((int)_typeFileResult.replaced);
    json += ",\"unchanged\":" + String((int)_typeFileResult.unchanged);
    json += ",\"disabled\":" + String((int)_typeFileResult.disabled);
    json += ",\"rejected\":" + String((int)_typeFileResult.rejected);
    json += ",\"warnings\":" + String((int)_typeFileResult.warnings);
    RaftJson::appendStringField(json, "firstError", _typeFileResult.firstError.c_str());
    RaftJson::appendStringField(json, "firstWarning", _typeFileResult.firstWarning.c_str());
    json += ",\"records\":" + String((int)stats.count) + ",\"maxRecords\":" + String((int)stats.maxRecords);
    json += ",\"bytes\":" + String((int)stats.bytesUsed) + ",\"maxBytes\":" + String((int)stats.maxBytes);
    json += ",\"maxFileBytes\":" + String((int)_typeFileLimits.maxFileBytes);
    json += ",\"identReady\":" + String(deviceTypeRecords.areRecordSourcesReady() ? "true" : "false");
    json += ",\"restartRequired\":" + String(isTypeFileRestartRequired() ? "true" : "false");
    if (jsonParams.getBool("entries", false))
        json += ",\"entries\":" + typeFileEntriesJson(_typeFileResult);
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, json.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief What happened to each entry of a device type file, as a JSON array
String DeviceManager::typeFileEntriesJson(const DeviceTypeFileLoader::Result& result)
{
    String json = "[";
    for (const DeviceTypeFileLoader::RecordOutcome& outcome : result.records)
    {
        if (json.length() > 1)
            json += ",";
        String entry;
        RaftJson::appendStringField(entry, "key", outcome.key.c_str());
        RaftJson::appendStringField(entry, "outcome", DeviceTypeFileLoader::getOutcomeStr(outcome.outcome));
        if (outcome.error.length() > 0)
            RaftJson::appendStringField(entry, "error", outcome.error.c_str());
        if (outcome.warnings.length() > 0)
            RaftJson::appendStringField(entry, "warnings", outcome.warnings.c_str());
        if (outcome.noDecoder)
            entry += ",\"noDecoder\":true";
        if (outcome.decoder.length() > 0)
            RaftJson::appendStringField(entry, "decoder", outcome.decoder.c_str());
        json += "{" + entry + "}";
    }
    return json + "]";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check a device type file (what it would do at the next startup) and describe the result as JSON fields
/// @return false if the file can't be read (fileError says why)
bool DeviceManager::checkTypeFile(const String& path, DeviceTypeFileLoader::Result& result, String& json, String& fileError)
{
    String text;
    bool present = false;
    if (!readFileText(path, _typeFileLimits.maxFileBytes, text, present))
    {
        fileError = "failFileTooBigOrUnreadable";
        return false;
    }
    if (!present)
    {
        fileError = "failFileNotFound";
        return false;
    }
    DeviceTypeFileLoader::checkJson(deviceTypeRecords, _typeFileKeys, text.c_str(), _typeFileLimits, result);
    json = typeFileCheckJson(path, text.c_str(), text.length(), result);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief The fields of a type file check's reply
String DeviceManager::typeFileCheckJson(const String& path, const char* pText, uint32_t textLen,
            const DeviceTypeFileLoader::Result& result)
{
    char crcHex[8];
    snprintf(crcHex, sizeof(crcHex), "%04x", (unsigned)DeviceTypeFileLoader::crc16((const uint8_t*)pText, textLen));
    String json;
    RaftJson::appendStringField(json, "file", path.c_str());
    json += ",\"bytes\":" + String((int)textLen) + ",\"crc\":\"" + String(crcHex) + "\"";
    json += ",\"valid\":" + String(result.fileValid && (result.rejected == 0) ? "true" : "false");
    json += ",\"added\":" + String((int)result.added) + ",\"replaced\":" + String((int)result.replaced);
    json += ",\"unchanged\":" + String((int)result.unchanged) + ",\"disabled\":" + String((int)result.disabled);
    json += ",\"rejected\":" + String((int)result.rejected) + ",\"warnings\":" + String((int)result.warnings);
    RaftJson::appendStringField(json, "firstError", result.firstError.c_str());
    RaftJson::appendStringField(json, "firstWarning", result.firstWarning.c_str());
    json += ",\"entries\":" + typeFileEntriesJson(result);
    return json;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Type file check / apply job
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct DeviceManager::TypeFileJob
{
    enum class Stage : uint8_t { Read, Check, Write };
    Stage stage = Stage::Read;
    bool apply = false;
    String reqStr;
    String path;
    uint32_t deferToken = 0;

    // The file (read a chunk at a time; in PSRAM where there is any)
    String fsName;
    String fileName;
    FILE* pFile = nullptr;
    uint32_t fileLen = 0;
    uint32_t pos = 0;
    std::vector<char, SpiramAwareAllocator<char>> text;

    // The check (in the check task): what it is given, and its result
    std::vector<String> fileKeys;
    DeviceTypeFileLoader::Limits limits;
    DeviceTypeFileLoader::Result result;
    String json;
    std::atomic<bool> checkDone{false};
    void* pTask = nullptr;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Start a check (or an apply - a check then the file written as the type file) with a deferred reply
/// @return RAFT_RESP_DEFERRED if started, otherwise the caller replies at once (respStr unchanged)
RaftRetCode DeviceManager::startTypeFileJob(bool apply, const String& reqStr, const String& path,
            const APISourceInfo& sourceInfo, String& respStr)
{
    (void)respStr;
    RestAPIEndpointManager* pRestAPIEndpointManager = getRestAPIEndpointManager();
    if (!sourceInfo.canDefer || !pRestAPIEndpointManager ||
                (pRestAPIEndpointManager->deferResponse(reqStr.c_str(), sourceInfo, TYPE_FILE_JOB_TIMEOUT_MS) != RAFT_RESP_DEFERRED))
        return RAFT_OK;
    _pTypeFileJob.reset(new TypeFileJob());
    _pTypeFileJob->apply = apply;
    _pTypeFileJob->reqStr = reqStr;
    _pTypeFileJob->path = path;
    _pTypeFileJob->deferToken = sourceInfo.deferToken;
    _pTypeFileJob->fileKeys = _typeFileKeys;
    _pTypeFileJob->limits = _typeFileLimits;
    splitFsPath(path, _pTypeFileJob->fsName, _pTypeFileJob->fileName);
    return RAFT_RESP_DEFERRED;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief The check (no flash access: it runs in a task with its stack in PSRAM)
void DeviceManager::typeFileCheckWork(TypeFileJob& job)
{
    DeviceTypeFileLoader::checkJson(deviceTypeRecords, job.fileKeys, job.text.data(), job.limits, job.result);
    job.json = typeFileCheckJson(job.path, job.text.data(), job.fileLen, job.result);
}

void DeviceManager::typeFileCheckTaskFn(void* pArg)
{
#ifdef DEVMAN_TYPE_FILE_CHECK_TASK
    TypeFileJob* pJob = (TypeFileJob*)pArg;
    typeFileCheckWork(*pJob);
    pJob->checkDone = true;
    // Deleted by the main loop (vTaskDeleteWithCaps frees the stack - not from the task itself)
    while (true)
        vTaskSuspend(nullptr);
#else
    (void)pArg;
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Advance the type file job a step (from loop())
void DeviceManager::serviceTypeFileJob()
{
    TypeFileJob& job = *_pTypeFileJob;
    switch (job.stage)
    {
        case TypeFileJob::Stage::Read:
        {
            if (!job.pFile)
            {
                uint32_t fileLen = 0;
                if (!fileSystem.getFileInfo(job.fsName, job.fileName, fileLen))
                {
                    String respStr;
                    Raft::setJsonErrorResult(job.reqStr.c_str(), respStr, "failFileNotFound");
                    finishTypeFileJob(respStr);
                    return;
                }
                if (fileLen > job.limits.maxFileBytes)
                {
                    String respStr;
                    Raft::setJsonErrorResult(job.reqStr.c_str(), respStr, "failFileTooBigOrUnreadable");
                    finishTypeFileJob(respStr);
                    return;
                }
                job.pFile = fileSystem.fileOpen(job.fsName, job.fileName, false, 0);
                if (!job.pFile)
                {
                    String respStr;
                    Raft::setJsonErrorResult(job.reqStr.c_str(), respStr, "failFileTooBigOrUnreadable");
                    finishTypeFileJob(respStr);
                    return;
                }
                job.fileLen = fileLen;
                job.text.resize(fileLen + 1);
                job.pos = 0;
                return;
            }
            uint32_t len = job.fileLen - job.pos;
            if (len > TYPE_FILE_READ_CHUNK)
                len = TYPE_FILE_READ_CHUNK;
            uint32_t numRead = (len > 0) ? fileSystem.fileRead(job.pFile, (uint8_t*)job.text.data() + job.pos, len) : 0;
            job.pos += numRead;
            if ((numRead == len) && (job.pos < job.fileLen))
                return;
            fileSystem.fileClose(job.pFile, job.fsName, job.fileName, false);
            job.pFile = nullptr;
            job.fileLen = job.pos;
            job.text[job.fileLen] = 0;

            // The check: in its own task if it can be made, otherwise here
            job.stage = TypeFileJob::Stage::Check;
#ifdef DEVMAN_TYPE_FILE_CHECK_TASK
            TaskHandle_t taskHandle = nullptr;
            // The lowest priority, on core 0: it only uses spare time there. At the main task's priority it held the
            // main task off for up to 300 ms (equal-priority tasks, one pinned and one not, aren't reliably time-sliced),
            // and sharing core 0 with that core's idle task (at its priority) keeps the task watchdog fed in a long check
            if (xTaskCreatePinnedToCoreWithCaps(typeFileCheckTaskFn, "TypeFileChk", TYPE_FILE_CHECK_TASK_STACK, &job,
                        TYPE_FILE_CHECK_TASK_PRIORITY, &taskHandle, TYPE_FILE_CHECK_TASK_CORE,
                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS)
            {
                job.pTask = taskHandle;
                return;
            }
            LOG_W(MODULE_PREFIX, "typefile check task not created - checking in the main loop");
#endif
            typeFileCheckWork(job);
            job.checkDone = true;
            return;
        }
        case TypeFileJob::Stage::Check:
        {
            if (!job.checkDone)
                return;
#ifdef DEVMAN_TYPE_FILE_CHECK_TASK
            if (job.pTask)
            {
                vTaskDeleteWithCaps((TaskHandle_t)job.pTask);
                job.pTask = nullptr;
            }
#endif
            String respStr;
            if (!job.apply)
            {
                Raft::setJsonBoolResult(job.reqStr.c_str(), respStr, true, job.json.c_str());
                finishTypeFileJob(respStr);
                return;
            }
            if (!job.result.fileValid || (job.result.rejected > 0))
            {
                Raft::setJsonErrorResult(job.reqStr.c_str(), respStr, "failCheck");
                // Include what the check found (the result is {"req":..,"rslt":"fail","error":..} - add the fields)
                if (respStr.endsWith("}"))
                    respStr = respStr.substring(0, respStr.length() - 1) + "," + job.json + "}";
                finishTypeFileJob(respStr);
                return;
            }

            // Apply: the checked file becomes the type file - renamed (atomic: a failure leaves the type file as it
            // was) if on the same file system, otherwise copied a chunk per call
            String fsName, fileName;
            splitFsPath(_typeFilePath, fsName, fileName);
            if (fsName.equalsIgnoreCase(job.fsName))
            {
                if (!fileSystem.renameFile(fsName, job.fileName, fileName))
                {
                    Raft::setJsonErrorResult(job.reqStr.c_str(), respStr, "failTypeFileWrite");
                    finishTypeFileJob(respStr);
                    return;
                }
                LOG_I(MODULE_PREFIX, "apiDevManTypeFile apply %s as %s - restarting to load it", job.path.c_str(), _typeFilePath.c_str());
                Raft::setJsonBoolResult(job.reqStr.c_str(), respStr, true, "\"restarting\":true");
                finishTypeFileJob(respStr);
                if (getSysManager())
                    getSysManager()->systemRestart();
                return;
            }
            job.pFile = fileSystem.fileOpen(fsName, fileName, true, 0);
            if (!job.pFile)
            {
                Raft::setJsonErrorResult(job.reqStr.c_str(), respStr, "failTypeFileWrite");
                finishTypeFileJob(respStr);
                return;
            }
            job.pos = 0;
            job.stage = TypeFileJob::Stage::Write;
            return;
        }
        case TypeFileJob::Stage::Write:
        {
            String fsName, fileName;
            splitFsPath(_typeFilePath, fsName, fileName);
            uint32_t len = job.fileLen - job.pos;
            if (len > TYPE_FILE_WRITE_CHUNK)
                len = TYPE_FILE_WRITE_CHUNK;
            uint32_t written = (len > 0) ? fileSystem.fileWrite(job.pFile, (const uint8_t*)job.text.data() + job.pos, len) : 0;
            job.pos += written;
            if ((written == len) && (job.pos < job.fileLen))
                return;
            fileSystem.fileClose(job.pFile, fsName, fileName, true);
            job.pFile = nullptr;
            String respStr;
            if (job.pos < job.fileLen)
            {
                Raft::setJsonErrorResult(job.reqStr.c_str(), respStr, "failTypeFileWrite");
                finishTypeFileJob(respStr);
                return;
            }
            fileSystem.deleteFile(job.fsName, job.fileName);
            LOG_I(MODULE_PREFIX, "apiDevManTypeFile apply %s as %s - restarting to load it", job.path.c_str(), _typeFilePath.c_str());
            Raft::setJsonBoolResult(job.reqStr.c_str(), respStr, true, "\"restarting\":true");
            finishTypeFileJob(respStr);
            if (getSysManager())
                getSysManager()->systemRestart();
            return;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Reply to the job's request and end it
void DeviceManager::finishTypeFileJob(const String& respStr)
{
    RestAPIEndpointManager* pRestAPIEndpointManager = getRestAPIEndpointManager();
    if (pRestAPIEndpointManager)
        pRestAPIEndpointManager->completeDeferred(_pTypeFileJob->deferToken, respStr);
    if (_pTypeFileJob->pFile)
        fileSystem.fileClose(_pTypeFileJob->pFile, _pTypeFileJob->fsName, _pTypeFileJob->fileName, false);
    _pTypeFileJob.reset();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle devlib: the device type file status with the fields of the feature/device-library branch's
///        endpoint (file, present, applied, appliedCrc16, added - records in effect from the file, suppressed,
///        rejected, maxRecords - records free for the file, maxFileBytes, firstError)
RaftRetCode DeviceManager::apiDevLib(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    (void)sourceInfo;
    char crcHex[8];
    snprintf(crcHex, sizeof(crcHex), "%04x", (unsigned)_typeFileCrc16);
    String json;
    RaftJson::appendStringField(json, "file", _typeFilePath.c_str());
    json += ",\"present\":" + String(_typeFilePresent ? "true" : "false");
    json += ",\"applied\":" + String(_typeFileResult.anyApplied() ? "true" : "false");
    json += ",\"appliedCrc16\":\"" + String(crcHex) + "\"";
    json += ",\"added\":" + String((int)(_typeFileResult.added + _typeFileResult.replaced + _typeFileResult.unchanged));
    json += ",\"suppressed\":" + String((int)_typeFileResult.disabled);
    json += ",\"rejected\":" + String((int)_typeFileResult.rejected);
    json += ",\"maxRecords\":" + String((int)_typeFileRecordsAvailable);
    json += ",\"maxFileBytes\":" + String((int)_typeFileLimits.maxFileBytes);
    RaftJson::appendStringField(json, "firstError", _typeFileResult.firstError.c_str());
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, json.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check whether the device type file differs from the one loaded at startup
/// @note A change to device type records is applied by a restart (devman/typefile/apply): a client making several
///       changes restarts once, when it chooses
bool DeviceManager::isTypeFileRestartRequired()
{
    if (_typeFileChanged)
        return true;
    if (_typeFilePath.length() == 0)
        return false;
    String fsName;
    String fileName = _typeFilePath;
    if (_typeFilePath.startsWith("/"))
    {
        int nextSlash = _typeFilePath.indexOf('/', 1);
        if (nextSlash > 1)
        {
            fsName = _typeFilePath.substring(1, nextSlash);
            fileName = _typeFilePath.substring(nextSlash + 1);
        }
    }
    uint32_t fileLen = 0;
    bool presentNow = fileSystem.getFileInfo(fsName, fileName, fileLen);
    if (presentNow != _typeFilePresent)
        return true;
    if (!presentNow || (fileLen > _typeFileLimits.maxFileBytes))
        return false;
    uint8_t* pContents = fileSystem.getFileContents(fsName, fileName, _typeFileLimits.maxFileBytes + 2);
    if (!pContents)
        return false;
    uint16_t crcNow = DeviceTypeFileLoader::crc16(pContents, strlen((const char*)pContents));
    SpiramAwareAllocator<uint8_t> allocator;
    allocator.deallocate(pContents, 0);
    return crcNow != _typeFileCrc16;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Ask for devices to be identified again
uint32_t DeviceManager::requestReIdentify(const BusReIdentRequest& request, BusNumType busNum)
{
    uint32_t numAccepted = 0;
    for (RaftBus* pBus : raftBusSystem.getBusList())
    {
        if (!pBus || ((request.scope == BusReIdentScope::Address) && (pBus->getBusNum() != busNum)))
            continue;
        RaftBusDevicesIF* pDevicesIF = pBus->getBusDevicesIF();
        if (pDevicesIF && pDevicesIF->requestReIdentify(request))
            numAccepted++;
    }
    return numAccepted;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set a device type's policy at runtime and identify the devices it affects again
DeviceTypeRecords::PolicyResult DeviceManager::setTypePolicy(const char* deviceTypeKey, uint8_t policy)
{
    DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
    if (!deviceTypeKey || !deviceTypeRecords.getRecord(deviceTypeKey, deviceTypeIndex))
        return DeviceTypeRecords::PolicyResult::NotFound;
    uint8_t prevPolicy = deviceTypeRecords.getPolicy(deviceTypeIndex);
    DeviceTypeRecords::PolicyResult result = deviceTypeRecords.setPolicy(deviceTypeIndex, policy);
    if ((result != DeviceTypeRecords::PolicyResult::Set) || (prevPolicy == policy))
        return result;

    // Devices identified as a type no longer enabled are identified again (they become another type, or Lost);
    // a type now enabled is tried for devices it outranks (devices that match nothing are tried again anyway, as
    // the records have changed)
    BusReIdentRequest request;
    request.scope = (policy == DeviceTypeRecord::POLICY_ENABLED) ? BusReIdentScope::Outranked : BusReIdentScope::TypeIndex;
    request.deviceTypeIndex = deviceTypeIndex;
    requestReIdentify(request);
    LOG_I(MODULE_PREFIX, "setTypePolicy %s %s", deviceTypeKey, DeviceTypeRecord::getPolicyStr(policy));
    return result;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle devman/reident (identify devices again)
RaftRetCode DeviceManager::apiDevManReIdent(const String &reqStr, String &respStr, const RaftJson& jsonParams)
{
    BusReIdentRequest request;
    BusNumType busNum = 0;
    DeviceTypeIndexType requestedTypeIndex = DEVICE_TYPE_INDEX_INVALID;
    bool typeGiven = false;
    const DeviceTypeRecord* pRequestedRec = getRequestedType(jsonParams, requestedTypeIndex, typeGiven);
    if (jsonParams.getBool("all", false))
    {
        request.scope = BusReIdentScope::All;
    }
    else if (jsonParams.getBool("unidentified", false))
    {
        request.scope = BusReIdentScope::Unidentified;
    }
    else if (typeGiven)
    {
        request.scope = BusReIdentScope::TypeIndex;
        request.deviceTypeIndex = requestedTypeIndex;
        if (!pRequestedRec)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failTypeNotFound");
    }
    else
    {
        RaftDeviceID deviceID;
        RaftBus* pBus = nullptr;
        String errorStr;
        if (resolveDeviceIDAndBus(jsonParams, deviceID, pBus, errorStr) != RAFT_OK)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, errorStr.c_str());
        if (!pBus)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failDeviceNotOnBus");
        request.scope = BusReIdentScope::Address;
        request.address = deviceID.getAddress();
        busNum = pBus->getBusNum();
    }
    uint32_t numBuses = requestReIdentify(request, busNum);
    String extra = "\"buses\":" + String((int)numBuses);
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, numBuses > 0, extra.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle devman/typepolicy (set a device type's policy at runtime, not saved)
RaftRetCode DeviceManager::apiDevManTypePolicy(const String &reqStr, String &respStr, const RaftJson& jsonParams)
{
    String policyStr = jsonParams.getString("policy", "");
    DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
    bool typeGiven = false;
    const DeviceTypeRecord* pRec = getRequestedType(jsonParams, deviceTypeIndex, typeGiven);
    if (!pRec || !pRec->deviceType)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failTypeNotFound");
    uint8_t policy = DeviceTypeRecord::POLICY_ENABLED;
    if (policyStr.equalsIgnoreCase("disabled"))
        policy = DeviceTypeRecord::POLICY_DISABLED;
    else if (policyStr.equalsIgnoreCase("disabledClaim"))
        policy = DeviceTypeRecord::POLICY_DISABLED_CLAIM;
    else if (!policyStr.equalsIgnoreCase("enabled"))
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failUnknownPolicy");
    DeviceTypeRecords::PolicyResult result = setTypePolicy(pRec->deviceType, policy);
    if (result != DeviceTypeRecords::PolicyResult::Set)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr,
                    result == DeviceTypeRecords::PolicyResult::Protected ? "failProtected" : "failTypeNotFound");
    String extra;
    RaftJson::appendStringField(extra, "deviceTypeKey", pRec->deviceType);
    RaftJson::appendStringField(extra, "policy", DeviceTypeRecord::getPolicyStr(policy));

    // Save it in the type file (applied at the next restart - the change above applies now) if asked
    if (jsonParams.getBool("save", false))
    {
        if (_typeFilePath.length() == 0)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failNoTypeFile");
        String fileText;
        bool present = false;
        if (!readFileText(_typeFilePath, _typeFileLimits.maxFileBytes, fileText, present))
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failTypeFileRead");
        bool changed = false;
        String newText = DeviceTypeFileLoader::setPolicyInFile(fileText.c_str(), pRec->deviceType, policy, changed);
        if (changed)
        {
            if (!writeFileText(_typeFilePath, newText))
                return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failTypeFileWrite");
            markTypeFileChanged();
        }
        extra += String(",\"saved\":true,\"restartRequired\":") + (isTypeFileRestartRequired() ? "true" : "false");
    }
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, extra.c_str());
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Saved device settings
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Split a file path: "/local/x.json" is file system "local", file "x.json"; a name without a file system
///        is on the default file system
void DeviceManager::splitFsPath(const String& path, String& fsName, String& fileName)
{
    fsName = "";
    fileName = path;
    if (path.startsWith("/"))
    {
        int nextSlash = path.indexOf('/', 1);
        if (nextSlash > 1)
        {
            fsName = path.substring(1, nextSlash);
            fileName = path.substring(nextSlash + 1);
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Read a text file
/// @param path file path
/// @param maxBytes largest file read
/// @param text (out) contents (empty if there is no file)
/// @param present (out) whether the file exists
/// @return false if the file exists but couldn't be read (or is too big)
bool DeviceManager::readFileText(const String& path, uint32_t maxBytes, String& text, bool& present)
{
    text = "";
    String fsName, fileName;
    splitFsPath(path, fsName, fileName);
    uint32_t fileLen = 0;
    present = fileSystem.getFileInfo(fsName, fileName, fileLen);
    if (!present)
        return true;
    if (fileLen > maxBytes)
        return false;
    // getFileContents refuses a file of maxLen - 1 bytes or more, and null-terminates what it returns
    uint8_t* pContents = fileSystem.getFileContents(fsName, fileName, maxBytes + 2);
    if (!pContents)
        return false;
    text = (const char*)pContents;
    SpiramAwareAllocator<uint8_t> allocator;
    allocator.deallocate(pContents, 0);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Write a text file
bool DeviceManager::writeFileText(const String& path, const String& text)
{
    String fsName, fileName;
    splitFsPath(path, fsName, fileName);
    String contents = text;
    return fileSystem.setFileContents(fsName, fileName, contents);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Load the saved device settings (names, roles, devconfig settings and polling) and apply them
/// @note devconfig settings and polling are applied when the device is identified (as the type the settings were
///       saved for); names and roles at once
void DeviceManager::loadDeviceSettings()
{
    if (_settingsFilePath.length() == 0)
        return;
    String text;
    bool present = false;
    if (!readFileText(_settingsFilePath, MAX_DEVICE_SETTINGS_FILE_BYTES, text, present))
    {
        LOG_W(MODULE_PREFIX, "loadDeviceSettings %s could not be read", _settingsFilePath.c_str());
        return;
    }
    if (!present)
        return;
    if (!_settingsStore.loadJson(text.c_str()))
    {
        LOG_W(MODULE_PREFIX, "loadDeviceSettings %s is not valid", _settingsFilePath.c_str());
        return;
    }
    uint32_t numDevices = 0;
    for (const auto& entry : _settingsStore.all())
    {
        RaftDeviceID deviceID = RaftDeviceID::fromString(String(entry.first.c_str()));
        if (!deviceID.isValid())
            continue;
        const DeviceSettingsStore::Settings& settings = entry.second;
        numDevices++;
        if (settings.name.length() > 0)
            setDeviceName(deviceID, settings.name);
        if (settings.role.length() > 0)
            setDeviceRole(deviceID, settings.role);
        if ((settings.typeKey.length() > 0) && !settings.devConfig.empty())
        {
            DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
            if (deviceTypeRecords.getRecord(settings.typeKey.c_str(), deviceTypeIndex))
            {
                DevConfigSettings& devConfig = _devConfigSettings[DeviceNames::packDeviceIDKey(deviceID)];
                devConfig.deviceTypeIndex = deviceTypeIndex;
                devConfig.values = settings.devConfig;
            }
        }
        if (settings.pollEnabled == 0)
            _pollingStopped[DeviceNames::packDeviceIDKey(deviceID)] = true;
    }
    LOG_I(MODULE_PREFIX, "loadDeviceSettings %s settings for %d devices", _settingsFilePath.c_str(), (int)numDevices);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Save the device settings
bool DeviceManager::saveDeviceSettings()
{
    if (_settingsFilePath.length() == 0)
        return false;
    bool ok = writeFileText(_settingsFilePath, _settingsStore.toJson());
    if (!ok)
        LOG_W(MODULE_PREFIX, "saveDeviceSettings %s could not be written", _settingsFilePath.c_str());
    return ok;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Stop polling a device that has been identified if its polling has been stopped (devman/devenable)
void DeviceManager::applyPollingSetting(RaftDeviceID deviceID)
{
    if (_pollingStopped.find(DeviceNames::packDeviceIDKey(deviceID)) == _pollingStopped.end())
        return;
    RaftBus* pBus = raftBusSystem.getBusByNumber(deviceID.getBusNum());
    RaftBusDevicesIF* pBusDevices = pBus ? pBus->getBusDevicesIF() : nullptr;
    if (pBusDevices)
        pBusDevices->setDevicePollingEnabled(deviceID.getAddress(), false);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get a device's display name (see getDeviceDisplayName)
String DeviceManager::displayNameFor(RaftDeviceID deviceID, DeviceTypeIndexType deviceTypeIndex) const
{
    String name = lookupDeviceName(deviceID);
    if (name.length() > 0)
        return name;
    String typeName;
    const DeviceTypeRecord* pRec = deviceTypeRecords.getRecord(deviceTypeIndex);
    if (pRec)
    {
        if (pRec->devInfoJson)
            typeName = RaftJson::getStringIm(pRec->devInfoJson, pRec->devInfoJson + strlen(pRec->devInfoJson), "name", "");
        if ((typeName.length() == 0) && pRec->deviceType)
            typeName = pRec->deviceType;
    }
    if (typeName.length() == 0)
        return deviceID.toString();
    return typeName + " " + deviceID.toString();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief A device's display name: its name if it has one, otherwise its type's display name and its device ID
String DeviceManager::getDeviceDisplayName(RaftDeviceID deviceID) const
{
    DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
    if (deviceID.getBusNum() == RaftDeviceID::BUS_NUM_DIRECT_CONN)
    {
        RaftDevice* pDevice = const_cast<DeviceManager*>(this)->getDevice(deviceID);
        if (pDevice)
            deviceTypeIndex = pDevice->getDeviceTypeIndex();
    }
    else
    {
        RaftBus* pBus = raftBusSystem.getBusByNumber(deviceID.getBusNum());
        if (pBus)
            deviceTypeIndex = pBus->getDeviceTypeIndex(deviceID.getAddress());
    }
    return displayNameFor(deviceID, deviceTypeIndex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Stage 10 endpoints
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle devman/types?offset=<n>&count=<n> - the device type table (index, key, source and policy)
RaftRetCode DeviceManager::apiDevManTypes(const String &reqStr, String &respStr, const RaftJson& jsonParams)
{
    uint32_t offset = jsonParams.getLong("offset", 0);
    uint32_t count = jsonParams.getLong("count", 50);
    if (count > 200)
        count = 200;
    // Extended records are indexed after the base records
    uint32_t total = DeviceTypeRecords::getNumBaseRecords() + deviceTypeRecords.getExtendedRecordStats().count;
    String json = "\"total\":" + String((int)total) + ",\"generation\":" + String((int)deviceTypeRecords.getGeneration()) +
                ",\"types\":[";
    bool first = true;
    for (uint32_t pos = offset; (pos < total) && (pos < offset + count); pos++)
    {
        DeviceTypeIndexType deviceTypeIndex = pos;
        const DeviceTypeRecord* pRec = deviceTypeRecords.getRecord(deviceTypeIndex);
        if (!pRec)
            continue;
        String entry = "{\"dtIdx\":" + String((int)deviceTypeIndex);
        RaftJson::appendStringField(entry, "key", pRec->deviceType ? pRec->deviceType : "");
        RaftJson::appendStringField(entry, "src", DeviceTypeRecords::getRecordSourceStr(deviceTypeRecords.getRecordSource(deviceTypeIndex)));
        RaftJson::appendStringField(entry, "policy", DeviceTypeRecord::getPolicyStr(deviceTypeRecords.getPolicy(deviceTypeIndex)));
        entry += "}";
        if (!first)
            json += ",";
        first = false;
        json += entry;
    }
    json += "]";
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, json.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle devman/identinfo?deviceid=<id> - the identification state of a device on a bus
RaftRetCode DeviceManager::apiDevManIdentInfo(const String &reqStr, String &respStr, const RaftJson& jsonParams)
{
    RaftDeviceID deviceID;
    RaftBus* pBus = nullptr;
    String errorStr;
    if (resolveDeviceIDAndBus(jsonParams, deviceID, pBus, errorStr) != RAFT_OK)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, errorStr.c_str());
    RaftBusDevicesIF* pBusDevices = pBus ? pBus->getBusDevicesIF() : nullptr;
    if (!pBusDevices)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failDeviceNotOnBus");
    String info = pBusDevices->getIdentInfoJson(deviceID.getAddress());
    if (info.length() == 0)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failNotSupported");
    String extra = "\"deviceID\":\"" + deviceID.toString() + "\",\"ident\":" + info;
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, extra.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle devman/reinit?deviceid=<id> - run a device's initialisation again (its devconfig settings are then
///        applied again). The response is deferred until it is done if the transport allows, otherwise it reports
///        that the request was queued
RaftRetCode DeviceManager::apiDevManReInit(const String &reqStr, String &respStr, const RaftJson& jsonParams,
            const APISourceInfo& sourceInfo)
{
    RaftDeviceID deviceID;
    RaftBus* pBus = nullptr;
    String errorStr;
    if (resolveDeviceIDAndBus(jsonParams, deviceID, pBus, errorStr) != RAFT_OK)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, errorStr.c_str());
    RaftBusDevicesIF* pBusDevices = pBus ? pBus->getBusDevicesIF() : nullptr;
    if (!pBusDevices)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failDeviceNotOnBus");

    // Deferred: the result completes the response (from loop())
    RestAPIEndpointManager* pRestAPIEndpointManager = getRestAPIEndpointManager();
    if (sourceInfo.canDefer && pRestAPIEndpointManager &&
                (pRestAPIEndpointManager->deferResponse(reqStr.c_str(), sourceInfo, REINIT_TIMEOUT_MS) == RAFT_RESP_DEFERRED))
    {
        std::shared_ptr<PendingReInit> pPending = std::make_shared<PendingReInit>();
        pPending->deferToken = sourceInfo.deferToken;
        pPending->startMs = millis();
        pPending->reqStr = reqStr;
        if (!pBusDevices->requestReInit(deviceID.getAddress(), [pPending](bool ok) { pPending->result = ok ? 1 : 0; }))
        {
            pRestAPIEndpointManager->getDeferredResponses().cancel(sourceInfo.deferToken);
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failDeviceNotOnline");
        }
        _pendingReInits.push_back(pPending);
        return RAFT_RESP_DEFERRED;
    }

    // Not deferred
    if (!pBusDevices->requestReInit(deviceID.getAddress(), nullptr))
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failDeviceNotOnline");
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, "\"queued\":true");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Complete the deferred responses of re-initialisations that have finished (or given up on)
void DeviceManager::servicePendingReInits()
{
    RestAPIEndpointManager* pRestAPIEndpointManager = getRestAPIEndpointManager();
    for (auto it = _pendingReInits.begin(); it != _pendingReInits.end();)
    {
        int result = (*it)->result.load();
        bool timedOut = Raft::isTimeout(millis(), (*it)->startMs, REINIT_TIMEOUT_MS);
        if ((result < 0) && !timedOut)
        {
            ++it;
            continue;
        }
        if (pRestAPIEndpointManager)
        {
            String deferredRespStr;
            if (result > 0)
                Raft::setJsonBoolResult((*it)->reqStr.c_str(), deferredRespStr, true);
            else
                Raft::setJsonErrorResult((*it)->reqStr.c_str(), deferredRespStr, result == 0 ? "failInit" : "failTimeout");
            pRestAPIEndpointManager->completeDeferred((*it)->deferToken, deferredRespStr);
        }
        it = _pendingReInits.erase(it);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle devman/devenable?deviceid=<id>&poll=<0|1>&save=<0|1> - stop or resume polling a device. The
///        setting is kept while the device is offline (and applied when it is identified again); save=1 keeps it
///        over a restart
RaftRetCode DeviceManager::apiDevManDevEnable(const String &reqStr, String &respStr, const RaftJson& jsonParams)
{
    RaftDeviceID deviceID;
    RaftBus* pBus = nullptr;
    String errorStr;
    if (resolveDeviceIDAndBus(jsonParams, deviceID, pBus, errorStr) != RAFT_OK)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, errorStr.c_str());
    RaftBusDevicesIF* pBusDevices = pBus ? pBus->getBusDevicesIF() : nullptr;
    if (!pBusDevices)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failDeviceNotOnBus");
    uint64_t key = DeviceNames::packDeviceIDKey(deviceID);

    // Query only
    String pollStr = jsonParams.getString("poll", "");
    if (pollStr.length() == 0)
    {
        bool pollStopped = _pollingStopped.find(key) != _pollingStopped.end();
        String extra = "\"deviceID\":\"" + deviceID.toString() + "\",\"poll\":" + (pollStopped ? "false" : "true");
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, extra.c_str());
    }

    // Set
    bool pollEnabled = jsonParams.getBool("poll", true);
    if (pollEnabled)
        _pollingStopped.erase(key);
    else
        _pollingStopped[key] = true;
    bool appliedNow = pBusDevices->setDevicePollingEnabled(deviceID.getAddress(), pollEnabled);
    String extra = "\"deviceID\":\"" + deviceID.toString() + "\",\"poll\":" + (pollEnabled ? "true" : "false") +
                ",\"online\":" + (appliedNow ? "true" : "false");
    if (jsonParams.getBool("save", false))
    {
        _settingsStore.getOrAdd(deviceID.toString()).pollEnabled = pollEnabled ? -1 : 0;
        extra += String(",\"saved\":") + (saveDeviceSettings() ? "true" : "false");
    }
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, extra.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle devman/reserved - the addresses a device type record identifies a device at (re-addressable
///        devices keep off them)
RaftRetCode DeviceManager::apiDevManReserved(const String &reqStr, String &respStr, const RaftJson& jsonParams)
{
    String json = "\"addrs\":[";
    bool first = true;
    for (uint32_t addr = 0; addr <= 0x7f; addr++)
    {
        if (!deviceTypeRecords.isAddrReserved(addr))
            continue;
        char addrStr[8];
        snprintf(addrStr, sizeof(addrStr), "\"0x%02x\"", (unsigned)addr);
        if (!first)
            json += ",";
        first = false;
        json += addrStr;
    }
    json += "]";
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, json.c_str());
}
