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
#include "RaftBusDevice.h"
#include "DeviceFactory.h"
#include "SysManager.h"
#include "RestAPIEndpointManager.h"
#include "DemoDevice.h"

// Warnings
#define WARN_ON_DEVICE_CLASS_NOT_FOUND
#define WARN_ON_DEVICE_INSTANTIATION_FAILED
#define WARN_ON_SETUP_DEVICE_FAILED

// Debug
#define DEBUG_BUS_OPERATION_STATUS_OK_CB
#define DEBUG_NEW_DEVICE_FOUND_CB
// #define DEBUG_DEVICE_SETUP
// #define DEBUG_DEVICE_FACTORY
// #define DEBUG_LIST_DEVICES
// #define DEBUG_JSON_DEVICE_DATA
// #define DEBUG_BINARY_DEVICE_DATA
// #define DEBUG_JSON_DEVICE_HASH
// #define DEBUG_DEVMAN_API
// #define DEBUG_BUS_ELEMENT_STATUS
// #define DEBUG_GET_DEVICE
// #define DEBUG_JSON_DEVICE_HASH_DETAIL
// #define DEBUG_MAKE_BUS_REQUEST_VERBOSE
// #define DEBUG_API_CMDRAW
// #define DEBUG_SYSMOD_GET_NAMED_VALUE
// #define DEBUG_SYSMOD_RECV_CMD_JSON

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
/// @brief Get named value from device
/// @param pValueName Name in format "DeviceName.paramName"
/// @param isValid (out) true if value is valid
/// @return double value
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
    if (!pValueName)
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
bool DeviceManager::setNamedString(const char* pValueName, const char* value)
{
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
/// @return RaftRetCode
RaftRetCode DeviceManager::receiveCmdJSON(const char* cmdJSON)
{
    // Parse cmdJSON to extract device name
    RaftJson json(cmdJSON);
    String deviceName = json.getString("device", "");
#ifdef DEBUG_SYSMOD_RECV_CMD_JSON
    LOG_I("DeviceManager", "[DEBUG] receiveCmdJSON: device=%s, json=%s", deviceName.c_str(), cmdJSON);
#endif
    if (deviceName.length() > 0)
    {
        RaftDevice* pDevice = getDevice(deviceName.c_str());
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
/// @brief Setup function
void DeviceManager::setup()
{
    // Setup buses
    raftBusSystem.setup("Buses", modConfig(),
            std::bind(&DeviceManager::busElemStatusCB, this, std::placeholders::_1, std::placeholders::_2),
            std::bind(&DeviceManager::busOperationStatusCB, this, std::placeholders::_1, std::placeholders::_2)
    );

    // Setup device classes (these are the keys into the device factory)
    setupDevices("Devices", modConfig());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Post setup function
/// @note This handles post-setup for statically added devices (dynamic devices are handled separately)
void DeviceManager::postSetup()
{
    // Register JSON data source (message generator and state detector functions)
    getSysManager()->registerDataSource("Publish", "devjson", 
        [this](const char* messageName, CommsChannelMsg& msg) {
            String statusStr = getDevicesDataJSON();
            msg.setFromBuffer((uint8_t*)statusStr.c_str(), statusStr.length());
            return true;
        },
        [this](const char* messageName, std::vector<uint8_t>& stateHash) {
            return getDevicesHash(stateHash);
        }
    );    

    // Register binary data source (new)
    getSysManager()->registerDataSource("Publish", "devbin", 
        [this](const char* messageName, CommsChannelMsg& msg) {
            std::vector<uint8_t> binaryData = getDevicesDataBinary();
            msg.setFromBuffer(binaryData.data(), binaryData.size());
            return true;
        },
        [this](const char* messageName, std::vector<uint8_t>& stateHash) {
            return getDevicesHash(stateHash);
        }
    );

    // Get a frozen copy of the device list (null-pointers excluded)
    RaftDevice* pDeviceListCopy[DEVICE_LIST_MAX_SIZE];
    uint32_t numDevices = getDeviceListFrozen(pDeviceListCopy, DEVICE_LIST_MAX_SIZE, false);

    // Loop through the devices
    for (uint32_t devIdx = 0; devIdx < numDevices; devIdx++)
    {
        pDeviceListCopy[devIdx]->postSetup();
    }

    // Register for device data change callbacks
#ifdef DEBUG_DEVICE_SETUP
    uint32_t numDevCBsRegistered = 
#endif
    registerForDeviceDataChangeCBs();

    // Register for device events
    for (uint32_t devIdx = 0; devIdx < numDevices; devIdx++)
    {
        pDeviceListCopy[devIdx]->registerForDeviceStatusChange(
            std::bind(&DeviceManager::deviceEventCB, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
        );
    }

    // Debug
#ifdef DEBUG_DEVICE_SETUP
    LOG_I(MODULE_PREFIX, "postSetup %d devices registered %d CBs", numDevices, numDevCBsRegistered);
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Loop function
void DeviceManager::loop()
{
    // Service the buses
    raftBusSystem.loop();

    // Get a frozen copy of the device list for online devices
    RaftDevice* pDeviceListCopy[DEVICE_LIST_MAX_SIZE];
    uint32_t numDevices = getDeviceListFrozen(pDeviceListCopy, DEVICE_LIST_MAX_SIZE, true);

    // Loop through the devices
    for (uint32_t devIdx = 0; devIdx < numDevices; devIdx++)
    {
        // Handle device loop
        pDeviceListCopy[devIdx]->loop();
    }
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
void DeviceManager::busElemStatusCB(RaftBus& bus, const std::vector<BusElemAddrAndStatus>& statusChanges)
{
    // Handle the status changes
    for (const auto& el : statusChanges)
    {
        // Find the device
        String deviceId = bus.formUniqueId(el.address);
        RaftDevice* pDevice = getDeviceByID(deviceId.c_str());
        bool newlyCreated = false;
        if (!pDevice)
        {
            // Check if device newly created
            if (el.isNewlyIdentified)
            {
                // Generate config JSON for the device
                String devConfig = "{\"name\":" + deviceId + "}";

                // Create the device
                pDevice = new RaftBusDevice(bus.getBusName().c_str(), el.address, "RaftBusDevice", devConfig.c_str());
                pDevice->setDeviceTypeIndex(el.deviceTypeIndex);
                newlyCreated = true;

                // Add to the list of instantiated devices & setup
                if (RaftMutex_lock(_accessMutex, 5))
                {
                    // Add to the list of instantiated devices
                    _deviceList.push_back({pDevice, el.isChangeToOnline});
                    RaftMutex_unlock(_accessMutex);

                    // Setup device
                    pDevice->setup();
                    pDevice->postSetup();

                    // Debug
#ifdef DEBUG_NEW_DEVICE_FOUND_CB
                    LOG_I(MODULE_PREFIX, "busElemStatusCB new device %s name %s class %s pubTypeIdx %d", 
                                    deviceId.c_str(), 
                                    pDevice->getDeviceName().c_str(), 
                                    pDevice->getDeviceClassName().c_str(),
                                    pDevice->getDeviceTypeIndex());
#endif
                }
                else
                {
                    // Delete the device to avoid memory leak
                    delete pDevice;
                    pDevice = nullptr;

                    // Debug
                    LOG_E(MODULE_PREFIX, "busElemStatusCB failed to add device %s", deviceId.c_str());
                }
            }
        }

        // Handle status update
        if (pDevice)
        {
            // Handle device status change
            pDevice->handleStatusChange(el.isChangeToOnline, el.isChangeToOffline, el.isNewlyIdentified, el.deviceTypeIndex);

            // Handle device status change callbacks
            callDeviceStatusChangeCBs(pDevice, el, newlyCreated);

            // If newly created, register for device data notifications for this specific device
            if (newlyCreated)
            {
                registerForDeviceDataChangeCBs(pDevice->getDeviceName().c_str());
            }

            // Update online status in device list
            if (el.isChangeToOffline || (el.isChangeToOnline && !newlyCreated))
            {
                // Find device in list and set status
                if (RaftMutex_lock(_accessMutex, 5))
                {
                    for (auto& devPtrAndOnline : _deviceList)
                    {
                        if (devPtrAndOnline.pDevice == pDevice)
                        {
                            devPtrAndOnline.isOnline = el.isChangeToOnline;
                            break;
                        }
                    }
                    RaftMutex_unlock(_accessMutex);
                }
            }
        }
        
        // Debug
#ifdef DEBUG_BUS_ELEMENT_STATUS
        LOG_I(MODULE_PREFIX, "busElemStatusInfo ID %s %s%s%s%s",
                        deviceId.c_str(), 
                        el.isChangeToOnline ? "->Online" : ("->Offline" + String(el.isChangeToOffline ? " (was online)" : "")).c_str(),
                        el.isNewlyIdentified ? (" DevTypeIdx " + String(el.deviceTypeIndex)).c_str() : "",
                        newlyCreated ? " NewlyCreated" : "",
                        pDevice ? "" : " NOT IDENTIFIED YET");
#endif
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup devices
/// @param pConfigPrefix prefix for the device configuration
/// @param devManConfig configuration for the device manager
void DeviceManager::setupDevices(const char* pConfigPrefix, RaftJsonIF& devManConfig)
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
            LOG_W(MODULE_PREFIX, "setupDevices %s class %s not found", pConfigPrefix, devClass.c_str());
#endif
            continue;
        }

        // Create the device
        auto pDevice = pDeviceClassDef->pCreateFn(devClass.c_str(), devConf.c_str());
        if (!pDevice)
        {
#ifdef WARN_ON_DEVICE_INSTANTIATION_FAILED
            LOG_E(MODULE_PREFIX, "setupDevices %s class %s create failed devConf %s", 
                        pConfigPrefix, devClass.c_str(), devConf.c_str());
#endif
            continue;
        }

        // Add to the list of instantiated devices
        _deviceList.push_back({pDevice, true});

        // Debug
#ifdef DEBUG_DEVICE_FACTORY
        {
            LOG_I(MODULE_PREFIX, "setup class %s devConf %s", 
                        devClass.c_str(), devConf.c_str());
        }
#endif

    }

    // Now call setup on instantiated devices
    for (auto& devPtrAndOnline : _deviceList)
    {
        if (devPtrAndOnline.pDevice)
        {
#ifdef DEBUG_DEVICE_SETUP            
            LOG_I(MODULE_PREFIX, "setup pDevice %p name %s", devPtrAndOnline.pDevice, devPtrAndOnline.pDevice->getDeviceName());
#endif
            // Setup device
            devPtrAndOnline.pDevice->setup();

            // See if the device has a device type record
            DeviceTypeRecordDynamic devTypeRec;
            if (devPtrAndOnline.pDevice->getDeviceTypeRecord(devTypeRec))
            {
                // Add the device type record to the device type records
                uint16_t deviceTypeIndex = 0;
                deviceTypeRecords.addExtendedDeviceTypeRecord(devTypeRec, deviceTypeIndex);
                devPtrAndOnline.pDevice->setDeviceTypeIndex(deviceTypeIndex);
            }
        }
    }

    // Give each SysMod the opportunity to add endpoints and comms channels and to keep a
    // pointer to the CommsCoreIF that can be used to send messages
    for (auto& devPtrAndOnline : _deviceList)
    {
        if (devPtrAndOnline.pDevice)
        {
            if (getRestAPIEndpointManager())
                devPtrAndOnline.pDevice->addRestAPIEndpoints(*getRestAPIEndpointManager());
            if (getCommsCore())
                devPtrAndOnline.pDevice->addCommsChannels(*getCommsCore());
        }            
    }

#ifdef DEBUG_LIST_DEVICES
    uint32_t deviceIdx = 0;
    for (auto& devPtrAndOnline : _deviceList)
    {
        LOG_I(MODULE_PREFIX, "Device %d: %s", deviceIdx++, 
                devPtrAndOnline.pDevice ? devPtrAndOnline.pDevice->getDeviceName() : "UNKNOWN");
            
    }
    if (_deviceList.size() == 0)
        LOG_I(MODULE_PREFIX, "No devices found");
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup a single device
/// @param pDeviceClass class of the device to setup
/// @param devConfig configuration for the device
/// @return RaftDevice* pointer to the created device or nullptr if failed
RaftDevice* DeviceManager::setupDevice(const char* pDeviceClass, RaftJsonIF& devConfig)
{
    // Check valid
    if (!pDeviceClass)
    {
#ifdef WARN_ON_SETUP_DEVICE_FAILED
        LOG_E(MODULE_PREFIX, "setupDevice invalid parameters pDeviceClass %s", 
                pDeviceClass ? pDeviceClass : "NULL");
#endif
        return nullptr;
    }

    // Find the device class in the factory
    const DeviceFactory::RaftDeviceClassDef* pDeviceClassDef = deviceFactory.findDeviceClass(pDeviceClass);
    if (!pDeviceClassDef)
    {
#ifdef WARN_ON_SETUP_DEVICE_FAILED
        LOG_W(MODULE_PREFIX, "setupDevice class %s not found", pDeviceClass);
#endif
        return nullptr;
    }
    // Create the device
    auto pDevice = pDeviceClassDef->pCreateFn(pDeviceClass, devConfig.c_str());
    if (!pDevice)
    {
#ifdef WARN_ON_SETUP_DEVICE_FAILED
        LOG_E(MODULE_PREFIX, "setupDevice class %s create failed devConf %s", 
                pDeviceClass, devConfig.c_str());
#endif
        return nullptr;
    }
    // Add to the list of instantiated devices
    if (RaftMutex_lock(_accessMutex, 5))
    {
        // Add to the list of instantiated devices
        _deviceList.push_back({pDevice, true});
        RaftMutex_unlock(_accessMutex);
        // Setup device
        pDevice->setup();
        pDevice->postSetup();
#ifdef DEBUG_DEVICE_SETUP
        LOG_I(MODULE_PREFIX, "setupDevice %s devConf %s", 
                pDeviceClass, devConfig.c_str());
#endif
    }
    else
    {
#ifdef WARN_ON_SETUP_DEVICE_FAILED
        LOG_E(MODULE_PREFIX, "setupDevice failed to add device %s", pDeviceClass);
#endif
        // Delete the device to avoid memory leak
        delete pDevice;
        pDevice = nullptr;
    }

    // If the device has a device type record, add it to the device type records
    DeviceTypeRecordDynamic devTypeRec;
    if (pDevice && pDevice->getDeviceTypeRecord(devTypeRec))
    {
        // Add the device type record to the device type records
        uint16_t deviceTypeIndex = 0;
        deviceTypeRecords.addExtendedDeviceTypeRecord(devTypeRec, deviceTypeIndex);
        pDevice->setDeviceTypeIndex(deviceTypeIndex);
    }

    // If the device was successfully created, register for device data notifications
    if (pDevice)
    {
        // Register for device data notifications
        registerForDeviceDataChangeCBs(pDevice->getDeviceName().c_str());
    }

    // Debug
#ifdef DEBUG_DEVICE_SETUP
    if (pDevice)
    {
        LOG_I(MODULE_PREFIX, "setupDevice %s name %s class %s devTypeIdx %d", 
                        pDeviceClass, 
                        pDevice->getDeviceName().c_str(),
                        pDevice->getDeviceClassName().c_str(),
                        pDevice->getDeviceTypeIndex());
    }
#endif

    // Return success
    return pDevice;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get devices' data as JSON
/// @return JSON string
String DeviceManager::getDevicesDataJSON() const
{
    // Pre-allocate string capacity to avoid multiple reallocations
    // Estimate: ~200 bytes per device/bus element
    String jsonStr;
    jsonStr.reserve(1024);
    
    // Start JSON object
    jsonStr += "{";
    bool needsComma = false;

    // Check all buses for data
    uint16_t connModeBusNum = DEVICE_CONN_MODE_FIRST_BUS;
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
            snprintf(prefix, sizeof(prefix), "%s\"%d\":", 
                needsComma ? "," : "", connModeBusNum);
            jsonStr += prefix;
            jsonStr += jsonRespStr;
            needsComma = true;
        }

        // Next bus
        connModeBusNum++;
    }

    // Get a frozen copy of the device list (null-pointers excluded)
    RaftDevice* pDeviceListCopy[DEVICE_LIST_MAX_SIZE];
    uint32_t numDevices = getDeviceListFrozen(pDeviceListCopy, DEVICE_LIST_MAX_SIZE, true);

    // Loop through the devices
    for (uint32_t devIdx = 0; devIdx < numDevices; devIdx++)
    {
        RaftDevice* pDevice = pDeviceListCopy[devIdx];
        String jsonRespStr = pDevice->getStatusJSON();

        // Check for empty string or empty JSON object
        if (jsonRespStr.length() > 2)
        {
            char prefix[256];
            snprintf(prefix, sizeof(prefix), "%s\"%d\":", 
                needsComma ? "," : "", (int)pDevice->getDeviceTypeIndex());
            jsonStr += prefix;
            jsonStr += jsonRespStr;
            needsComma = true;
        }
    }

#ifdef DEBUG_JSON_DEVICE_DATA
    LOG_I(MODULE_PREFIX, "getDevicesDataJSON %s", jsonStr.c_str());
#endif

    // Close JSON object only if we added data
    if (!needsComma)
    {
        // No data available
        return "";
    }

    jsonStr += "}";
    return jsonStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get devices' data as binary
/// @return Binary data vector
std::vector<uint8_t> DeviceManager::getDevicesDataBinary() const
{
    std::vector<uint8_t> binaryData;
    binaryData.reserve(500);

    // Add bus data
    uint16_t connModeBusNum = DEVICE_CONN_MODE_FIRST_BUS;
    for (RaftBus* pBus : raftBusSystem.getBusList())
    {
        if (!pBus)
            continue;
        RaftBusDevicesIF* pDevicesIF = pBus->getBusDevicesIF();
        if (!pDevicesIF)
            continue;

        // Add the bus data
        std::vector<uint8_t> busBinaryData = pDevicesIF->getQueuedDeviceDataBinary(connModeBusNum);
        binaryData.insert(binaryData.end(), busBinaryData.begin(), busBinaryData.end());

        // Next bus
        connModeBusNum++;
    }

    // Get a frozen copy of the device list (null-pointers excluded)
    RaftDevice* pDeviceListCopy[DEVICE_LIST_MAX_SIZE];
    uint32_t numDevices = getDeviceListFrozen(pDeviceListCopy, DEVICE_LIST_MAX_SIZE, true);

    // Loop through the devices
    for (uint32_t devIdx = 0; devIdx < numDevices; devIdx++)
    {
        RaftDevice* pDevice = pDeviceListCopy[devIdx];
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
    RaftDevice* pDeviceListCopy[DEVICE_LIST_MAX_SIZE];
    uint32_t numDevices = getDeviceListFrozen(pDeviceListCopy, DEVICE_LIST_MAX_SIZE, true);

    // Loop through the devices
    for (uint32_t devIdx = 0; devIdx < numDevices; devIdx++)
    {
        // Check device status
        RaftDevice* pDevice = pDeviceListCopy[devIdx];
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
/// @brief Get a device by name
/// @param pDeviceName Name of the device
/// @return RaftDevice* Pointer to the device or nullptr if not found
RaftDevice* DeviceManager::getDevice(const char* pDeviceName) const
{
    // Obtain access to the device list
    if (!pDeviceName || !RaftMutex_lock(_accessMutex, 5))
        return nullptr;

    // Loop through the devices
    for (auto& devPtrAndOnline : _deviceList)
    {
#ifdef DEBUG_GET_DEVICE
        LOG_I(MODULE_PREFIX, "getDevice %s checking %s", pDeviceName, devPtrAndOnline.pDevice ? devPtrAndOnline.pDevice->getDeviceName() : "UNKNOWN");
#endif
        if (devPtrAndOnline.pDevice && devPtrAndOnline.pDevice->getDeviceName() == pDeviceName)
        {
            RaftMutex_unlock(_accessMutex);
            return devPtrAndOnline.pDevice;
        }
    }

    // Unlock mutex
    RaftMutex_unlock(_accessMutex);
    return nullptr;
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
    RaftDevice* pDeviceListCopy[DEVICE_LIST_MAX_SIZE];
    bool deviceOnlineArray[DEVICE_LIST_MAX_SIZE];
    uint32_t numDevices = getDeviceListFrozen(pDeviceListCopy, DEVICE_LIST_MAX_SIZE, false, deviceOnlineArray);

    // Loop through the devices
    String jsonStrDev;
    for (uint32_t devIdx = 0; devIdx < numDevices; devIdx++)
    {
        RaftDevice* pDevice = pDeviceListCopy[devIdx];
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
// Endpoints
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DeviceManager::addRestAPIEndpoints(RestAPIEndpointManager &endpointManager)
{
    // REST API endpoints
    endpointManager.addEndpoint("devman", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                            std::bind(&DeviceManager::apiDevMan, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                            " devman/typeinfo?bus=<busName>&type=<typeName> - Get type info,"
                            " devman/cmdraw?bus=<busName>&addr=<addr>&hexWr=<hexWriteData>&numToRd=<numBytesToRead>&msgKey=<msgKey> - Send raw command to device,"
                            " devman/cmdjson?body=<jsonCommand> - Send JSON command to device (requires 'device' field in JSON),"
                            " devman/setpollms?bus=<busNameOrNumber>&device=<deviceIdOrAddress>&intervalMs=<milliseconds> - Set device polling interval (bus devices only),"
                            " devman/busname?busnum=<busNumber> - Get bus name from bus number,"
                            " devman/demo?type=<deviceType>&rate=<sampleRateMs>&duration=<durationMs>&offlineIntvS=<N>&offlineDurS=<M> - Start demo device");
    LOG_I(MODULE_PREFIX, "addRestAPIEndpoints added devman");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// REST API DevMan
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode DeviceManager::apiDevMan(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    // Get device info
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    RestAPIEndpointManager::getParamsAndNameValues(reqStr.c_str(), params, nameValues);
    RaftJson jsonParams = RaftJson::getJSONFromNVPairs(nameValues, true); 

    // Get command
    String cmdName = reqStr;
    if (params.size() > 1)
        cmdName = params[1];

    // Check command
    if (cmdName.equalsIgnoreCase("typeinfo"))
    {
        // Get bus name
        String busName = jsonParams.getString("bus", "");
        if (busName.length() == 0)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusMissing");

        // Get device name
        String devTypeName = jsonParams.getString("type", "");
        if (devTypeName.length() == 0)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failTypeMissing");

        // Check if the bus name is valid and, if so, use the bus devices interface to get the device info
        String devInfo;
        RaftBus* pBus = raftBusSystem.getBusByName(busName);
        if (!pBus)
        {
            // Try to get by bus number if the busName start with a number
            if ((busName.length() > 0) && isdigit(busName[0]))
            {
                int busNum = busName.toInt();
                int busIdx = 1;
                for (auto& bus : raftBusSystem.getBusList())
                {
                    if (busIdx++ == busNum)
                    {
                        pBus = bus;
                        break;
                    }
                }
            }
        }
        uint32_t deviceTypeIndex = 0;
        if (pBus)
        {
            // Get devices interface
            RaftBusDevicesIF* pDevicesIF = pBus->getBusDevicesIF();
            if (!pDevicesIF)
                return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failTypeNotFound");

            // Check if the first digit of the device type name is a number
            if ((devTypeName.length() > 0) && isdigit(devTypeName[0]))
            {
                // Get device info by number
                deviceTypeIndex = devTypeName.toInt();
                devInfo = pDevicesIF->getDevTypeInfoJsonByTypeIdx(deviceTypeIndex, false);
            }
            if (devInfo.length() == 0)
            {
                // Get device info by name if possible
                devInfo = pDevicesIF->getDevTypeInfoJsonByTypeName(devTypeName, false, deviceTypeIndex);
            }
        }
        else
        {
            // Use the global device type info to get the device info
            if ((devTypeName.length() > 0) && isdigit(devTypeName[0]))
            {
                // Get device info by number
                deviceTypeIndex = devTypeName.toInt();
                devInfo = deviceTypeRecords.getDevTypeInfoJsonByTypeIdx(deviceTypeIndex, false);
            }
            if (devInfo.length() == 0)
            {
                // Get device info by name if possible
                devInfo = deviceTypeRecords.getDevTypeInfoJsonByTypeName(devTypeName, false, deviceTypeIndex);
            }
        }

        // Check valid
        if ((devInfo.length() == 0) || (devInfo == "{}"))
        {
#ifdef DEBUG_DEVMAN_API
            LOG_I(MODULE_PREFIX, "apiHWDevice bus %s type %s DEVICE NOT FOUND", busName.c_str(), devTypeName.c_str());
#endif
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failTypeNotFound");
        }

#ifdef DEBUG_DEVMAN_API
        LOG_I(MODULE_PREFIX, "apiHWDevice bus %s busFound %s type %s devInfo %s", 
                busName.c_str(), 
                pBus ? "Y" : "N",
                devTypeName.c_str(), 
                devInfo.c_str());
#endif

        // Set result
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, 
                    ("\"devinfo\":" + devInfo + ",\"dtIdx\":" + String(deviceTypeIndex)).c_str());
    }

    // Check for raw command
    if (cmdName.equalsIgnoreCase("cmdraw"))
    {
        // Get bus name
        String busName = jsonParams.getString("bus", "");
        if (busName.length() == 0)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusMissing");

        // Get args
        String addrStr = jsonParams.getString("addr", "");
        String hexWriteData = jsonParams.getString("hexWr", "");
        int numBytesToRead = jsonParams.getLong("numToRd", 0);
        // String msgKey = jsonParams.getString("msgKey", "");

        // Check valid
        if (addrStr.length() == 0)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failMissingAddr");

        // Find the bus
        RaftBus* pBus = raftBusSystem.getBusByName(busName);
        if (!pBus)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusNotFound");

        // Convert address
        BusElemAddrType addr = strtol(addrStr.c_str(), NULL, 16);

        // Get bytes to write
        uint32_t numBytesToWrite = hexWriteData.length() / 2;
        std::vector<uint8_t> writeVec;
        writeVec.resize(numBytesToWrite);
        uint32_t writeBytesLen = Raft::getBytesFromHexStr(hexWriteData.c_str(), writeVec.data(), numBytesToWrite);
        writeVec.resize(writeBytesLen);

        // Store the msg key for response
        // TODO store the msgKey for responses
        // _cmdResponseMsgKey = msgKey;

        // Form HWElemReq
        static const uint32_t CMDID_CMDRAW = 100;
        HWElemReq hwElemReq = {writeVec, numBytesToRead, CMDID_CMDRAW, "cmdraw", 0};

        // Form request
        BusRequestInfo busReqInfo("", addr);
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
        LOG_I(MODULE_PREFIX, "apiHWDevice addr %s len %d data %s ...", 
                        addrStr.c_str(), 
                        hwElemReq._writeData.size(),
                        outStr.c_str());
#endif

        bool rslt = pBus->addRequest(busReqInfo);
        if (!rslt)
        {
            LOG_W(MODULE_PREFIX, "apiHWDevice failed send raw command");
        }

        // Debug
#ifdef DEBUG_API_CMDRAW
        LOG_I(MODULE_PREFIX, "apiHWDevice hexWriteData %s numToRead %d", hexWriteData.c_str(), numBytesToRead);
#endif

        // Set result
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, rslt);    
    }

    // Check for JSON command routing
    if (cmdName.equalsIgnoreCase("cmdjson"))
    {
        // Get the JSON command from the body parameter
        String cmdJSON = jsonParams.getString("body", "");
        if (cmdJSON.length() == 0)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failMissingBody");

        // Call receiveCmdJSON to route the command to the appropriate device
        RaftRetCode retc = receiveCmdJSON(cmdJSON.c_str());

#ifdef DEBUG_DEVMAN_API
        LOG_I(MODULE_PREFIX, "apiDevMan cmdjson retc %d cmdJSON %s", retc, cmdJSON.c_str());
#endif

        // Return result
        if (retc == RAFT_OK)
            return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
        else if (retc == RAFT_INVALID_OBJECT)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failDeviceNotFound");
        else if (retc == RAFT_INVALID_OPERATION)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failNoDeviceSpecified");
        else
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failCmdFailed");
    }

#ifdef DEVICE_MANAGER_ENABLE_DEMO_DEVICE

    // Check for demo command
    if (cmdName.equalsIgnoreCase("demo"))
    {
        // Get demo parameters
        String deviceType = jsonParams.getString("type", "");
        if (deviceType.length() == 0)
            deviceType = "ACCDEMO";

        uint32_t sampleRateMs = jsonParams.getLong("rate", 100);
        uint32_t durationMs = jsonParams.getLong("duration", 0);
        uint32_t offlineIntvS = jsonParams.getLong("offlineIntvS", 0);
        uint32_t offlineDurS = jsonParams.getLong("offlineDurS", 10);

        // Validate parameters
        if (sampleRateMs < 10)
            sampleRateMs = 10; // Minimum 10ms
        if (sampleRateMs > 60000)
            sampleRateMs = 60000; // Maximum 60s
        if (offlineDurS < 1)
            offlineDurS = 1; // Minimum 1s offline duration

        // Check if the device already exists
        RaftDevice* pExistingDevice = getDeviceByID(deviceType.c_str());
        if (pExistingDevice)
        {
            // Return error if the device already exists
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failDemoDeviceExists");
        }

        // Setup the demo device
        RaftJson jsonConfig = "{\"name\":\"DemoDevice\",\"type\":\"" + deviceType + 
                                 "\",\"sampleRateMs\":" + String(sampleRateMs) + 
                                 ",\"durationMs\":" + String(durationMs) +
                                 ",\"offlineIntvS\":" + String(offlineIntvS) +
                                 ",\"offlineDurS\":" + String(offlineDurS) + "}";
        setupDevice(deviceType.c_str(), jsonConfig);

        // Set result
        String resultStr = "\"demoStarted\":true,\"type\":\"" + deviceType + 
                          "\",\"rate\":" + String(sampleRateMs) + 
                          ",\"duration\":" + String(durationMs) +
                          ",\"offlineIntvS\":" + String(offlineIntvS) +
                          ",\"offlineDurS\":" + String(offlineDurS);
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, resultStr.c_str());
    }
#endif

    // Check for set poll interval command
    if (cmdName.equalsIgnoreCase("setpollinterval"))
    {
        // Get bus name
        String busName = jsonParams.getString("bus", "");
        if (busName.length() == 0)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusMissing");

        // Get device name
        String deviceName = jsonParams.getString("device", "");
        if (deviceName.length() == 0)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failDeviceMissing");
        
        // Get polling interval
        uint32_t intervalMs = jsonParams.getLong("intervalMs", 0);
        if (intervalMs == 0)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failInvalidInterval");
        
        // Find bus
        RaftBus* pBus = raftBusSystem.getBusByName(busName);
        if (!pBus)
        {
            // Try to get by bus number if the busName start with a number
            if ((busName.length() > 0) && isdigit(busName[0]))
            {
                getBusNameByNumber(busName.toInt(), busName);

                pBus = raftBusSystem.getBusByName(busName);
                if (!pBus)
                    return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusNotFound");
            }
            else
            {
                return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusNotFound");
            }
        }

        // Find device
        RaftDevice* pDevice = getDevice(deviceName.c_str());
        BusElemAddrType deviceAddr = 0;
        if (!pDevice)
        {
            // Attempt to convert the device name to an address - it starts with a digit
            if ((deviceName.length() > 0) && isdigit(deviceName[0]))
            {
                deviceAddr = strtol(deviceName.c_str(), NULL, 16);
            }
            else
            {
                return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failDeviceNotFound");
            }
        }
        else
        {
            deviceAddr = pDevice->getDeviceAddress();
        }

        // Set the polling interval
        bool rslt = pBus->setDevicePollInterval(deviceAddr, intervalMs);
        
#ifdef DEBUG_DEVMAN_API
        LOG_I(MODULE_PREFIX, "apiDevMan setpollinterval device %s bus %s addr 0x%02x intervalMs %d result %s",
                deviceName.c_str(), pBusDevice->getBusName().c_str(), 
                pBusDevice->getBusAddress(), intervalMs, rslt ? "OK" : "FAIL");
#endif
        
        // Return result
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, rslt);
    }

    // Check for bus name by number command
    if (cmdName.equalsIgnoreCase("busname"))
    {
        // Get bus number
        int busNum = jsonParams.getLong("busnum", -1);
        if (busNum < DEVICE_CONN_MODE_FIRST_BUS)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failInvalidBusNum");
        
        // Get bus name
        String busName;
        if (!getBusNameByNumber(busNum, busName))
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusNotFound");
        
#ifdef DEBUG_DEVMAN_API
        LOG_I(MODULE_PREFIX, "apiDevMan busname busNum %d busName %s",
                busNum, busName.c_str());
#endif
        
        // Return result
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, 
                                       ("\"busName\":\"" + busName + "\"").c_str());
    }

    // Set result
    return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failUnknownCmd");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cmd result report callbacks
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DeviceManager::cmdResultReportCallback(BusRequestResult &reqResult)
{
#ifdef DEBUG_CMD_RESULT
    LOG_I(MODULE_PREFIX, "cmdResultReportCallback len %d", reqResult.getReadDataLen());
    Raft::logHexBuf(reqResult.getReadData(), reqResult.getReadDataLen(), MODULE_PREFIX, "cmdResultReportCallback");
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Register for device data notifications (note that callbacks may occur on different threads)
/// @param pDeviceName Name of the device
/// @param dataChangeCB Callback for data change
/// @param minTimeBetweenReportsMs Minimum time between reports (ms)
/// @param pCallbackInfo Callback info (passed to the callback)
void DeviceManager::registerForDeviceData(const char* pDeviceName, RaftDeviceDataChangeCB dataChangeCB, 
        uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo)
{
    // Add to requests for device data changes
    _deviceDataChangeCBList.push_back(DeviceDataChangeRec(pDeviceName, dataChangeCB, minTimeBetweenReportsMs, pCallbackInfo));

    // Debug
    bool found = false;
    for (auto& rec : _deviceDataChangeCBList)
    {
        if (rec.deviceName == pDeviceName)
        {
            found = true;
            break;
        }
    }
    LOG_I(MODULE_PREFIX, "registerForDeviceData %s %s minTime %dms", 
        pDeviceName, found ? "OK" : "DEVICE_NOT_PRESENT", minTimeBetweenReportsMs);
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
uint32_t DeviceManager::getDeviceListFrozen(RaftDevice** pDevices, uint32_t maxDevices, bool onlyOnline, bool* pDeviceOnlineArray) const
{
    if (!RaftMutex_lock(_accessMutex, 5))
        return 0;
    uint32_t numDevices = 0;
    for (auto& devPtrAndOnline : _deviceList)
    {
        if (numDevices >= maxDevices)
            break;
        if (pDeviceOnlineArray)
            pDeviceOnlineArray[numDevices] = devPtrAndOnline.isOnline;
        if (devPtrAndOnline.pDevice && (!onlyOnline || devPtrAndOnline.isOnline))
            pDevices[numDevices++] = devPtrAndOnline.pDevice;
    }
    RaftMutex_unlock(_accessMutex);
    return numDevices;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Find device in device list
/// @param pDeviceID ID of the device
/// @return pointer to device if found
RaftDevice* DeviceManager::getDeviceByID(const char* pDeviceID) const
{
    if (!RaftMutex_lock(_accessMutex, 5))
        return nullptr;
    for (auto& devPtrAndOnline : _deviceList)
    {
        if (devPtrAndOnline.pDevice && (devPtrAndOnline.pDevice->idMatches(pDeviceID)))
        {
            RaftMutex_unlock(_accessMutex);
            return devPtrAndOnline.pDevice;
        }
    }
    RaftMutex_unlock(_accessMutex);
    return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get bus name by bus number
/// @param busNum Bus number (starting from DEVICE_CONN_MODE_FIRST_BUS)
/// @param busName (out) Bus name if found
/// @return true if bus found
bool DeviceManager::getBusNameByNumber(uint16_t busNum, String& busName) const
{
    uint16_t connModeBusNum = DEVICE_CONN_MODE_FIRST_BUS;
    for (RaftBus* pBus : raftBusSystem.getBusList())
    {
        if (!pBus)
            continue;
        RaftBusDevicesIF* pDevicesIF = pBus->getBusDevicesIF();
        if (!pDevicesIF)
            continue;
        
        // Check if this is the requested bus number
        if (connModeBusNum == busNum)
        {
            busName = pBus->getBusName();
            return true;
        }
        
        // Next bus
        connModeBusNum++;
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief call device status change callbacks
/// @param pDevice pointer to the device
/// @param el bus element address and status
/// @param newlyCreated true if the device was newly created
void DeviceManager::callDeviceStatusChangeCBs(RaftDevice* pDevice, const BusElemAddrAndStatus& el, bool newlyCreated)
{
    // Obtain a lock & make a copy of the device status change callbacks
    if (!RaftMutex_lock(_accessMutex, 5))
        return;
    std::vector<RaftDeviceStatusChangeCB> statusChangeCallbacks(_deviceStatusChangeCBList.begin(), _deviceStatusChangeCBList.end());
    RaftMutex_unlock(_accessMutex);

    // Call the device status change callbacks
    for (RaftDeviceStatusChangeCB statusChangeCB : statusChangeCallbacks)
    {
        statusChangeCB(*pDevice, el.isChangeToOnline || newlyCreated, newlyCreated);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Register for device data change callbacks
/// @param pDeviceName Name of the device (nullptr for all devices)
/// @return number of devices registered for data change callbacks
uint32_t DeviceManager::registerForDeviceDataChangeCBs(const char* pDeviceName)
{
    // Get mutex
    if (!RaftMutex_lock(_accessMutex, 5))
        return 0;

    // Create a vector of devices for the device data change callbacks
    std::vector<DeviceDataChangeRecTmp> deviceListForDataChangeCB;
    for (auto& rec : _deviceDataChangeCBList)
    {
        // Check if the device name matches (if specified)
        if (pDeviceName && (rec.deviceName != pDeviceName))
            continue;
        // Find device
        RaftDevice* pDevice = nullptr;
        for (auto& devPtrAndOnline : _deviceList)
        {
            if (!devPtrAndOnline.pDevice)
                continue;
            if (rec.deviceName == devPtrAndOnline.pDevice->getDeviceName())
            {
                pDevice = devPtrAndOnline.pDevice;
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
