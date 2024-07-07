////////////////////////////////////////////////////////////////////////////////
//
// DeviceManager.cpp
//
////////////////////////////////////////////////////////////////////////////////

#include "DeviceManager.h"
#include "RaftUtils.h"
#include "RaftDevice.h"
#include "SysManager.h"
#include <functional>

// Warnings
#define WARN_ON_DEVICE_CLASS_NOT_FOUND
#define WARN_ON_DEVICE_INSTANTIATION_FAILED

// Debug
// #define DEBUG_DEVICE_FACTORY
// #define DEBUG_LIST_DEVICES
// #define DEBUG_JSON_DEVICE_DATA
// #define DEBUG_JSON_DEVICE_HASH

static const char *MODULE_PREFIX = "DeviceManager";

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

    // Register data source (message generator and state detector functions)
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
        if (pDevice)
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
    // Debug
    for (const auto& el : statusChanges)
    {
        LOG_I(MODULE_PREFIX, "busElemStatusInfo %s %s %s %s", bus.getBusName().c_str(), 
            bus.addrToString(el.address).c_str(), el.isChangeToOnline ? "Online" : ("Offline" + String(el.isChangeToOffline ? " (was online)" : "")).c_str(),
            el.isNewlyIdentified ? ("DevTypeIdx " + String(el.deviceTypeIndex)).c_str() : "");
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
            // TODO - remove
            LOG_I(MODULE_PREFIX, "setup pDevice %p name %s", 
                    pDevice, pDevice->getDeviceName());
            pDevice->setup();
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

    // Post-setup - called after setup of all sysMods complete
    for (auto* pDevice : _deviceList)
    {
        if (pDevice)
            pDevice->postSetup();
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
        String jsonRespStr = pDevicesIF->getPollResponsesJson();

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
            uint32_t identPollLastMs = pBus->getLastStatusUpdateMs(true, true);
            stateHash[0] ^= (identPollLastMs & 0xff);
            stateHash[1] ^= ((identPollLastMs >> 8) & 0xff);
        }
    }

    // Check all devices for data
    for (RaftDevice* pDevice : _deviceList)
    {
        // Check device
        if (pDevice)
        {
            // Check device status
            uint32_t identPollLastMs = pDevice->getLastStatusUpdateMs(true, true);
            stateHash[0] ^= (identPollLastMs & 0xff);
            stateHash[1] ^= ((identPollLastMs >> 8) & 0xff);
        }
    }

    // Debug
#ifdef DEBUG_JSON_DEVICE_HASH
    LOG_I(MODULE_PREFIX, "getDevicesHash %s", ("Hash " + String(stateHash[0],16) + String(stateHash[1],16)).c_str());
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
        if (pDevice && pDevice->getDeviceName() == pDeviceName)
            return pDevice;
    }
    return nullptr;
}
