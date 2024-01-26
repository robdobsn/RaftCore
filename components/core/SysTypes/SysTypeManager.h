/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Manager for SysTypes
// Handles selection of system type from a set of JSON alternatives
//
// Rob Dobson 2019-2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <list>
#include <functional>
#include <vector>
#include "RaftArduino.h"
#include "Logger.h"
#include "RaftJsonNVS.h"
#include "SpiramAwareAllocator.h"
#include "RaftRetCode.h"
#include "SysTypeInfoRec.h"

class RestAPIEndpointManager;
class APISourceInfo;
class SysManager;

class SysTypeManager
{
public:
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Constructor
    /// @param systemConfig system configuration (based on a JSON document)
    /// @note The system configuration is the main JSON document that is used to configure the system
    ///       It comprises an element that is stored in non-volatile storage that may be empty or contain
    ///       a JSON document. If this document contains a key called "SysType" then the value of this key
    ///       is used to select the SysType from the list of SysTypes passed into setup on startup or after
    ///       the SysTypeInfoRecs have been changed. An additional use for the non-volatile JSON document is that
    ///       it will be searched first for any key requested (so can be used to override configuration settings).
    ///       The second element of the system configuration is a chained JSON document that contains the
    ///       configuration for the selected SysType. This is not stored in non-volatile storage and it
    ///       MUST be available throughout the lifetime of this object. A pointer to this chained document 
    ///       is set at the folliowing times: (a) startup, (b) when a different version of the configuration is
    ///       selected (for instance this can be used to implement different configurations for different hardware
    ///       revisions), (c) after the SysTypeInfoRecs have been changed and (d) when API is used to select a
    ///       different SysType.
    SysTypeManager(RaftJsonIF& systemConfig, RaftJson& baseSysTypesJson);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set the base SysTypes to be selected from
    /// @param pSysTypeInfoRecs pointer to a vector of SysTypeInfoRec records
    /// @note The SysTypeInfoRec records are not copied and must remain valid for the lifetime of this object
    void setBaseSysTypes(const SysTypeInfoRec* pSysTypeInfoRecs, uint16_t numSysTypeInfoRecs);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get current SysType name
    /// @return SysType name - if the SysType key exists in JSON non-volatile storage it is used, otherwise the
    ///         last selected SysType name is returned
    String getCurrentSysTypeName();

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set version of base SysType
    /// @param hwRev versionString
    void setBaseSysTypeVersion(const char* pVersionStr);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get version of base SysType
    /// @return versionString
    String getBaseSysTypeVersion()
    {
        return _baseSysTypeVersion;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set callback for system restart
    /// @param systemRestartCallback callback to restart system
    void setSystemRestartCallback(std::function<void()> systemRestartCallback)
    {
        _systemRestartCallback = systemRestartCallback;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get a list of SysTypes as a JSON list
    /// @return JSON document containing just a list of SysTypes
    /// @note the list is generated from the SysTypeInfoRec records passed into setup. The list returned will only
    ///       include those SysTypes that are valid for the current hardware revision
    String getBaseSysTypesListAsJson();

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get the JSON document for a base SysType (SysTypes are used for configuration defined by the JSON doc)
    /// @param pSysTypeName name of the SysType (if this is nullptr or empty then the current SysType doc is returned)
    ///                     the information returned will be for the SysType record relating to the current hardware 
    ///                     revision
    /// @param outJsonDoc JSON document to return content in
    /// @param append true if the JSON document should be appended to
    /// @return bool true if the JSON document was found
    bool getBaseSysTypeContent(const char* pSysTypeName, String& outJsonDoc, bool append);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set the non-volatile document contents
    /// @param pJsonDoc JSON document
    /// @return bool true if the JSON document was successfully set
    /// @note This is the JSON document that is stored in non-volatile storage and is used 
    ///       override configuration settings
    bool setNonVolatileDocContents(const char* pJsonDoc);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Add REST API endpoints
    /// @param endpointManager endpoint manager
    void addRestAPIEndpoints(RestAPIEndpointManager& endpointManager);

private:
    // SysType information records
    const SysTypeInfoRec* _pSysTypeInfoRecs = nullptr;
    uint16_t _numSysTypeInfoRecs = 0;

    // System configuration
    RaftJsonIF& _systemConfig;

    // Chained JSON document used for access to the base SysType
    RaftJson& _baseSysTypeConfig;

    // Base SysType version
    String _baseSysTypeVersion;

    // Index of last SysTypeInfoRec selected
    int _currentlySysTypeInfoRecIdx = -1;

    // Last post result ok
    bool _lastPostResultOk = false;
    std::vector<char, SpiramAwareAllocator<char>> _postResultBuf;

    // System reset callback
    std::function<void()> _systemRestartCallback = nullptr;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Select the most appropriate SysType based on requested SysType name and hardware revision
    void selectMostAppropriateSysType();

    // API System type
    RaftRetCode apiGetSysTypes(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);
    RaftRetCode apiGetSysTypeContent(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);

    // API System settings
    RaftRetCode apiSysTypeGetSettings(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);
    RaftRetCode apiSysTypePostSettings(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);
    RaftRetCode apiSysTypePostSettingsBody(const String& reqStr, const uint8_t *pData, size_t len, 
                        size_t index, size_t total, const APISourceInfo& sourceInfo);
    RaftRetCode apiSysTypeClearSettings(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);
};
