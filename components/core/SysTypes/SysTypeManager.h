/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Manager for SysTypes
// Handles selection of system type from a set of JSON alternatives
//
// Rob Dobson 2019-2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <Logger.h>
#include <ConfigNVS.h>
#include <list>
#include <SpiramAwareAllocator.h>
#include <functional>

class RestAPIEndpointManager;
class APISourceInfo;
class SysManager;

class SysTypeManager
{
public:
    // Constructor
    SysTypeManager(ConfigNVS& sysTypeConfig);

    // Setup
    void setup(const char** pSysTypeConfigArrayStatic, int sysTypeConfigArrayLen);

    // Add endpoints
    void addRestAPIEndpoints(RestAPIEndpointManager& endpointManager);

    // Handling of SysTypes list
    void getSysTypesListJSON(String& sysTypesListJSON);
    bool getSysTypeConfig(const String& sysTypeName, String& sysTypeConfig);

    // Set SysSettings which are generally non-volatile and contain one of the SysType values
    bool setSysSettings(const uint8_t *pData, int len);

    // Set system restart callback
    void setSystemRestartCallback(std::function<void()> systemRestartCallback)
    {
        _systemRestartCallback = systemRestartCallback;
    }

private:
    // List of sysTypes
    std::list<const char*> _sysTypesList;

    // Current sysType name
    String _curSysTypeName;

    // System type configuration
    ConfigNVS& _sysTypeConfig;

    // Last post result ok
    bool _lastPostResultOk = false;
    std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> _postResultBuf;

    // System reset callback
    std::function<void()> _systemRestartCallback = nullptr;

    // API System type
    RaftRetCode apiGetSysTypes(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);
    RaftRetCode apiGetSysTypeConfig(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);

    // API System settings
    RaftRetCode apiSysTypeGetSettings(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);
    RaftRetCode apiSysTypePostSettings(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);
    RaftRetCode apiSysTypePostSettingsBody(const String& reqStr, const uint8_t *pData, size_t len, 
                        size_t index, size_t total, const APISourceInfo& sourceInfo);
    RaftRetCode apiSysTypeClearSettings(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);
};
