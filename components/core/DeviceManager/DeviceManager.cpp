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

// Warnings
#define WARN_ON_DEVICE_CLASS_NOT_FOUND
#define WARN_ON_DEVICE_INSTANTIATION_FAILED
#define WARN_ON_SETUP_DEVICE_FAILED

// Debug
#define DEBUG_BUS_OPERATION_STATUS_OK_CB
#define DEBUG_BUS_ELEMENT_STATUS_CHANGES
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
#define DEBUG_LOOP_SHOW_DEVICES_INTERVAL_MS 1000
#define DEBUG_DEVICE_CONFIG_API

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
    registerForDeviceDataChangeCBs(RaftDeviceID::BUS_NUM_ALL_DEVICES_ANY_BUS);

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
std::vector<uint8_t> DeviceManager::getDevicesDataBinary(uint16_t topicIndex) const
{
    std::vector<uint8_t> binaryData;
    binaryData.reserve(502);

    // Envelope header: magic+version byte (0xDB = devbin v1) followed by topic index
    binaryData.push_back(0xDB);
    binaryData.push_back(topicIndex <= 0xFE ? (uint8_t)topicIndex : 0xFF);

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
double DeviceManager::getNamedValue(const char* pValueName, bool& isValid)
{
    // TODO - this only supports static devices ...

    if (!pValueName)
        return 0.0;
    // Parse valueName as "deviceName.paramName"
    String valueNameStr(pValueName);
    int dotPos = valueNameStr.indexOf('.');
    if (dotPos > 0)
    {
        String deviceName = valueNameStr.substring(0, dotPos);
        String paramName = valueNameStr.substring(dotPos + 1);
        RaftDevice* pDevice = getDeviceByStringLookup(deviceName.c_str());
        if (pDevice) 
        {
            double val = pDevice->getNamedValue(paramName.c_str(), isValid);
#ifdef DEBUG_SYSMOD_GET_NAMED_VALUE
            LOG_I("DeviceManager", "getNamedValue: device=%s param=%s result: %f (valid=%d)", 
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
bool DeviceManager::setNamedValue(const char* pValueName, double value)
{
    // TODO - this only supports static devices ...

    if (!pValueName)
         return false;
    // Parse valueName as "deviceName.paramName"
    String valueNameStr(pValueName);
    int dotPos = valueNameStr.indexOf('.');
    if (dotPos > 0)
    {
        String deviceName = valueNameStr.substring(0, dotPos);
        String paramName = valueNameStr.substring(dotPos + 1);
        RaftDevice* pDevice = getDeviceByStringLookup(deviceName.c_str());
        if (pDevice)
        {
            pDevice->setNamedValue(paramName.c_str(), value);
            return false;
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
        RaftDevice* pDevice = getDeviceByStringLookup(deviceName.c_str());
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
bool DeviceManager::setNamedString(const char* pValueName, const char* value)
{
    // TODO - this only supports static devices ...
    if(!pValueName || !value)
        return false;

    // Parse valueName as "deviceName.paramName"
    String valueNameStr(pValueName);
    int dotPos = valueNameStr.indexOf('.');
    if (dotPos > 0)
    {
        String deviceName = valueNameStr.substring(0, dotPos);
        String paramName = valueNameStr.substring(dotPos + 1);
        RaftDevice* pDevice = getDeviceByStringLookup(deviceName.c_str());
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
/// @return RaftRetCode
RaftRetCode DeviceManager::receiveCmdJSON(const char* cmdJSON)
{
    // TODO - maybe need to handle bus devices in here?

    // Parse cmdJSON to extract device name
    RaftJson json(cmdJSON);
    String deviceName = json.getString("device", "");
#ifdef DEBUG_SYSMOD_RECV_CMD_JSON
    LOG_I("DeviceManager", "[DEBUG] receiveCmdJSON: device=%s, json=%s", deviceName.c_str(), cmdJSON);
#endif
    if (deviceName.length() > 0)
    {
        RaftDevice* pDevice = getDeviceByStringLookup(deviceName.c_str());
        if (pDevice) {
#ifdef DEBUG_SYSMOD_RECV_CMD_JSON
            RaftRetCode ret = pDevice->sendCmdJSON(cmdJSON);
            LOG_I("DeviceManager", "[DEBUG] sendCmdJSON result: %d", ret);
            return ret;
#else
            return pDevice->sendCmdJSON(cmdJSON);
#endif
        }
#ifdef DEBUG_SYSMOD_RECV_CMD_JSON
        LOG_W("DeviceManager", "[DEBUG] receiveCmdJSON failed: device not found (%s)", deviceName.c_str());
#endif
        return RAFT_INVALID_OBJECT;
    }
#ifdef DEBUG_SYSMOD_RECV_CMD_JSON
    LOG_W("DeviceManager", "[DEBUG] receiveCmdJSON failed: no device specified");
#endif
    // No device specified, not handled
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
                            " devman/devconfig?deviceid=<deviceId>&intervalUs=<microseconds>&numSamples=<count> - device configuration,"
                            " devman/busname?busnum=<busNumber> - Get bus name from bus number,"
                            " devman/demo?type=<deviceType>&rate=<sampleRateMs>&duration=<durationMs>&offlineIntvS=<N>&offlineDurS=<M> - Start demo device"
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
RaftRetCode DeviceManager::resolveDeviceIDAndBus(const String& reqStr, String& respStr, const RaftJson& jsonParams,
                                                  RaftDeviceID& deviceID, RaftBus*& pBus)
{
    String deviceIdStr = jsonParams.getString("deviceid", "");
    if (deviceIdStr.length() != 0)
    {
        deviceID = RaftDeviceID::fromString(deviceIdStr.c_str());
    }
    else
    {
        String addrStr = jsonParams.getString("addr", "");
        if (addrStr.length() == 0)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failAddrMissing");

        String busName = jsonParams.getString("bus", "");
        if (busName.length() == 0)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusMissing");

        RaftBus* pBusForName = raftBusSystem.getBusByName(busName, true);
        if (!pBusForName)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusNotFound");

        deviceID = RaftDeviceID(pBusForName->getBusNum(), RaftDeviceID::fromString(addrStr.c_str()).getAddress());
    }

    if (!deviceID.isValid())
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failInvalidDeviceID");

    pBus = raftBusSystem.getBusByNumber(deviceID.getBusNum());
    if (!pBus)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusNotFound");

    return RAFT_OK;
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
    RaftRetCode retc = resolveDeviceIDAndBus(reqStr, respStr, jsonParams, deviceID, pBus);
    if (retc != RAFT_OK)
        return retc;

    // Data to write and number of bytes to read
    String hexWriteData = jsonParams.getString("hexWr", "");
    int numBytesToRead = jsonParams.getLong("numToRd", 0);


    uint32_t numBytesToWrite = hexWriteData.length() / 2;
    std::vector<uint8_t> writeVec;
    writeVec.resize(numBytesToWrite);
    uint32_t writeBytesLen = Raft::getBytesFromHexStr(hexWriteData.c_str(), writeVec.data(), numBytesToWrite);
    writeVec.resize(writeBytesLen);

    static const uint32_t CMDID_CMDRAW = 100;
    HWElemReq hwElemReq = {writeVec, numBytesToRead, CMDID_CMDRAW, "cmdraw", 0};

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
///        "intervalUs" (polling interval in microseconds)
/// @return RaftRetCode indicating success or failure of the operation
RaftRetCode DeviceManager::apiDevManDevConfig(const String &reqStr, String &respStr, const RaftJson& jsonParams)
{
    // Handle non-bus devices in here?

    // Resolve deviceID and bus from params
    RaftDeviceID deviceID;
    RaftBus* pBus = nullptr;
    RaftRetCode retc = resolveDeviceIDAndBus(reqStr, respStr, jsonParams, deviceID, pBus);
    if (retc != RAFT_OK)
        return retc;
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

    // Read back
    uint64_t pollIntervalUs = pBus->getDevicePollIntervalUs(addr);
    uint32_t numSamplesResult = pBus->getDeviceNumSamples(addr);
    if (pollIntervalUs == 0 && numSamplesResult == 0)
        return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failUnsupportedBus");

#ifdef DEBUG_DEVICE_CONFIG_API
        LOG_I(MODULE_PREFIX, "devconfig applied deviceID %s intervalUs %llu (rateHz %.3f) numSamples %u",
                deviceID.toString().c_str(),
                (unsigned long long)pollIntervalUs,
                pollIntervalUs == 0 ? 0 : 1000000.0 / pollIntervalUs,
                (unsigned)numSamplesResult);
#endif

    String extra = "\"deviceID\":\"" + deviceID.toString() + "\",\"pollIntervalUs\":" + String(pollIntervalUs) +
                   ",\"numSamples\":" + String(numSamplesResult);
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
/// @brief Callback for command result reports
/// @param reqResult Result of the bus request
void DeviceManager::cmdResultReportCallback(BusRequestResult &reqResult)
{
#ifdef DEBUG_CMD_RESULT
    LOG_I(MODULE_PREFIX, "cmdResultReportCallback len %d", reqResult.getReadDataLen());
    Raft::logHexBuf(reqResult.getReadData(), reqResult.getReadDataLen(), MODULE_PREFIX, "cmdResultReportCallback");
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Register for device data notifications (note that callbacks may occur on different threads)
/// @param deviceID Device identifier
/// @param dataChangeCB Callback for data change
/// @param minTimeBetweenReportsMs Minimum time between reports (ms)
/// @param pCallbackInfo Callback info (passed to the callback)
void DeviceManager::registerForDeviceData(RaftDeviceID deviceID, RaftDeviceDataChangeCB dataChangeCB, 
        uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo)
{
    // Add to requests for device data changes
    _deviceDataChangeCBList.push_back(DeviceDataChangeRec(deviceID, dataChangeCB, minTimeBetweenReportsMs, pCallbackInfo));

    // Debug
    bool found = false;
    for (auto& rec : _deviceDataChangeCBList)
    {
        if (rec.deviceID == deviceID)
        {
            found = true;
            break;
        }
    }
    LOG_I(MODULE_PREFIX, "registerForDeviceData %s %s minTime %dms", 
        deviceID.toString().c_str(), found ? "OK" : "DEVICE_NOT_PRESENT", minTimeBetweenReportsMs);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Register for device status changes
/// @param statusChangeCB Callback for status change
void DeviceManager::registerForDeviceStatusChange(RaftDeviceStatusChangeCB statusChangeCB)
{
    // Add to requests for device status changes
    _deviceStatusChangeCBList.push_back(statusChangeCB);
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
/// @return pointer to device if found, nullptr otherwise
RaftDevice* DeviceManager::getDeviceByStringLookup(const String& deviceStr) const
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
    std::vector<RaftDeviceStatusChangeCB> statusChangeCallbacks(_deviceStatusChangeCBList.begin(), _deviceStatusChangeCBList.end());
    RaftMutex_unlock(_accessMutex);

    // Call the device status change callbacks
    for (RaftDeviceStatusChangeCB statusChangeCB : statusChangeCallbacks)
    {
        statusChangeCB(*pDevice, addrAndStatus);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Register for device data change callbacks
/// @param deviceID ID of device (isAnyDevice() true for all devices)
/// @return number of devices registered for data change callbacks
uint32_t DeviceManager::registerForDeviceDataChangeCBs(RaftDeviceID deviceID)
{
    // Get mutex
    if (!RaftMutex_lock(_accessMutex, 5))
        return 0;

    // Create a vector of devices for the device data change callbacks
    std::vector<DeviceDataChangeRecTmp> deviceListForDataChangeCB;
    for (auto& rec : _deviceDataChangeCBList)
    {
        // Check if the device name matches (if specified)
        if (!deviceID.isAnyDevice() && (rec.deviceID != deviceID))
            continue;
            
        // Find device
        RaftDevice* pDevice = nullptr;
        for (auto& devRec : _staticDeviceList)
        {
            if (!devRec.pDevice)
                continue;
            if (rec.deviceID != devRec.pDevice->getDeviceID())
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

    // Check for any device data change callbacks
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
