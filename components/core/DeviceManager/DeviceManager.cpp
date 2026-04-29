////////////////////////////////////////////////////////////////////////////////
//
// DeviceManager.cpp
//
////////////////////////////////////////////////////////////////////////////////

#include <functional>
#include <algorithm>
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
    : RaftSysMod(pModuleName, sysConfig)
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
    // Setup buses
    raftBusSystem.setup("Buses", modConfig(),
            std::bind(&DeviceManager::busElemStatusCB, this, std::placeholders::_1, std::placeholders::_2),
            std::bind(&DeviceManager::busOperationStatusCB, this, std::placeholders::_1, std::placeholders::_2)
    );

    // Setup device classes (these are the keys into the device factory)
    setupStaticDevices("Devices", modConfig());

    // Load device names from config
    loadDeviceNames(modConfig());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Post setup
/// @note This handles post-setup for statically added devices (dynamic devices are handled separately)
void DeviceManager::postSetup()
{
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

        // Get the devices interface for the bus and if it exists register for device data updates for the deviceID of the bus element with the status change
        RaftBusDevicesIF* pBusDevicesIF = bus.getBusDevicesIF();
        if (!pBusDevicesIF)
            continue;

        // Iterate the registered device data change callbacks to see if any match the deviceID or deviceTypeIndex of the bus element with the status change
#ifdef DEBUG_BUS_ELEMENT_STATUS_CHANGES
        bool recordFound = false;
#endif
        for (const DeviceDataChangeRec& rec : _requestedDeviceDataChangeCBList)
        {
            // Check if the record matches the deviceID
            bool registerForData = (rec.recType == DeviceDataChangeRec::DataChangeRecType::DEVICE_ID) && 
                        (rec.deviceID.getBusNum() == bus.getBusNum()) && 
                        (rec.deviceID.getAddress() == addrStatus.address);
            if (registerForData)
            {
                // Debug
#ifdef DEBUG_REGISTER_FOR_DEVICE_DATA
                LOG_I(MODULE_PREFIX, "busElemStatusCB MATCH reg deviceID %s against busElem deviceID %s deviceTypeIndex %u", 
                    rec.deviceID.toString().c_str(),
                    RaftDeviceID(bus.getBusNum(), addrStatus.address).toString().c_str(), addrStatus.deviceTypeIndex);
                recordFound = true;
#endif
                pBusDevicesIF->registerForDeviceData(rec.deviceID.getAddress(), rec.dataChangeCB, rec.minTimeBetweenReportsMs, rec.pCallbackInfo);
                break;
            }

            // Check if the device is newly identified and if the record matches the deviceTypeIndex
            bool registerForDataByType = (rec.recType == DeviceDataChangeRec::DataChangeRecType::DEVICE_TYPE_INDEX) && 
                        (rec.deviceTypeIndex == addrStatus.deviceTypeIndex) && 
                        (addrStatus.deviceTypeIndex != DEVICE_TYPE_INDEX_INVALID);

            if (registerForDataByType)
            {
#ifdef DEBUG_REGISTER_FOR_DEVICE_DATA
                LOG_I(MODULE_PREFIX, "busElemStatusCB MATCH reg deviceTypeIndex %u against busElem deviceTypeIndex %u", 
                        rec.deviceTypeIndex, addrStatus.deviceTypeIndex);
                recordFound = true;
#endif
                pBusDevicesIF->registerForDeviceData(addrStatus.address, rec.dataChangeCB, rec.minTimeBetweenReportsMs, rec.pCallbackInfo);
                break;
            }

#ifdef DEBUG_REGISTER_FOR_DEVICE_DATA
            String regTypeInfoStr = (rec.recType == DeviceDataChangeRec::DataChangeRecType::DEVICE_ID ? String("DEVICE_ID") + " " + rec.deviceID.toString() : String("DEVICE_TYPE_INDEX") + " " + String(rec.deviceTypeIndex));
            String busElemTypeInfoStr = String("DEVICE_ID") + " " + RaftDeviceID(bus.getBusNum(), addrStatus.address).toString() + " DEVICE_TYPE_INDEX " + String(addrStatus.deviceTypeIndex);
            LOG_I(MODULE_PREFIX, "busElemStatusCB NO MATCH reg %s busElem %s", regTypeInfoStr.c_str(), busElemTypeInfoStr.c_str());
#endif
        }

        // Debug
#ifdef DEBUG_BUS_ELEMENT_STATUS_CHANGES
        LOG_I(MODULE_PREFIX, "busElemStatusCB bus %s address %x onlineState %s isNewlyIdentified %s deviceTypeIndex %u numDataChangeCBReqs %d dataChangeCBRequested %s", 
                    bus.getBusName().c_str(), addrStatus.address,
                    BusAddrStatus::getOnlineStateStr(addrStatus.onlineState), 
                    addrStatus.isNewlyIdentified ? "Y" : "N", 
                    addrStatus.deviceTypeIndex, 
                    _requestedDeviceDataChangeCBList.size(),
                    recordFound ? "Y" : "N");
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
/// @brief Setup static devices that are defined in the SysType configuration
/// @param pConfigPrefix prefix for the device configuration
/// @param devManConfig configuration for the device manager
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

        // Get class of device
        String devClass = devConf.getString("class", "");

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
        auto pDevice = pDeviceClassDef->pCreateFn(devClass.c_str(), devConf.c_str());
        if (!pDevice)
        {
#ifdef WARN_ON_DEVICE_INSTANTIATION_FAILED
            LOG_E(MODULE_PREFIX, "setupStaticDevices %s class %s create failed devConf %s", 
                        pConfigPrefix, devClass.c_str(), devConf.c_str());
#endif
            continue;
        }

        // Set deviceID and add to the list of instantiated devices (static creation from config)
        pDevice->setDeviceID(RaftDeviceID(RaftDeviceID::BUS_NUM_DIRECT_CONN, _staticDeviceList.size()));
        _staticDeviceList.push_back({pDevice, true});

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
        {
#ifdef DEBUG_DEVICE_SETUP            
            LOG_I(MODULE_PREFIX, "setup pDevice %p name %s", devRec.pDevice, devRec.pDevice->getDeviceName());
#endif
            // Setup device
            devRec.pDevice->setup();

            // See if the device has a device type record
            DeviceTypeRecordDynamic devTypeRec;
            if (devRec.pDevice->getDeviceTypeRecord(devTypeRec))
            {
                // Add the device type record to the device type records
                uint16_t deviceTypeIndex = 0;
                deviceTypeRecords.addExtendedDeviceTypeRecord(devTypeRec, deviceTypeIndex);
                devRec.pDevice->setDeviceTypeIndex(deviceTypeIndex);
            }
        }
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
                devRec.pDevice ? devRec.pDevice->getDeviceName() : "UNKNOWN");
            
    }
    if (_staticDeviceList.size() == 0)
        LOG_I(MODULE_PREFIX, "No devices found");
#endif
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
                pDevice->getDeviceName().c_str(), Raft::getHexStr(deviceBinaryData.data(), deviceBinaryData.size()).c_str());
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
                pDevice->getDeviceName().c_str(), deviceStateHash, stateHash[0], stateHash[1]);
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
        auto it = _deviceNameToID.find(std::string(deviceName.c_str()));
        if (it != _deviceNameToID.end())
        {
            double val = getBusDeviceNamedValue(it->second, paramName, isValid);
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
        auto it = _deviceNameToID.find(std::string(deviceName.c_str()));
        if (it != _deviceNameToID.end())
        {
            deviceID = it->second;
        }
        else
        {
            deviceID = RaftDeviceID::fromString(deviceName);
        }

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
                            " devman/typeinfo?type=<typeName> - Get type info,"
                            " devman/cmdraw?deviceid=<deviceId>&hexWr=<hexWriteData>&numToRd=<numBytesToRead>&msgKey=<msgKey> - Send raw command to device,"
                            " devman/cmdjson?body=<jsonCommand> - Send JSON command to device (requires 'device' field in JSON),"
                            " devman/devconfig?deviceid=<deviceId>&intervalUs=<microseconds>&numSamples=<count>&sampleRateHz=<hz>&busHz=<hz>&busHzSlots=<csv> - device configuration,"
                            " devman/busname?busnum=<busNumber> - Get bus name from bus number,"
                            " devman/demo?type=<deviceType>&rate=<sampleRateMs>&duration=<durationMs>&offlineIntvS=<N>&offlineDurS=<M> - Start demo device,"
                            " devman/setname?deviceid=<deviceId>&name=<friendlyName> - Assign friendly name to a bus device"
                            " devman/slot?bus=<busNameOrNum>&slot=<n>&mode=<i2c|serial-full|serial-half> - Set slot mode (omit mode to query),"
                            " Note: typeName can be either a device type name or a device type index"
                            " Note: deviceId=<deviceId> can be replaced with bus=<busNameOrNumber>&addr=<addr>");
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
        return apiDevManCmdRaw(reqStr, respStr, jsonParams);
    if (cmdName.equalsIgnoreCase("cmdjson"))
        return apiDevManCmdJson(reqStr, respStr, jsonParams);
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
    if (cmdName.equalsIgnoreCase("slot"))
        return apiDevManSlot(reqStr, respStr, jsonParams);

    return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failUnknownCmd");
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get type information for a device
/// @param reqStr request string containing the command and parameters
/// @param respStr (out) response string to be filled with the result
/// @param jsonParams JSON object containing the parameters for the command, expected to have a "type" field which can be either a device type name or a device type index
/// @return RaftRetCode indicating success or failure of the operation, with the response containing the device type information in JSON format if successful
RaftRetCode DeviceManager::apiDevManTypeInfo(const String &reqStr, String &respStr, const RaftJson& jsonParams)
{
    String typeName = jsonParams.getString("type", "");
    if (typeName.length() == 0)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failTypeMissing");

    String devInfo;
    DeviceTypeIndexType deviceTypeIndex = 0;

    if ((typeName.length() > 0) && isdigit(typeName[0]))
    {
        deviceTypeIndex = (DeviceTypeIndexType)typeName.toInt();
        devInfo = deviceTypeRecords.getDevTypeInfoJsonByTypeIdx(deviceTypeIndex, false);
    }
    if (devInfo.length() == 0)
        devInfo = deviceTypeRecords.getDevTypeInfoJsonByTypeName(typeName, false, deviceTypeIndex);

    if ((devInfo.length() == 0) || (devInfo == "{}"))
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failTypeNotFound");

    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true,
                ("\"devinfo\":" + devInfo + ",\"dtIdx\":" + String(deviceTypeIndex)).c_str());
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
/// @return RaftRetCode indicating success or failure of the operation, with the response containing the result of the command if successful
RaftRetCode DeviceManager::apiDevManCmdRaw(const String &reqStr, String &respStr, const RaftJson& jsonParams)
{
    // Maybe need to handle non-bus devices in here?


    // Resolve deviceID and bus from params
    RaftDeviceID deviceID;
    RaftBus* pBus = nullptr;
    String errorStr;
    RaftRetCode retc = resolveDeviceIDAndBus(jsonParams, deviceID, pBus, errorStr);
    if (retc != RAFT_OK)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, errorStr.c_str());

    // Data to write and number of bytes to read
    String hexWriteData = jsonParams.getString("hexWr", "");
    int numBytesToRead = jsonParams.getLong("numToRd", 0);

    // Convert hex string to byte vector
    uint32_t numBytesToWrite = hexWriteData.length() / 2;
    std::vector<uint8_t> writeVec;
    writeVec.resize(numBytesToWrite);
    uint32_t writeBytesLen = Raft::getBytesFromHexStr(hexWriteData.c_str(), writeVec.data(), numBytesToWrite);
    writeVec.resize(writeBytesLen);

    // Create hardware element request
    static const uint32_t CMDID_CMDRAW = 100;
    HWElemReq hwElemReq = {writeVec, numBytesToRead, CMDID_CMDRAW, "cmdraw", 0};

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

    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, rslt);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief REST API endpoint for sending JSON command to device
/// @param reqStr request string containing the command and parameters
/// @param respStr (out) response string to be filled with the result
/// @param jsonParams JSON object containing the parameters for the command, expected to have a "body" field with the JSON command
/// @return RaftRetCode indicating success or failure of the operation, with the response containing the result of the command if successful
/// @note the body is sent to the device as-is and is expected to contain a "device" field to route the command to the correct device. 
///       The response is a boolean indicating whether the command was successfully sent to the device, and any result from the command execution 
///       would be expected to be obtained via other means (e.g. device data updates, separate API calls, etc.)
RaftRetCode DeviceManager::apiDevManCmdJson(const String &reqStr, String &respStr, const RaftJson& jsonParams)
{
    String cmdJSON = jsonParams.getString("body", "");
    if (cmdJSON.length() == 0)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failMissingBody");

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
    String deviceType = jsonParams.getString("type", "");
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

    RaftDevice* pExistingDevice = getDeviceByID(deviceType.c_str());
    if (pExistingDevice)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failDemoDeviceExists");

    RaftJson jsonConfig = "{\"name\":\"DemoDevice\",\"type\":\"" + deviceType +
                                "\",\"sampleRateMs\":" + String(sampleRateMs) +
                                ",\"durationMs\":" + String(durationMs) +
                                ",\"offlineIntvS\":" + String(offlineIntvS) +
                                ",\"offlineDurS\":" + String(offlineDurS) + "}";
    setupDevice(deviceType.c_str(), jsonConfig);

    String resultStr = "\"demoStarted\":true,\"type\":\"" + deviceType +
                        "\",\"rate\":" + String(sampleRateMs) +
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
        bool anyWriteSent = false;
        for (int i = 0; i < actionsArrayLen; i++)
        {
            String idxStr = String(i);
            String actionName = devInfoJsonDoc.getString(("actions[" + idxStr + "]/n").c_str(), "");
            if (!actionName.startsWith("_conf."))
                continue;
            anyConfActionFound = true;

            // Look up sampleRateHz in this action's map
            // Map entries can be objects {"w":"hex", "i":intervalUs, "s":numSamples} or plain strings "hex"
            String mapPath = "actions[" + idxStr + "]/map/" + sampleRateHzStr;

            // Try object format first: map/<rate>/w
            String mappedHexValue = devInfoJsonDoc.getString((mapPath + "/w").c_str(), "");
            if (mappedHexValue.length() == 0)
            {
                // Fall back to plain string format: map/<rate>
                mappedHexValue = devInfoJsonDoc.getString(mapPath.c_str(), "");
            }
            if (mappedHexValue.length() == 0)
                continue;

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
        if (!anyWriteSent)
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
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, extra.c_str());
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
    // Add to requests for device data changes
    if (unregister)
    {
        // Remove matching record from the list
        _requestedDeviceDataChangeCBList.remove_if([&](const DeviceDataChangeRec& rec) {
            return rec.matches(deviceID, dataChangeCB, pCallbackInfo);
        });
        
#ifdef DEBUG_REGISTER_FOR_DEVICE_DATA
        LOG_I(MODULE_PREFIX, "registerForDeviceData unregister deviceID %s cb callbackInfo %p", 
                deviceID.toString().c_str(), pCallbackInfo);
#endif
        return;
    }

#ifdef DEBUG_REGISTER_FOR_DEVICE_DATA
    LOG_I(MODULE_PREFIX, "registerForDeviceData register deviceID %s callbackInfo %p minTimeBetweenReportsMs %u", 
            deviceID.toString().c_str(), pCallbackInfo, minTimeBetweenReportsMs);
#endif

    _requestedDeviceDataChangeCBList.push_back(DeviceDataChangeRec(deviceID, dataChangeCB, minTimeBetweenReportsMs, pCallbackInfo));
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
        // Remove matching record from the list
        _requestedDeviceDataChangeCBList.remove_if([&](const DeviceDataChangeRec& rec) {
            return rec.matches(deviceTypeIndex, dataChangeCB, pCallbackInfo);
        });
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
    
    // Add to requests for device data changes
    _requestedDeviceDataChangeCBList.push_back(DeviceDataChangeRec(deviceTypeIndex, dataChangeCB, minTimeBetweenReportsMs, pCallbackInfo));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Register for device data notifications (note that callbacks may occur on different threads)
/// @param deviceTypeName Device type name
/// @param dataChangeCB Callback for data change
/// @param minTimeBetweenReportsMs Minimum time between reports (ms)
/// @param pCallbackInfo Optional callback info (passed to the callback)
/// @param unregister true to unregister the callback instead of registering
void DeviceManager::registerForDeviceData(const char* deviceTypeName, RaftDeviceDataChangeCB dataChangeCB,
        uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo, bool unregister)
{
    // Get device type index for the device type name
    DeviceTypeRecord devTypeRec;
    DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
    deviceTypeRecords.getDeviceInfo(String(deviceTypeName), devTypeRec, deviceTypeIndex);
    if (deviceTypeIndex == DEVICE_TYPE_INDEX_INVALID)
    {
        LOG_W(MODULE_PREFIX, "registerForDeviceData failed: invalid device type name %s", deviceTypeName);
        return;
    }

#ifdef DEBUG_REGISTER_FOR_DEVICE_DATA
    LOG_I(MODULE_PREFIX, "registerForDeviceData deviceTypeName %s deviceTypeIndex %u", deviceTypeName, deviceTypeIndex);
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
    _requestedDeviceStatusChangeCBList.push_back(statusChangeCB);
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
    if (!RaftMutex_lock(_accessMutex, 5))
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
    if (!RaftMutex_lock(_accessMutex, 5))
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
    if (!RaftMutex_lock(_accessMutex, 5))
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
    auto it = _deviceNameToID.find(std::string(deviceStr.c_str()));
    if (it != _deviceNameToID.end())
    {
        // Found the name — try to find the device by the mapped ID
        RaftDevice* pDevice = getDevice(it->second);
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
    if (!RaftMutex_lock(_accessMutex, 5))
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
    // Get mutex
    if (!RaftMutex_lock(_accessMutex, RAFT_MUTEX_WAIT_FOREVER))
        return 0;

    // Create a vector of devices for the device data change callbacks
    std::vector<DeviceDataChangeRecTmp> deviceListForDataChangeCB;
    for (auto& rec : _requestedDeviceDataChangeCBList)
    {
        // Check if the record matches a deviceID or deviceTypeIndex in the static device list
        RaftDevice* pDevice = nullptr;
        for (auto& devRec : _staticDeviceList)
        {
            if (!devRec.pDevice)
                continue;
            if ((rec.recType == DeviceDataChangeRec::DataChangeRecType::DEVICE_ID) && (rec.deviceID == devRec.pDevice->getDeviceID()))
            {
                pDevice = devRec.pDevice;
                break;
            }
            else if ((rec.recType == DeviceDataChangeRec::DataChangeRecType::DEVICE_TYPE_INDEX) && (rec.deviceTypeIndex == devRec.pDevice->getDeviceTypeIndex()))
            {
                pDevice = devRec.pDevice;
                break;
            }
        }
        if (!pDevice)
            continue;
        deviceListForDataChangeCB.push_back({pDevice, rec.dataChangeCB, rec.minTimeBetweenReportsMs, rec.pCallbackInfo});
    }
    RaftMutex_unlock(_accessMutex);

    // Handle the found devices - register for device data notifications
    for (auto& cbRec : deviceListForDataChangeCB)
    {
        // Register for device data notification from the device
        cbRec.pDevice->registerForDeviceData(
            cbRec.dataChangeCB,
            cbRec.minTimeBetweenReportsMs,
            cbRec.pCallbackInfo
        );
    }
    return deviceListForDataChangeCB.size();
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
/// @brief Load device names from configuration
/// @param devManConfig Device manager configuration (expects a "DeviceNames" object mapping RaftDeviceID strings to friendly names)
void DeviceManager::loadDeviceNames(RaftJsonIF& devManConfig)
{
    // Get keys from DeviceNames config section
    std::vector<String> keys;
    if (!devManConfig.getKeys("DeviceNames", keys) || keys.empty())
        return;

    for (const auto& key : keys)
    {
        // Key is a RaftDeviceID string (e.g. "1_f8"), value is the friendly name
        String path = "DeviceNames/" + key;
        String friendlyName = devManConfig.getString(path.c_str(), "");
        if (friendlyName.length() == 0)
            continue;

        RaftDeviceID deviceID = RaftDeviceID::fromString(key);
        if (!deviceID.isValid())
        {
#ifdef DEBUG_DEVICE_NAMES
            LOG_W(MODULE_PREFIX, "loadDeviceNames: invalid deviceID key: %s", key.c_str());
#endif
            continue;
        }

        // Store in both maps
        setDeviceName(deviceID, friendlyName);
#ifdef DEBUG_DEVICE_NAMES
        LOG_I(MODULE_PREFIX, "loadDeviceNames: %s -> %s", key.c_str(), friendlyName.c_str());
#endif
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Assign a friendly name to a bus device
/// @param deviceID Device identifier
/// @param name Friendly name
void DeviceManager::setDeviceName(RaftDeviceID deviceID, const String& name)
{
    std::string nameStr(name.c_str());
    uint64_t key = packDeviceIDKey(deviceID);

    // Remove old name for this deviceID if it exists
    auto itOld = _deviceIDToName.find(key);
    if (itOld != _deviceIDToName.end())
    {
        _deviceNameToID.erase(itOld->second);
        _deviceIDToName.erase(itOld);
    }

    // Remove old deviceID for this name if it exists
    auto itOldName = _deviceNameToID.find(nameStr);
    if (itOldName != _deviceNameToID.end())
    {
        _deviceIDToName.erase(packDeviceIDKey(itOldName->second));
        _deviceNameToID.erase(itOldName);
    }

    // Store new mapping
    _deviceNameToID[nameStr] = deviceID;
    _deviceIDToName[key] = nameStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get friendly name for a device ID
/// @param deviceID Device identifier
/// @return friendly name if mapped, otherwise deviceID.toString()
String DeviceManager::getDeviceNameForID(RaftDeviceID deviceID) const
{
    uint64_t key = packDeviceIDKey(deviceID);
    auto it = _deviceIDToName.find(key);
    if (it != _deviceIDToName.end())
        return String(it->second.c_str());
    return deviceID.toString();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Resolve a device name to a RaftDeviceID using the DeviceNames map
/// @param name Friendly device name
/// @param deviceID (out) resolved device ID
/// @return true if found in the name map
bool DeviceManager::resolveDeviceNameToID(const String& name, RaftDeviceID& deviceID) const
{
    auto it = _deviceNameToID.find(std::string(name.c_str()));
    if (it != _deviceNameToID.end())
    {
        deviceID = it->second;
        return true;
    }
    return false;
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

    // Use non-destructive latest-value path to avoid draining the ring buffer
    // (which would starve the publish pipeline)
    RaftBusDeviceDecodeState decodeState;
    if (!pDevicesIF->getLatestDecodedPollResponse(addr, decodedBuf, devTypeRec.pollStructSize, decodeState))
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

    // Apply divisor and addend
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

#ifdef DEBUG_DEVICE_NAMES
    LOG_I(MODULE_PREFIX, "apiDevManSetName: %s -> %s", deviceIDStr.c_str(), name.c_str());
#endif

    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
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
#ifdef DEBUG_CMD_RESULT_CALLBACK
    LOG_I(MODULE_PREFIX, "cmdResultReportCallback len %d", reqResult.getReadDataLen());
    Raft::logHexBuf(reqResult.getReadData(), reqResult.getReadDataLen(), MODULE_PREFIX, "cmdResultReportCallback");
#endif
}
