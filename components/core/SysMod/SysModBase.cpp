/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Base class for SysMods (System Modules)
// For more info see SysManager
// Rob Dobson 2013-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "SysModBase.h"
#include "SysManager.h"
#include "ConfigPinMap.h"
#include "CommsCoreIF.h"

SysManager* SysModBase::_pSysManager = NULL;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SysModBase::SysModBase(const char *pModuleName, ConfigBase& defaultConfig, ConfigBase* pGlobalConfig, ConfigBase* pMutableConfig, 
            const char* pGlobalConfigPrefix, bool mutableConfigIsGlobal)
{
    // Set sysmod name
    if (pModuleName)
        _sysModName = pModuleName;
    _sysModLogPrefix = _sysModName + ": ";

    // Handle config
    String moduleConfigPrefix = _sysModName;
    if (pGlobalConfigPrefix)
        moduleConfigPrefix = pGlobalConfigPrefix;
    _combinedConfig.addConfig(&defaultConfig, moduleConfigPrefix.c_str(), false);
    _combinedConfig.addConfig(pGlobalConfig, moduleConfigPrefix.c_str(), false);
    _combinedConfig.addConfig(pMutableConfig, mutableConfigIsGlobal ? moduleConfigPrefix.c_str() : "", true);

    // Set log level for module if specified
    String logLevel = configGetString("logLevel", "");
    setModuleLogLevel(pModuleName, logLevel);

    // Add to system module manager
    if (_pSysManager)
        _pSysManager->add(this);
}

SysModBase::~SysModBase()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String SysModBase::getSystemName()
{
    if (_pSysManager)
        return _pSysManager->getSystemName();
    return "";
}

String SysModBase::getSystemUniqueString()
{
    if (_pSysManager)
        return _pSysManager->getSystemUniqueString();
    return "";
}

String SysModBase::getFriendlyName(bool& isSet)
{
    if (_pSysManager)
        return _pSysManager->getFriendlyName(isSet);
    isSet = false;
    return "";
}

RestAPIEndpointManager* SysModBase::getRestAPIEndpointManager()
{
    // Check parent
    if (!_pSysManager)
        return nullptr;

    // Get parent's endpoints
    return _pSysManager->getRestAPIEndpointManager();
}

// Get CommsCore
CommsCoreIF* SysModBase::getCommsCore()
{
    // Check parent
    if (!_pSysManager)
        return nullptr;

    // Get CommsCore
    return _pSysManager->getCommsCore();
}

long SysModBase::configGetLong(const char *dataPath, long defaultValue)
{
    return _combinedConfig.getLong(dataPath, defaultValue);
}

double SysModBase::configGetDouble(const char *dataPath, double defaultValue)
{
    return _combinedConfig.getDouble(dataPath, defaultValue);
}

bool SysModBase::configGetBool(const char *dataPath, bool defaultValue)
{
    return _combinedConfig.getBool(dataPath, defaultValue);
}

String SysModBase::configGetString(const char *dataPath, const char* defaultValue)
{
    return _combinedConfig.getString(dataPath, defaultValue);
}

String SysModBase::configGetString(const char *dataPath, const String& defaultValue)
{
    return _combinedConfig.getString(dataPath, defaultValue.c_str());
}

bool SysModBase::configGetArrayElems(const char *dataPath, std::vector<String>& strList) const
{
    return _combinedConfig.getArrayElems(dataPath, strList);
}

void SysModBase::configRegisterChangeCallback(ConfigChangeCallbackType configChangeCallback)
{
    _combinedConfig.registerChangeCallback(configChangeCallback);
}

int SysModBase::configGetPin(const char* dataPath, const char* defaultValue)
{
    String pinName = configGetString(dataPath, defaultValue);
    return ConfigPinMap::getPinFromName(pinName.c_str());
}

void SysModBase::configSaveData(const String& configStr)
{
    _combinedConfig.writeConfig(configStr);
}

// Get JSON status of another SysMod
String SysModBase::sysModGetStatusJSON(const char* sysModName)
{
    if (_pSysManager)
        return _pSysManager->getStatusJSON(sysModName);
    return "{\"rslt\":\"fail\"}";
}

// Post JSON command to another SysMod
RaftRetCode SysModBase::sysModSendCmdJSON(const char* sysModName, const char* jsonCmd)
{
    if (_pSysManager)
        return _pSysManager->sendCmdJSON(sysModName, jsonCmd);
    return RAFT_INVALID_OPERATION;
}

// SysMod get named value
double SysModBase::sysModGetNamedValue(const char* sysModName, const char* valueName, bool& isValid)
{
    if (_pSysManager)
        return _pSysManager->getNamedValue(sysModName, valueName, isValid);
    isValid = false;
    return 0;
}

// Add status change callback on a SysMod
void SysModBase::sysModSetStatusChangeCB(const char* sysModName, SysMod_statusChangeCB statusChangeCB)
{
    if (_pSysManager)
        _pSysManager->setStatusChangeCB(sysModName, statusChangeCB);
}

// Execute status change callbacks
void SysModBase::executeStatusChangeCBs(bool changeToOn)
{
    for (SysMod_statusChangeCB& statusChangeCB : _statusChangeCBs)
    {
        if (statusChangeCB)
            statusChangeCB(_sysModName, changeToOn);
    }
}

SupervisorStats* SysModBase::getSysManagerStats()
{
    if (_pSysManager)
        return _pSysManager->getStats();
    return nullptr;
}

// File/stream system activity - main firmware update
bool SysModBase::isSystemMainFWUpdate()
{
    if (getSysManager())
        return getSysManager()->isSystemMainFWUpdate();
    return false;
}

// File/stream system activity - file transfer
bool SysModBase::isSystemFileTransferring()
{
    if (getSysManager())
        return getSysManager()->isSystemFileTransferring();
    return false;
}

// File/stream system activity - streaming
bool SysModBase::isSystemStreaming()
{
    if (getSysManager())
        return getSysManager()->isSystemStreaming();
    return false;
}
