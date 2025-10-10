////////////////////////////////////////////////////////////////////////////////
//
// DeviceManager.cpp
//
////////////////////////////////////////////////////////////////////////////////

#include <functional>
#include "DeviceManager.h"
#include "DeviceTypeRecordDynamic.h"
#include "DeviceTypeRecords.h"
#include "RaftBusSystem.h"
#include "RaftBusDevice.h"
#include "DeviceFactory.h"
#include "SysManager.h"
#include "RestAPIEndpointManager.h"

// Warnings
#define WARN_ON_DEVICE_CLASS_NOT_FOUND
#define WARN_ON_DEVICE_INSTANTIATION_FAILED

// Debug
// #define DEBUG_DEVICE_SETUP
// #define DEBUG_DEVICE_FACTORY
// #define DEBUG_LIST_DEVICES
// #define DEBUG_JSON_DEVICE_DATA
// #define DEBUG_JSON_DEVICE_HASH
// #define DEBUG_DEVMAN_API
// #define DEBUG_BUS_ELEMENT_STATUS
// #define DEBUG_GET_DEVICE
// #define DEBUG_JSON_DEVICE_HASH_DETAIL
// #define DEBUG_MAKE_BUS_REQUEST_VERBOSE
// #define DEBUG_API_CMDRAW

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
    uint32_t numDevices = getDeviceListFrozen(pDeviceListCopy, DEVICE_LIST_MAX_SIZE);

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

    // Get a frozen copy of the device list
    RaftDevice* pDeviceListCopy[DEVICE_LIST_MAX_SIZE];
    uint32_t numDevices = getDeviceListFrozen(pDeviceListCopy, DEVICE_LIST_MAX_SIZE);

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
    LOG_I(MODULE_PREFIX, "busOperationStatusInfo %s %s", bus.getBusName().c_str(), 
        RaftBus::busOperationStatusToString(busOperationStatus));
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
                    _deviceList.push_back(pDevice);
                    RaftMutex_unlock(_accessMutex);

                    // Setup device
                    pDevice->setup();
                    pDevice->postSetup();
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
        }
        
        // Debug
#ifdef DEBUG_BUS_ELEMENT_STATUS
        LOG_I(MODULE_PREFIX, "busElemStatusInfo ID %s %s%s%s%s",
                        deviceId.c_str(), 
                        el.isChangeToOnline ? "Online" : ("Offline" + String(el.isChangeToOffline ? " (was online)" : "")).c_str(),
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
        _deviceList.push_back(pDevice);

        // Debug
#ifdef DEBUG_DEVICE_FACTORY
        {
            LOG_I(MODULE_PREFIX, "setup class %s devConf %s", 
                        devClass.c_str(), devConf.c_str());
        }
#endif

    }

    // Now call setup on instantiated devices
    for (auto* pDevice : _deviceList)
    {
        if (pDevice)
        {
#ifdef DEBUG_DEVICE_SETUP            
            LOG_I(MODULE_PREFIX, "setup pDevice %p name %s", pDevice, pDevice->getDeviceName());
#endif
            // Setup device
            pDevice->setup();

            // See if the device has a device type record
            DeviceTypeRecordDynamic devTypeRec;
            if (pDevice->getDeviceTypeRecord(devTypeRec))
            {
                // Add the device type record to the device type records
                uint16_t deviceTypeIndex = 0;
                deviceTypeRecords.addExtendedDeviceTypeRecord(devTypeRec, deviceTypeIndex);
                pDevice->setDeviceTypeIndex(deviceTypeIndex);
            }
        }
    }

    // Give each SysMod the opportunity to add endpoints and comms channels and to keep a
    // pointer to the CommsCoreIF that can be used to send messages
    for (auto* pDevice : _deviceList)
    {
        if (pDevice)
        {
            if (getRestAPIEndpointManager())
                pDevice->addRestAPIEndpoints(*getRestAPIEndpointManager());
            if (getCommsCore())
                pDevice->addCommsChannels(*getCommsCore());
        }            
    }

#ifdef DEBUG_LIST_DEVICES
    uint32_t deviceIdx = 0;
    for (auto* pDevice : _deviceList)
    {
        LOG_I(MODULE_PREFIX, "Device %d: %s", deviceIdx++, 
                pDevice ? pDevice->getDeviceName() : "UNKNOWN");
            
    }
    if (_deviceList.size() == 0)
        LOG_I(MODULE_PREFIX, "No devices found");
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get devices' data as JSON
/// @return JSON string
String DeviceManager::getDevicesDataJSON() const
{
    // JSON strings
    String jsonStrBus, jsonStrDev;

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
            jsonStrBus += (jsonStrBus.length() == 0 ? "\"" : ",\"") + pBus->getBusName() + "\":" + jsonRespStr;
        }
    }

    // Get a frozen copy of the device list (null-pointers excluded)
    RaftDevice* pDeviceListCopy[DEVICE_LIST_MAX_SIZE];
    uint32_t numDevices = getDeviceListFrozen(pDeviceListCopy, DEVICE_LIST_MAX_SIZE);

    // Loop through the devices
    for (uint32_t devIdx = 0; devIdx < numDevices; devIdx++)
    {
        RaftDevice* pDevice = pDeviceListCopy[devIdx];
        String jsonRespStr = pDevice->getStatusJSON();

        // Check for empty string or empty JSON object
        if (jsonRespStr.length() > 2)
        {
            jsonStrDev += (jsonStrDev.length() == 0 ? "\"" : ",\"") + pDevice->getPublishDeviceType() + "\":" + jsonRespStr;
        }
    }

#ifdef DEBUG_JSON_DEVICE_DATA
    LOG_I(MODULE_PREFIX, "getDevicesDataJSON BUS %s DEV %s ", jsonStrBus.c_str(), jsonStrDev.c_str());
#endif

    return "{" + (jsonStrBus.length() == 0 ? (jsonStrDev.length() == 0 ? "" : jsonStrDev) : (jsonStrDev.length() == 0 ? jsonStrBus : jsonStrBus + "," + jsonStrDev)) + "}";
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
    uint32_t numDevices = getDeviceListFrozen(pDeviceListCopy, DEVICE_LIST_MAX_SIZE);

    // Loop through the devices
    for (uint32_t devIdx = 0; devIdx < numDevices; devIdx++)
    {
        RaftDevice* pDevice = pDeviceListCopy[devIdx];
        std::vector<uint8_t> deviceBinaryData = pDevice->getStatusBinary();
        binaryData.insert(binaryData.end(), deviceBinaryData.begin(), deviceBinaryData.end());
    }

    return binaryData;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check for change of devices' data
/// @param stateHash hash of the currently available data
void DeviceManager::getDevicesHash(std::vector<uint8_t>& stateHash) const
{
    // Clear hash to two bytes
    stateHash.clear();
    stateHash.push_back(0);
    stateHash.push_back(0);

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
            LOG_I(MODULE_PREFIX, "getDevicesHash %s %02x%02x", pBus->getBusName().c_str(), stateHash[0], stateHash[1]);
#endif
        }
    }

    // Get a frozen copy of the device list (null-pointers excluded)
    RaftDevice* pDeviceListCopy[DEVICE_LIST_MAX_SIZE];
    uint32_t numDevices = getDeviceListFrozen(pDeviceListCopy, DEVICE_LIST_MAX_SIZE);

    // Loop through the devices
    for (uint32_t devIdx = 0; devIdx < numDevices; devIdx++)
    {
        // Check device status
        RaftDevice* pDevice = pDeviceListCopy[devIdx];
        uint32_t identPollLastMs = pDevice->getDeviceInfoTimestampMs(true, true);
        stateHash[0] ^= (identPollLastMs & 0xff);
        stateHash[1] ^= ((identPollLastMs >> 8) & 0xff);

#ifdef DEBUG_JSON_DEVICE_HASH_DETAIL
        LOG_I(MODULE_PREFIX, "getDevicesHash %s %02x%02x", pDevice->getDeviceName().c_str(), stateHash[0], stateHash[1]);
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
    if (!RaftMutex_lock(_accessMutex, 5))
        return nullptr;

    // Loop through the devices
    for (auto* pDevice : _deviceList)
    {
#ifdef DEBUG_GET_DEVICE
        LOG_I(MODULE_PREFIX, "getDevice %s checking %s", pDeviceName, pDevice ? pDevice->getDeviceName() : "UNKNOWN");
#endif
        if (pDevice && pDevice->getDeviceName() == pDeviceName)
        {
            RaftMutex_unlock(_accessMutex);
            return pDevice;
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
    String jsonStrBus, jsonStrDev;

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
    uint32_t numDevices = getDeviceListFrozen(pDeviceListCopy, DEVICE_LIST_MAX_SIZE);

    // Loop through the devices
    for (uint32_t devIdx = 0; devIdx < numDevices; devIdx++)
    {
        RaftDevice* pDevice = pDeviceListCopy[devIdx];
        String jsonRespStr = pDevice->getDebugJSON(true);

        // Check for empty string or empty JSON object
        if (jsonRespStr.length() > 2)
        {
            jsonStrDev += (jsonStrDev.length() == 0 ? "\"" : ",\"") + pDevice->getPublishDeviceType() + "\":" + jsonRespStr;
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
                            " devman/cmdraw?bus=<busName>&addr=<addr>&hexWr=<hexWriteData>&numToRd=<numBytesToRead>&msgKey=<msgKey> - Send raw command to device");
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
                devInfo = pDevicesIF->getDevTypeInfoJsonByTypeIdx(devTypeName.toInt(), false);
            }
            if (devInfo.length() == 0)
            {
                // Get device info by name if possible
                devInfo = pDevicesIF->getDevTypeInfoJsonByTypeName(devTypeName, false);
            }
        }
        else
        {
            // Use the global device type info to get the device info
            if ((devTypeName.length() > 0) && isdigit(devTypeName[0]))
            {
                // Get device info by number
                devInfo = deviceTypeRecords.getDevTypeInfoJsonByTypeIdx(devTypeName.toInt(), false);
            }
            if (devInfo.length() == 0)
            {
                // Get device info by name if possible
                devInfo = deviceTypeRecords.getDevTypeInfoJsonByTypeName(devTypeName, false);
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
        LOG_I(MODULE_PREFIX, "apiHWDevice bus %s type %s devInfo %s", busName.c_str(), devTypeName.c_str(), devInfo.c_str());
#endif

        // Set result
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, ("\"devinfo\":" + devInfo).c_str());
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
/// @brief Get a frozen version of device list
/// @param pDeviceList (out) list of devices
/// @param maxNumDevices maximum number of devices to return
/// @return number of devices
uint32_t DeviceManager::getDeviceListFrozen(RaftDevice** pDevices, uint32_t maxDevices) const
{
    if (!RaftMutex_lock(_accessMutex, 5))
        return 0;
    uint32_t numDevices = 0;
    for (auto* pDevice : _deviceList)
    {
        if (numDevices >= maxDevices)
            break;
        if (pDevice)
            pDevices[numDevices++] = pDevice;
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
    for (auto* pDevice : _deviceList)
    {
        if (pDevice && (pDevice->idMatches(pDeviceID)))
        {
            RaftMutex_unlock(_accessMutex);
            return pDevice;
        }
    }
    RaftMutex_unlock(_accessMutex);
    return nullptr;
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
        for (auto* pTestDevice : _deviceList)
        {
            if (!pTestDevice)
                continue;
            if (rec.deviceName == pTestDevice->getDeviceName())
            {
                pDevice = pTestDevice;
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