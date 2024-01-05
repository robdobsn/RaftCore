/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftJsonNVS
// JSON persisted to non-volatile storage
//
// Rob Dobson 2016-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftJsonNVS.h"

static const char *MODULE_PREFIX = MODULE_PREFIX;

// #define WARN_ON_NVS_JSON_DOC_TOO_SHORT
// #define WARN_ON_NVS_JSON_DOC_TOO_LONG
// #define WARN_ON_NVS_ACCESS_FAILURES
// #define DEBUG_NVS_CONFIG_READ_WRITE

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set new contents for the JSON document
/// @param pJsonDoc the new JSON document
/// @return true if the JSON document was successfully set, false if the JSON document was too long
/// @note This is used by RaftJsonIF implementations that support changes to the JSON document
///       Implementations that store to NVS or similar may persist the new JSON document
bool RaftJsonNVS::setJsonDoc(const char* pJsonDoc)
{
    // Set json doc
    uint32_t jsonDocStrLen = strlen(pJsonDoc);

    // Check if a max length is set
    if (_jsonMaxlen > 0)
    {
        // Check if the string is too long
        if (jsonDocStrLen > _jsonMaxlen)
        {
#ifdef WARN_ON_NVS_JSON_DOC_TOO_LONG
            LOG_W(MODULE_PREFIX, "setNewContent TOO_LONG namespace %s read: len(%d) maxlen %d", 
                        _nvsNamespace.c_str(), jsonDocStrLen, _jsonMaxlen);
#endif
            return false;
        }
    }

    // Set the document
    setJsonDoc(pJsonDoc, jsonDocStrLen);

    // Write the value to NVS
    uint32_t nvsHandle = 0;
    esp_err_t err = nvs_open(_nvsNamespace.c_str(), NVS_READWRITE, &nvsHandle);
    if (err)
    {
#ifdef WARN_ON_NVS_ACCESS_FAILURES
        LOG_W(MODULE_PREFIX, "nvs_open FAIL ns %s error %s", _nvsNamespace.c_str(), nvs_error(err));
#endif
        return false;
    }

    // Set the new string into the NVS handle
    err = nvs_set_str(nvsHandle, KEY_NAME_FOR_JSON_DOC, pJsonDoc);
    if (err)
    {
#ifdef WARN_ON_NVS_ACCESS_FAILURES
        LOG_W(MODULE_PREFIX, "nvs_set_str FAIL ns %s error %s", _nvsNamespace.c_str(), nvs_error(err));
#endif
        return false;
    }

    // Commit
    err = nvs_commit(nvsHandle);
    if (err)
    {
#ifdef WARN_ON_NVS_ACCESS_FAILURES
        LOG_E(MODULE_PREFIX, "nvs_commit FAIL ns %s error %s", _nvsNamespace.c_str(), nvs_error(err));
#endif
        return false;
    }

    // Close NVS
    nvs_close(nvsHandle);

    // Call config change callbacks
    for (int i = 0; i < _jsonChangeCallbacks.size(); i++)
    {
        if (_jsonChangeCallbacks[i])
        {
            (_jsonChangeCallbacks[i])();
        }
    }

#ifdef DEBUG_NVS_CONFIG_READ_WRITE
    LOG_W(MODULE_PREFIX, "setNewContent OK namespace %s len %d maxlen %d",
                _nvsNamespace.c_str(), jsonDocStrLen, _jsonMaxlen);
#endif

    // Ok
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Read the JSON document from NVS
/// @return void
void RaftJsonNVS::readJsonDocFromNVS()
{
    // Open NVS
    uint32_t nvsHandle = 0;
    esp_err_t err = nvs_open(_nvsNamespace.c_str(), NVS_READONLY, &nvsHandle);
    if (err)
    {
#ifdef WARN_ON_NVS_ACCESS_FAILURES
        LOG_W(MODULE_PREFIX, "nvs_open FAIL ns %s %s", _nvsNamespace.c_str(), nvs_error(err));
#endif
        return;
    }

    // Vector for string
    std::vector<char, SpiramAwareAllocator<char>> jsonDoc;

    // Call nvs_get_str twice, first to get length
    size_t stringLen = 0;
    err = nvs_get_str(nvsHandle, KEY_NAME_FOR_JSON_DOC, nullptr, &stringLen);
    if (err)
    {
#ifdef WARN_ON_NVS_ACCESS_FAILURES
        LOG_W(MODULE_PREFIX, "nvs_get_str len FAILED ns %s error %s", 
                    _nvsNamespace.c_str(), nvs_error(err));
#endif
        nvs_close(nvsHandle);
        return;
    }

    // Allocate space (the value returned by nvs_get_str includes the null terminator)
    jsonDoc.resize(stringLen);
    stringLen = jsonDoc.size();
    err = nvs_get_str(nvsHandle, KEY_NAME_FOR_JSON_DOC, jsonDoc.data(), &stringLen);
    if (err)
    {
#ifdef WARN_ON_NVS_ACCESS_FAILURES
        LOG_W(MODULE_PREFIX, "nvs_get_str data FAILED ns %s error %s", 
                    _nvsNamespace.c_str(), nvs_error(err));
#endif
        nvs_close(nvsHandle);
        return;
    }

    // Close NVS
    nvs_close(nvsHandle);

    // Set the base class JSON doc
    setJsonDoc(jsonDoc.data(), jsonDoc.size() > 0 ? jsonDoc.size() - 1 : 0);

    // Stats
    _statsCallsToGetNVStr++;

#ifdef DEBUG_NVS_CONFIG_READ_WRITE
    // Debug
    LOG_W(MODULE_PREFIX, "getStrFromNVS OK ns %s len %d maxlen %d statsCallsToGetNVStr %d", 
                _nvsNamespace.c_str(), jsonDoc.size(), _jsonMaxlen, _statsCallsToGetNVStr);
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set the JSON document into the base class
/// @param pJsonDoc the JSON document
/// @param jsonDocStrLen the length of the JSON document (excludes terminator)
void RaftJsonNVS::setJsonDoc(const char* pJsonDoc, uint32_t jsonDocStrLen)
{
    // Empty JSON doc
    const char* pEmptyJsonDoc = "{}";

    // Check the jsonDoc is at least 2 chars long (must at least be "{}")
    if (jsonDocStrLen < 2)
    {
#ifdef WARN_ON_NVS_JSON_DOC_TOO_SHORT
        LOG_W(MODULE_PREFIX, "setJsonDocFromNVS TOO_SHORT namespace %s read: len(%d) <<<%s>>> maxlen %d", 
                    _nvsNamespace.c_str(), jsonDocStrLen, 
                    pJsonDoc, _jsonMaxlen);
#endif
        // Set to an empty JSON object
        pJsonDoc = pEmptyJsonDoc;
        jsonDocStrLen = strlen(pJsonDoc);
    }

    // Check if the string is too long
    if (jsonDocStrLen > _jsonMaxlen)
    {
#ifdef WARN_ON_NVS_JSON_DOC_TOO_SHORT
        LOG_W(MODULE_PREFIX, "setJsonDocFromNVS TOO_LONG namespace %s read: len(%d) maxlen %d", 
                    _nvsNamespace.c_str(), jsonDocStrLen, _jsonMaxlen);
#endif
        // Set to an empty JSON object
        pJsonDoc = pEmptyJsonDoc;
        jsonDocStrLen = strlen(pJsonDoc);
    }

    // Store value in parent object (makes a copy of the string)
    setSourceStr(pJsonDoc, true, jsonDocStrLen);
}
