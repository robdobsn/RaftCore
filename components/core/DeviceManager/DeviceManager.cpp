////////////////////////////////////////////////////////////////////////////////
//
// DeviceManager.cpp
//
////////////////////////////////////////////////////////////////////////////////

#include <functional>
#include "DeviceManager.h"
#include "DeviceTypeRecordDynamic.h"
#include "DeviceTypeRecords.h"
#include "RaftUtils.h"
#include "RaftDevice.h"
#include "RaftBusDevice.h"
#include "SysManager.h"

// Warnings
#define WARN_ON_DEVICE_CLASS_NOT_FOUND
#define WARN_ON_DEVICE_INSTANTIATION_FAILED

// Debug
// #define DEBUG_DEVICE_FACTORY
// #define DEBUG_LIST_DEVICES
// #define DEBUG_JSON_DEVICE_DATA
// #define DEBUG_JSON_DEVICE_HASH
// #define DEBUG_DEVMAN_API
// #define DEBUG_DEVICE_SETUP
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
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor
DeviceManager::~DeviceManager()
{
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

    // Post-setup - called after setup of all sysMods complete
    for (auto* pDevice : _deviceList)
    {
        if (pDevice)
            pDevice->postSetup();
    }

    // Check for any device data change callbacks
    for (auto& rec : _deviceDataChangeCBList)
    {
        // Get device
        RaftDevice* pDevice = getDevice(rec.deviceName.c_str());
        if (!pDevice)
        {
            LOG_W(MODULE_PREFIX, "postSetup deviceDataChangeCB %s not found", rec.deviceName.c_str());
            continue;
        }

        // Register for device data notification with the device
        pDevice->registerForDeviceData(
            rec.dataChangeCB,
            rec.minTimeBetweenReportsMs,
            rec.pCallbackInfo
        );

        // Debug
        LOG_I(MODULE_PREFIX, "postSetup registered deviceDataChangeCB for %s", rec.deviceName.c_str());
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Loop function
void DeviceManager::loop()
{
    // Service the buses
    raftBusSystem.loop();

    // Loop through the devices
    for (auto* pDevice : _deviceList)
    {
        // Check valid
        if (!pDevice)
            continue;

        // Handle device loop
        pDevice->loop();
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
    // Locate (or add) the device and handle the status change
    for (const auto& el : statusChanges)
    {
        // Check if device is in the device list
        RaftDevice* pFoundDevice = nullptr;
        String deviceId = bus.formUniqueId(el.address);
        for (auto* pDevice : _deviceList)
        {
            if (pDevice && (pDevice->idMatches(deviceId.c_str())))
            {
                pFoundDevice = pDevice;
                break;
            }
        }
#ifdef DEBUG_BUS_ELEMENT_STATUS
        bool newlyCreated = false;
#endif
        if (!pFoundDevice)
        {
            // Check if device newly created
            if (el.isNewlyIdentified)
            {
                // Generate config JSON for the device
                String devConfig = "{\"name\":" + deviceId + "}";

                // Create the device
                pFoundDevice = new RaftBusDevice(bus.getBusName().c_str(), el.address, "RaftBusDevice", devConfig.c_str());
                pFoundDevice->setDeviceTypeIndex(el.deviceTypeIndex);

#ifdef DEBUG_BUS_ELEMENT_STATUS
                newlyCreated = true;
#endif

                // Add to the list of instantiated devices
                _deviceList.push_back(pFoundDevice);

                // Setup device
                pFoundDevice->setup();
            }
        }

        // Handle status update
        if (pFoundDevice)
        {
            // Handle device status change
            pFoundDevice->handleStatusChange(el.isChangeToOnline, el.isChangeToOffline, el.isNewlyIdentified, el.deviceTypeIndex);

            // Register for device data notifications if required
            for (auto& rec : _deviceDataChangeCBList)
            {
                if (rec.deviceName == pFoundDevice->getDeviceName())
                {
                    // Register for device data notification with the device
                    pFoundDevice->registerForDeviceData(
                        rec.dataChangeCB,
                        rec.minTimeBetweenReportsMs,
                        rec.pCallbackInfo
                    );

                    // Debug
                    LOG_I(MODULE_PREFIX, "busElemStatusCB registered deviceDataChangeCB for %s", rec.deviceName.c_str());
                }
            }
        }

        // Debug
#ifdef DEBUG_BUS_ELEMENT_STATUS
        LOG_I(MODULE_PREFIX, "busElemStatusInfo ID %s %s%s%s%s",
                        deviceId.c_str(), 
                        el.isChangeToOnline ? "Online" : ("Offline" + String(el.isChangeToOffline ? " (was online)" : "")).c_str(),
                        el.isNewlyIdentified ? (" DevTypeIdx " + String(el.deviceTypeIndex)).c_str() : "",
                        newlyCreated ? " NewlyCreated" : "",
                        pFoundDevice ? "" : " NOT IDENTIFIED YET");
#endif
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup devices
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

    // Check all devices for data
    for (RaftDevice* pDevice : _deviceList)
    {
        if (!pDevice)
            continue;
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

    // Add device data
    for (RaftDevice* pDevice : _deviceList)
    {
        if (!pDevice)
            continue;
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

    // Check all devices for data
    for (RaftDevice* pDevice : _deviceList)
    {
        // Check device
        if (pDevice)
        {
            // Check device status
            uint32_t identPollLastMs = pDevice->getDeviceInfoTimestampMs(true, true);
            stateHash[0] ^= (identPollLastMs & 0xff);
            stateHash[1] ^= ((identPollLastMs >> 8) & 0xff);

#ifdef DEBUG_JSON_DEVICE_HASH_DETAIL
            LOG_I(MODULE_PREFIX, "getDevicesHash %s %02x%02x", pDevice->getDeviceName().c_str(), stateHash[0], stateHash[1]);
#endif
        }
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
    for (auto* pDevice : _deviceList)
    {
#ifdef DEBUG_GET_DEVICE
        LOG_I(MODULE_PREFIX, "getDevice %s checking %s", pDeviceName, pDevice ? pDevice->getDeviceName() : "UNKNOWN");
#endif
        if (pDevice && pDevice->getDeviceName() == pDeviceName)
            return pDevice;
    }
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

    // Check all devices for data
    for (RaftDevice* pDevice : _deviceList)
    {
        if (!pDevice)
            continue;
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
                            " devman/typeinfo?bus=<busName>&type=<typeName> - Get device info for type by name or number,"
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
