/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Base class for Raft SysMods (System Modules)
// For more info see SysManager
// Rob Dobson 2013-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "RaftSysMod.h"
#include "SysManagerIF.h"
#include "CommsCoreIF.h"
#include "DeviceManager.h"

SysManagerIF* RaftSysMod::_pSysManager = NULL;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
/// @param pModuleName Module name
/// @param sysConfig System configuration interface
/// @param pConfigPrefix Configuration prefix
/// @param pMutableConfigNamespace Mutable configuration namespace
/// @param pMutableConfigPrefix Mutable configuration prefix
RaftSysMod::RaftSysMod(const char *pModuleName, 
            RaftJsonIF& sysConfig,
            const char* pConfigPrefix, 
            const char* pMutableConfigNamespace,
            const char* pMutableConfigPrefix) :
        config(sysConfig, pConfigPrefix ? pConfigPrefix : pModuleName)
{
    // Set sysmod name
    if (pModuleName)
        _sysModName = pModuleName;
    _sysModLogPrefix = _sysModName + ": ";

    // Set log level for module if specified
    String logLevel = configGetString("logLevel", "");
    setModuleLogLevel(pModuleName, logLevel);

    // Add to system module manager
    if (_pSysManager)
        _pSysManager->addManagedSysMod(this);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor
RaftSysMod::~RaftSysMod()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get system name
/// @return System name
String RaftSysMod::getSystemName()
{
    bool isValid = false;
    if (_pSysManager)
        return _pSysManager->getNamedString(nullptr, "SystemName", isValid);
    return "";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get system unique string
/// @return System unique string
String RaftSysMod::getSystemUniqueString()
{
    bool isValid = false;
    if (_pSysManager)
        return _pSysManager->getNamedString(nullptr, "SystemUniqueString", isValid);
    return "";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get friendly name
/// @param isSet (out) true if friendly name is set
/// @return Friendly name
String RaftSysMod::getFriendlyName(bool& isSet)
{
    bool isValid = false;
    if (_pSysManager)
    {
        isSet = _pSysManager->getNamedValue(nullptr, "FriendlyNameIsSet", isValid) != 0.0;
        return _pSysManager->getNamedString(nullptr, "FriendlyName", isValid);
    }
    isSet = false;
    return "";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get REST API endpoint manager
/// @return Pointer to RestAPIEndpointManager
RestAPIEndpointManager* RaftSysMod::getRestAPIEndpointManager()
{
    // Check parent
    if (!_pSysManager)
        return nullptr;

    // Get parent's endpoints
    return _pSysManager->getRestAPIEndpointManager();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get CommsCore interface
/// @return Pointer to CommsCoreIF
CommsCoreIF* RaftSysMod::getCommsCore()
{
    // Check parent
    if (!_pSysManager)
        return nullptr;

    // Get CommsCore
    return _pSysManager->getCommsCore();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Configuration access methods
int RaftSysMod::configGetInt(const char *dataPath, int defaultValue)
{
    return config.getInt(dataPath, defaultValue);
}

long RaftSysMod::configGetLong(const char *dataPath, long defaultValue)
{
    return config.getLong(dataPath, defaultValue);
}

double RaftSysMod::configGetDouble(const char *dataPath, double defaultValue)
{
    return config.getDouble(dataPath, defaultValue);
}

bool RaftSysMod::configGetBool(const char *dataPath, bool defaultValue)
{
    return config.getBool(dataPath, defaultValue);
}

String RaftSysMod::configGetString(const char *dataPath, const char* defaultValue)
{
    return config.getString(dataPath, defaultValue);
}

String RaftSysMod::configGetString(const char *dataPath, const String& defaultValue)
{
    return config.getString(dataPath, defaultValue.c_str());
}

RaftJsonIF::RaftJsonType RaftSysMod::configGetType(const char *dataPath, int& arrayLen)
{
    return config.getType(dataPath, arrayLen);
}

bool RaftSysMod::configGetArrayElems(const char *dataPath, std::vector<String>& strList) const
{
    return config.getArrayElems(dataPath, strList);
}

void RaftSysMod::configRegisterChangeCallback(RaftJsonChangeCallbackType configChangeCallback)
{
    config.registerChangeCallback(configChangeCallback);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Save config data from JSON string
/// @param configStr JSON string
void RaftSysMod::configSaveData(const String& configStr)
{
    config.setJsonDoc(configStr.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get JSON status string from another SysMod
/// @param sysModName Name of SysMod
/// @return JSON status string
String RaftSysMod::sysModGetStatusJSON(const char* sysModName) const
{
    if (_pSysManager)
        return _pSysManager->getStatusJSON(sysModName);
    return "{\"rslt\":\"fail\"}";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Send JSON command to another SysMod
/// @param sysModName Name of SysMod
/// @param jsonCmd JSON command
/// @return RaftRetCode
RaftRetCode RaftSysMod::sysModSendCmdJSON(const char* sysModName, const char* jsonCmd)
{
    if (_pSysManager)
        return _pSysManager->sendCmdJSON(sysModName, jsonCmd);
    return RAFT_INVALID_OPERATION;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get named value from another SysMod
/// @param sysModName Name of SysMod
/// @param valueName Name of value
/// @param isValid (out) true if value is valid
/// @return Named value
double RaftSysMod::sysModGetNamedValue(const char* sysModName, const char* valueName, bool& isValid) const
{
    if (_pSysManager)
        return _pSysManager->getNamedValue(sysModName, valueName, isValid);
    isValid = false;
    return 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get named string from another SysMod
/// @param pSysModName Name of the SysMod
/// @param valueName String name
/// @param isValid (out) true if value is valid
/// @return string
String RaftSysMod::sysModGetNamedString(const char* pSysModName, const char* valueName, bool& isValid) const
{
    if (_pSysManager)
        return _pSysManager->getNamedString(pSysModName, valueName, isValid);
    isValid = false;
    return "";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set the status change callback for a SysMod
/// @param sysModName Name of SysMod
/// @param statusChangeCB Callback function
void RaftSysMod::sysModSetStatusChangeCB(const char* sysModName, SysMod_statusChangeCB statusChangeCB)
{
    if (_pSysManager)
        _pSysManager->setStatusChangeCB(sysModName, statusChangeCB);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Execute status change callbacks
/// @param changeToOn true if changing to online
void RaftSysMod::executeStatusChangeCBs(bool changeToOn)
{
    for (SysMod_statusChangeCB& statusChangeCB : _statusChangeCBs)
    {
        if (statusChangeCB)
            statusChangeCB(_sysModName, changeToOn);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get SysManager statistics
/// @return Pointer to SupervisorStats
SupervisorStats* RaftSysMod::getSysManagerStats()
{
    if (_pSysManager)
        return _pSysManager->getStats();
    return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if system main firmware update is in progress
/// @return true if main firmware update in progress
bool RaftSysMod::isSystemMainFWUpdate()
{
    bool isValid;
    if (_pSysManager)
        return _pSysManager->getNamedValue(nullptr, "IsSystemMainFWUpdate", isValid) != 0.0;
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if system file transfer is in progress
/// @return true if file transfer in progress
bool RaftSysMod::isSystemFileTransferring()
{
    bool isValid = false;
    if (_pSysManager)
        return _pSysManager->getNamedValue(nullptr, "IsSystemFileTransferring", isValid) != 0.0;
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if system streaming is in progress
/// @return true if streaming in progress
bool RaftSysMod::isSystemStreaming()
{
    bool isValid = false;
    if (_pSysManager)
        return _pSysManager->getNamedValue(nullptr, "IsSystemStreaming", isValid) != 0.0;
    return false;
}
