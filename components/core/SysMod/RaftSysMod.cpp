/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Base class for Raft SysMods (System Modules)
// For more info see SysManager
// Rob Dobson 2013-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "RaftSysMod.h"
#include "SysManager.h"
#include "ConfigPinMap.h"
#include "CommsCoreIF.h"

SysManager* RaftSysMod::_pSysManager = NULL;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

RaftSysMod::~RaftSysMod()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String RaftSysMod::getSystemName()
{
    if (_pSysManager)
        return _pSysManager->getSystemName();
    return "";
}

String RaftSysMod::getSystemUniqueString()
{
    if (_pSysManager)
        return _pSysManager->getSystemUniqueString();
    return "";
}

String RaftSysMod::getFriendlyName(bool& isSet)
{
    if (_pSysManager)
        return _pSysManager->getFriendlyName(isSet);
    isSet = false;
    return "";
}

RestAPIEndpointManager* RaftSysMod::getRestAPIEndpointManager()
{
    // Check parent
    if (!_pSysManager)
        return nullptr;

    // Get parent's endpoints
    return _pSysManager->getRestAPIEndpointManager();
}

// Get CommsCore
CommsCoreIF* RaftSysMod::getCommsCore()
{
    // Check parent
    if (!_pSysManager)
        return nullptr;

    // Get CommsCore
    return _pSysManager->getCommsCore();
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

int RaftSysMod::configGetPin(const char* dataPath, const char* defaultValue)
{
    String pinName = configGetString(dataPath, defaultValue);
    return ConfigPinMap::getPinFromName(pinName.c_str());
}

void RaftSysMod::configSaveData(const String& configStr)
{
    config.setJsonDoc(configStr.c_str());
}

// Get JSON status of another SysMod
String RaftSysMod::sysModGetStatusJSON(const char* sysModName) const
{
    if (_pSysManager)
        return _pSysManager->getStatusJSON(sysModName);
    return "{\"rslt\":\"fail\"}";
}

// Post JSON command to another SysMod
RaftRetCode RaftSysMod::sysModSendCmdJSON(const char* sysModName, const char* jsonCmd)
{
    if (_pSysManager)
        return _pSysManager->sendCmdJSON(sysModName, jsonCmd);
    return RAFT_INVALID_OPERATION;
}

// SysMod get named value
double RaftSysMod::sysModGetNamedValue(const char* sysModName, const char* valueName, bool& isValid)
{
    if (_pSysManager)
        return _pSysManager->getNamedValue(sysModName, valueName, isValid);
    isValid = false;
    return 0;
}

// Add status change callback on a SysMod
void RaftSysMod::sysModSetStatusChangeCB(const char* sysModName, SysMod_statusChangeCB statusChangeCB)
{
    if (_pSysManager)
        _pSysManager->setStatusChangeCB(sysModName, statusChangeCB);
}

// Execute status change callbacks
void RaftSysMod::executeStatusChangeCBs(bool changeToOn)
{
    for (SysMod_statusChangeCB& statusChangeCB : _statusChangeCBs)
    {
        if (statusChangeCB)
            statusChangeCB(_sysModName, changeToOn);
    }
}

SupervisorStats* RaftSysMod::getSysManagerStats()
{
    if (_pSysManager)
        return _pSysManager->getStats();
    return nullptr;
}

// File/stream system activity - main firmware update
bool RaftSysMod::isSystemMainFWUpdate()
{
    if (getSysManager())
        return getSysManager()->isSystemMainFWUpdate();
    return false;
}

// File/stream system activity - file transfer
bool RaftSysMod::isSystemFileTransferring()
{
    if (getSysManager())
        return getSysManager()->isSystemFileTransferring();
    return false;
}

// File/stream system activity - streaming
bool RaftSysMod::isSystemStreaming()
{
    if (getSysManager())
        return getSysManager()->isSystemStreaming();
    return false;
}
