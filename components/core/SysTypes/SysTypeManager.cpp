/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Manager for SysTypes
// Handles selection of system type from a set of JSON alternatives
//
// Rob Dobson 2019-2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "RaftJson.h"
#include "RaftJsonNVS.h"
#include "RestAPIEndpointManager.h"
#include "RaftUtils.h"
#include "SysTypeManager.h"

// #define DEBUG_SYS_TYPE_MANAGER_API
// #define DEBUG_SYS_TYPE_SET_BEST

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
/// @param systemConfig system configuration (based on a JSON document)
/// @param baseSysTypesJson JSON document containing the current base SysType
SysTypeManager::SysTypeManager(RaftJsonIF& systemConfig, RaftJson& sysTypeConfig) :
        _systemConfig(systemConfig),
        _baseSysTypeConfig(sysTypeConfig)
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief setBaseSysTypes the SysTypes to be selected from
/// @param pSysTypeInfoRecs pointer to a vector of SysTypeInfoRec records
/// @note The SysTypeInfoRec records are not copied and must remain valid for the lifetime of this object
///       Once the SysTypeInfoRecs have been set the system type is selected based on the SysType key in
///       the JSON non-volatile storage (if it exists) or the first SysType in the SysTypeInfoRecs
///       (if there is only one SysType) or the SysType that matches the base SysType version string
///       if it is not empty
void SysTypeManager::setBaseSysTypes(const SysTypeInfoRec* pSysTypeInfoRecs, uint16_t numSysTypeInfoRecs)
{
    // Check valid
    if (!pSysTypeInfoRecs)
    {
        LOG_E(MODULE_PREFIX, "setup pSysTypeInfoRecs is NULL");
        return;
    }

    // Store the SysType information records
    _pSysTypeInfoRecs = pSysTypeInfoRecs;
    _numSysTypeInfoRecs = numSysTypeInfoRecs;

    // Set the system type to the best match
    selectBest();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set version of base SysType
/// @param hwRev versionString
/// @note Setting the base SysType version will cause the base SysType to be re-selected based on the
///       best match to the version string and SysType specified in the non-volatile JSON document
///       (if it exists)
void SysTypeManager::setBaseSysTypeVersion(const char* pVersionStr)
{
    // Set the version
    if (pVersionStr)
        _baseSysTypeVersion = pVersionStr;

    // Set the system type to the best match
    selectBest();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get current SysType name
/// @return SysType name which may be from the SysType key in JSON non-volatile storage or from the
///         currently selected SysType from the SysTypeInfoRecs.
String SysTypeManager::getCurrentSysTypeName()
{
    // Check the value from the JSON document
    String sysTypeName = _systemConfig.getString("SysType", "");

    // If this is empty then use record in SysTypeInfoRecs that is currently selected
    if (sysTypeName.length() != 0)
        return sysTypeName;

    // Check if a SysTypeInfoRec has been selected
    if (!_pSysTypeInfoRecs || (_currentlySysTypeInfoRecIdx < 0) || 
                (_currentlySysTypeInfoRecIdx >= (int)_numSysTypeInfoRecs))
        return "";

    // Return SysType name from the selected SysTypeInfoRec
    return _pSysTypeInfoRecs[_currentlySysTypeInfoRecIdx].getSysTypeName();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get a list of SysTypes as a JSON list
/// @return JSON document containing just a list of SysTypes
/// @note the list is generated from the SysTypeInfoRec records passed into setup. The list returned will only
///       include those SysTypes that are valid for the current base SysType version string (unless there is
///       only 1 base SysType in which case it will be returned)
String SysTypeManager::getBaseSysTypesListAsJson()
{

    // Set of SysType names
    std::vector<String> sysTypeNames;

    // Get a JSON formatted list of sysTypes
    String respStr = "[";

    // Add each SysType from the systype info records
    if (_pSysTypeInfoRecs)
    {
        bool isFirst = true;
        for (uint16_t sysTypeIdx = 0; sysTypeIdx < _numSysTypeInfoRecs; sysTypeIdx++)
        {
            // Get SysType info record
            const SysTypeInfoRec& sysTypeInfoRec = _pSysTypeInfoRecs[sysTypeIdx];

            // Get base name
            String recName = sysTypeInfoRec.getSysTypeName();
            if (recName.length() == 0)
                continue;

            // Get version
            String recVersion = sysTypeInfoRec.getSysTypeVersion();

            // Check if valid for this version
            if (!recVersion.equals(_baseSysTypeVersion) && (_numSysTypeInfoRecs != 1) && (_baseSysTypeVersion.length() != 0))
                continue;

            // Check if this SysType is already in the list
            bool alreadyInList = false;
            for (const String& sysTypeName : sysTypeNames)
            {
                if (sysTypeName.equals(recName))
                {
                    alreadyInList = true;
                    break;
                }
            }
            if (alreadyInList)
                continue;

            // Add to list
            sysTypeNames.push_back(recName);

            // Add comma if needed
            if (!isFirst)
                respStr += ",";
            isFirst = false;

            // Append to return string
            respStr += "\"" + recName + "\"";
        }
    }

    respStr += "]";
    return respStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get the JSON document for a base SysType (SysTypes are used for configuration defined by the JSON doc)
/// @param pSysTypeName name of the SysType (if this is nullptr or empty then the current SysType doc is returned)
///                     the information returned will be for the SysType record relating to the current base
///                     base SysType version
/// @param outJsonDoc JSON document to return content in
/// @param append true if the JSON document should be appended to
/// @return bool true if the JSON document was found
bool SysTypeManager::getBaseSysTypeContent(const char* pSysTypeName, String& outJsonDoc, bool append)
{
    // Check for nullptr or blank - in which case return the current JSON doc
    if (!pSysTypeName || (strlen(pSysTypeName) == 0))
    {
        // Get the JSON doc from chained RaftJson object
        const RaftJsonIF* pChainedRaftJson = _systemConfig.getChainedRaftJson();
        if (pChainedRaftJson)
        {
            const char* pJsonDoc = pChainedRaftJson->getJsonDoc();
            if (pJsonDoc)
            {
                if (!append)
                    outJsonDoc.clear();
                outJsonDoc += pJsonDoc;
                return true;
            }
        }

        // Failed to get JSON doc
        return false;
    }

    // Check the SysType info records are valid
    if (!_pSysTypeInfoRecs)
    {
        LOG_E(MODULE_PREFIX, "getJsonDocForSysType no SysTypeInfoRecs");
        return false;
    }

    // Find the requested SysType in the SysTypeInfoRecs - it needs to match both the name and
    // the base SysType version
    for (uint16_t sysTypeIdx = 0; sysTypeIdx < _numSysTypeInfoRecs; sysTypeIdx++)
    {
        const SysTypeInfoRec& sysTypeInfoRec = _pSysTypeInfoRecs[sysTypeIdx];
        String recName = sysTypeInfoRec.getSysTypeName();
        String recVersion = sysTypeInfoRec.getSysTypeVersion();
        if (recName.equals(pSysTypeName) && 
                (recVersion.equals(_baseSysTypeVersion) || (_numSysTypeInfoRecs == 1)) && 
                sysTypeInfoRec.pSysTypeJSONDoc)
        {
            // Get the JSON doc
            if (!append)
                outJsonDoc.clear();
            outJsonDoc += sysTypeInfoRec.pSysTypeJSONDoc;
            return true;
        }
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set the non-volatile document contents
/// @param pJsonDoc JSON document
/// @note This is the JSON document that is stored in non-volatile storage and is used 
///       override configuration settings
bool SysTypeManager::setNonVolatileDocContents(const char* pJsonDoc)
{
    // Set the non-volatile JSON document
    bool rslt = _systemConfig.setJsonDoc(pJsonDoc);

    // Select the best SysType because the non-volatile JSON document may have contained a SysType key
    if (rslt)
        selectBest();
    return rslt;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Add REST API endpoints
/// @param endpointManager endpoint manager
void SysTypeManager::addRestAPIEndpoints(RestAPIEndpointManager& endpointManager)
{
    // Get SysTypes list
    endpointManager.addEndpoint("getSysTypes", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                            std::bind(&SysTypeManager::apiGetSysTypes, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                            "Get list of base system types");

    // Get a SysType config
    endpointManager.addEndpoint("getSysTypeConfiguration", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                            std::bind(&SysTypeManager::apiGetSysTypeContent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                            "Get JSON contents for a named base system type");

    // Post persisted settings - these settings override the underlying (static) SysType settings
    endpointManager.addEndpoint("postsettings", 
                            RestAPIEndpoint::ENDPOINT_CALLBACK, 
                            RestAPIEndpoint::ENDPOINT_POST,
                            std::bind(&SysTypeManager::apiSysTypePostSettings, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                            "Set non-volatile systype config, for system add /reboot to restart after setting the value",
                            "application/json", 
                            NULL,
                            RestAPIEndpoint::ENDPOINT_CACHE_NEVER,
                            NULL, 
                            std::bind(&SysTypeManager::apiSysTypePostSettingsBody, this, 
                                    std::placeholders::_1, std::placeholders::_2, 
                                    std::placeholders::_3, std::placeholders::_4,
                                    std::placeholders::_5, std::placeholders::_6),
                            NULL);

    // Get persisted settings - these are the overridden settings so may be empty
    endpointManager.addEndpoint("getsettings", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                            std::bind(&SysTypeManager::apiSysTypeGetSettings, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                            "Get systype info for system, /getsettings/<filter> where filter is all, nv, base (nv indicates non-volatile) and filter can be blank for all");

    // Clear settings
    endpointManager.addEndpoint("clearsettings", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                            std::bind(&SysTypeManager::apiSysTypeClearSettings, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                            "Clear settings for system /clearsettings");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief API get system types
/// @param reqStr request string
/// @param respStr response string
/// @param sourceInfo source information
RaftRetCode SysTypeManager::apiGetSysTypes(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
#ifdef DEBUG_SYS_TYPE_MANAGER_API
    LOG_I(MODULE_PREFIX, "GetSysTypes");
#endif
    String sysTypesJson = getBaseSysTypesListAsJson();
    // Response
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, ("\"sysTypes\":" + sysTypesJson).c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief API get system type config
/// @param reqStr request string
/// @param respStr response string
/// @param sourceInfo source information
/// @note The systype content matching the base SysType version and requested name will be returned
RaftRetCode SysTypeManager::apiGetSysTypeContent(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
#ifdef DEBUG_SYS_TYPE_MANAGER_API
    LOG_I(MODULE_PREFIX, "apiGetSysTypeContent");
#endif
    String sysTypeName = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);
    String sysTypeJson = "\"sysType\":";
    bool gotOk = getBaseSysTypeContent(sysTypeName.c_str(), sysTypeJson, true);
    if (gotOk)
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, gotOk, sysTypeJson.c_str());
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, gotOk);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get system settings
/// @param reqStr request string
/// @param respStr response string
/// @param sourceInfo source information
RaftRetCode SysTypeManager::apiSysTypeGetSettings(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    // Check argument
    String filterSettings = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);

    // Basic response is the current system type
    String settingsResp = "\"sysType\":\"" + getCurrentSysTypeName() + "\"";
#ifdef DEBUG_SYS_TYPE_MANAGER_API
    LOG_I(MODULE_PREFIX, "apiSysTypeGetSettings filter %s sysType %s baseSysTypeVersion %s", 
                filterSettings.c_str(), getCurrentSysTypeName().c_str(), _baseSysTypeVersion.c_str());
#endif
    if (filterSettings.equalsIgnoreCase("nv") || filterSettings.equalsIgnoreCase("all") || (filterSettings == ""))
    {
        // Get the non-volatile document
        const char* pPersistedJsonDoc = _systemConfig.getJsonDoc();
        // Ensure we have a valid JSON document (check for nullptr and valid JSON start)
        if (pPersistedJsonDoc && (pPersistedJsonDoc[0] == '{' || pPersistedJsonDoc[0] == '['))
        {
            settingsResp += String(",\"nv\":") + pPersistedJsonDoc;
        }
        else
        {
            // Default to empty object if no valid JSON document
            settingsResp += String(",\"nv\":{}");
        }
#ifdef DEBUG_SYS_TYPE_MANAGER_API
        LOG_I(MODULE_PREFIX, "apiSysTypeGetSettings nv %s", pPersistedJsonDoc ? pPersistedJsonDoc : "nullptr");
#endif
    }
    if (filterSettings.equalsIgnoreCase("base") || filterSettings.equalsIgnoreCase("all") || (filterSettings == ""))
    {
        // Check if there is any chaining of RaftJson objects - if so the first in the chain is
        // considered the base JSON doc
        const RaftJsonIF* pBaseRaftJson = _systemConfig.getChainedRaftJson();
        if (pBaseRaftJson)
        {
            // Get the base JSON doc
            const char* pBaseJsonDoc = pBaseRaftJson->getJsonDoc();
            // Ensure we have a valid JSON document (check for nullptr and valid JSON start)
            if (pBaseJsonDoc && (pBaseJsonDoc[0] == '{' || pBaseJsonDoc[0] == '['))
            {
                settingsResp += String(",\"base\":") + pBaseJsonDoc;
            }
            else
            {
                // Default to empty object if no valid JSON document
                settingsResp += String(",\"base\":{}");
            }
        }
        else
        {
            // Default to empty object if no chained RaftJson exists
            settingsResp += String(",\"base\":{}");
        }
#ifdef DEBUG_SYS_TYPE_MANAGER_API
        LOG_I(MODULE_PREFIX, "apiSysTypeGetSettings base added");
#endif
    }
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, settingsResp.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set system settings (completion)
/// @param reqStr request string
/// @param respStr response string
/// @param sourceInfo source information
RaftRetCode SysTypeManager::apiSysTypePostSettings(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    // Note that this is called after the body of the POST is complete
#ifdef DEBUG_SYS_TYPE_MANAGER_API
    LOG_I(MODULE_PREFIX, "PostSettings request %s", reqStr.c_str());
#endif
    String rebootRequired = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);
    if (_lastPostResultOk && rebootRequired.equalsIgnoreCase("reboot"))
    {
        LOG_I(MODULE_PREFIX, "PostSettings rebooting ... request %s", reqStr.c_str());
        if (_systemRestartCallback)
            _systemRestartCallback();
    }

    // Result
    Raft::setJsonBoolResult(reqStr.c_str(), respStr, _lastPostResultOk);
    _lastPostResultOk = false;
    return RaftRetCode::RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set system settings body
/// @param reqStr request string
/// @param pData pointer to data
/// @param len length of data
RaftRetCode SysTypeManager::apiSysTypePostSettingsBody(const String& reqStr, const uint8_t *pData, size_t len, 
                size_t index, size_t total, const APISourceInfo& sourceInfo)
{
    if (len == total)
    {
        // Form the JSON document
        _postResultBuf.assign(pData, pData + len);
        // Make sure it is null-terminated
        if (_postResultBuf[_postResultBuf.size() - 1] != 0)
            _postResultBuf.push_back(0);
        // Store the settings from buffer and clear the buffer
        _lastPostResultOk = setNonVolatileDocContents(_postResultBuf.data());
        _postResultBuf.clear();
#ifdef DEBUG_SYS_TYPE_MANAGER_API
        LOG_I(MODULE_PREFIX, "apiSysTypePostSettingsBody oneblock rslt %s len %d index %d total %d curBufLen %d", 
                    _lastPostResultOk ? "OK" : "FAIL", len, index, total, _postResultBuf.size());
#endif
        return RAFT_OK;
    }

    // Check if first block
    if (index == 0)
        _postResultBuf.clear();

    // Append to the existing buffer
    _postResultBuf.insert(_postResultBuf.end(), pData, pData+len);

    // Check for complete
    if (_postResultBuf.size() == total)
    {
        // Check the buffer is null-terminated
        if (_postResultBuf[_postResultBuf.size() - 1] != 0)
            _postResultBuf.push_back(0);

        // Store the settings from buffer and clear the buffer
        _lastPostResultOk = setNonVolatileDocContents(_postResultBuf.data());
        _postResultBuf.clear();
#ifdef DEBUG_SYS_TYPE_MANAGER_API
        LOG_I(MODULE_PREFIX, "apiSysTypePostSettingsBody multiblock rslt %s len %d index %d total %d", 
                    _lastPostResultOk ? "OK" : "FAIL", len, index, total);
#endif
    }
    else
    {
#ifdef DEBUG_SYS_TYPE_MANAGER_API
        LOG_I(MODULE_PREFIX, "apiSysTypePostSettingsBody partial len %d index %d total %d curBufLen %d", 
            len, index, total, _postResultBuf.size());
#endif
    }
    return RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Clear system settings
/// @param reqStr request string
/// @param respStr response string
/// @param sourceInfo source information
RaftRetCode SysTypeManager::apiSysTypeClearSettings(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    LOG_I(MODULE_PREFIX, "ClearSettings");
    const char* pEmptyObj = "{}";
    bool clearOk = setNonVolatileDocContents(pEmptyObj);
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, clearOk);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief set the best system type based on the requested system type name and base SysType version
void SysTypeManager::selectBest()
{
    // This method sets a pointer to the JSON doc for a base SysType (from the SysTypeInfoRecs) into the chained
    // RaftJson object in the system config.
    // A SysType info rec will only be selected if it matches the base SysType version or if there is only 
    // one base SysType (otherwise, a nullptr will be set into the chained RaftJson object)
    // Assuming one or more SysType info recs match the base SysType version then the one chosen will either be
    // the one matching the SysType key in the non-volatile JSON document or the first one in the list

    // Initially remove the chained document so SysType can only come from the non-volatile JSON document
    _systemConfig.setChainedRaftJson(nullptr);

    // Get the system type from NVS
    String sysTypeName = _systemConfig.getString("SysType", "");

    // Find matching sysType
    int bestValidSysTypeInfoRecIdx = -1;
    for (int sysTypeInfoRecIdx = 0; sysTypeInfoRecIdx < (int)_numSysTypeInfoRecs; sysTypeInfoRecIdx++)
    {
        const SysTypeInfoRec& sysTypeInfoRec = _pSysTypeInfoRecs[sysTypeInfoRecIdx];
        String recName = sysTypeInfoRec.getSysTypeName();
        String recVersion = sysTypeInfoRec.getSysTypeVersion();
        // Check version matches first
        if (!recVersion.equals(_baseSysTypeVersion) && (_numSysTypeInfoRecs != 1) && (_baseSysTypeVersion.length() != 0))
            continue;
        // Check if this is first match
        if (bestValidSysTypeInfoRecIdx < 0)
            bestValidSysTypeInfoRecIdx = sysTypeInfoRecIdx;
        // Check if this is the SysType we are looking for
        if (recName.equals(sysTypeName))
            bestValidSysTypeInfoRecIdx = sysTypeInfoRecIdx;
        }

    // Check if a valid SysType was found
    if (bestValidSysTypeInfoRecIdx < 0)
    {
        LOG_W(MODULE_PREFIX, "selectBest no valid SysType found - numSysTypeInfoRecs %d baseSysTypeVersion %s sysType from NVS %s", 
                    _numSysTypeInfoRecs, _baseSysTypeVersion.c_str(), sysTypeName.c_str());
        return;
    }

    // Get the SysType info rec
    const SysTypeInfoRec& sysTypeInfoRec = _pSysTypeInfoRecs[bestValidSysTypeInfoRecIdx];

    // Set into the main document of the chained RaftJson object
    _baseSysTypeConfig.setSourceStr(sysTypeInfoRec.pSysTypeJSONDoc, false, sysTypeInfoRec.pSysTypeJSONDoc+strlen(sysTypeInfoRec.pSysTypeJSONDoc));

    // Set the chained RaftJson object into the system config
    _systemConfig.setChainedRaftJson(&_baseSysTypeConfig);

    // Record the selected SysType info rec
    _currentlySysTypeInfoRecIdx = bestValidSysTypeInfoRecIdx;

#ifdef DEBUG_SYS_TYPE_SET_BEST
    LOG_I(MODULE_PREFIX, "selectBest selected recName %s recVersion %s curBaseVersion <<<%s>>> jsonDocPtr %p chainedPtr %p chainedJsonDoc %s", 
                sysTypeInfoRec.getSysTypeName().c_str(), sysTypeInfoRec.getSysTypeVersion(), _baseSysTypeVersion.c_str(), 
                sysTypeInfoRec.pSysTypeJSONDoc,
                _systemConfig.getChainedRaftJson(),
                _systemConfig.getChainedRaftJson() ? _systemConfig.getChainedRaftJson()->getJsonDoc() : "null"
                );
#endif
}
