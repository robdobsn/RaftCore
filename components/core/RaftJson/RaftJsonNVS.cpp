/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftJsonNVS
// JSON persisted to non-volatile storage
//
// Rob Dobson 2016-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <vector>
#include <list>
#include "Logger.h"
#include "RaftJsonNVS.h"
#include "nvs_flash.h"
#include "esp_idf_version.h"

static const char *MODULE_PREFIX = "RaftJsonNVS";

// #define WARN_ON_NVS_JSON_DOC_TOO_SHORT
// #define WARN_ON_NVS_JSON_DOC_TOO_LONG
#define WARN_ON_NVS_ACCESS_FAILURES
// #define DEBUG_NVS_READ_WRITE_OPERATIONS
// #define DEBUG_NVS_READ_WRITE_OPERATIONS_VERBOSE
// #define WARN_ON_NVS_NAMESPACE_NOT_FOUND_FAILURES

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Statics
bool RaftJsonNVS::_nvsInitialised = RaftJsonNVS::initNVS(true);

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
            LOG_W(MODULE_PREFIX, "setJsonDoc TOO_LONG namespace %s read: len(%d) maxlen %d", 
                        _nvsNamespace.c_str(), jsonDocStrLen, _jsonMaxlen);
#endif
            return false;
        }
    }

    // Update the document
    updateJsonDoc(pJsonDoc, jsonDocStrLen);

    // Write the value to NVS
    uint32_t nvsHandle = 0;
    esp_err_t err = nvs_open(_nvsNamespace.c_str(), NVS_READWRITE, &nvsHandle);
    if (err != ESP_OK)
    {
#ifdef WARN_ON_NVS_ACCESS_FAILURES
        LOG_W(MODULE_PREFIX, "setJsonDoc nvs_open FAIL ns %s error %s", 
                        _nvsNamespace.c_str(), esp_err_to_name(err));
#endif
        return false;
    }

    // Set the new string into the NVS handle
    err = nvs_set_str(nvsHandle, KEY_NAME_FOR_JSON_DOC, pJsonDoc);
    if (err != ESP_OK)
    {
#ifdef WARN_ON_NVS_ACCESS_FAILURES
        LOG_W(MODULE_PREFIX, "setJsonDoc nvs_set_str FAIL ns %s error %d", 
                        _nvsNamespace.c_str(), esp_err_to_name(err));
#endif
        return false;
    }

    // Commit
    err = nvs_commit(nvsHandle);
    if (err != ESP_OK)
    {
#ifdef WARN_ON_NVS_ACCESS_FAILURES
        LOG_E(MODULE_PREFIX, "setJsonDoc nvs_commit FAIL ns %s error %s", 
                        _nvsNamespace.c_str(), esp_err_to_name(err));
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

#ifdef DEBUG_NVS_READ_WRITE_OPERATIONS
    LOG_I(MODULE_PREFIX, "setJsonDoc OK namespace %s len %d maxlen %d",
                _nvsNamespace.c_str(), jsonDocStrLen, _jsonMaxlen);
#endif
#ifdef DEBUG_NVS_READ_WRITE_OPERATIONS_VERBOSE
    {
        // Debug
        debugShowNVSInfo(true);
        // Show the NVS contents
        std::vector<char, SpiramAwareAllocator<char>> jsonDoc;
        if (RaftJsonNVS::getStrFromNVS(_nvsNamespace.c_str(), KEY_NAME_FOR_JSON_DOC, jsonDoc))
        {
            LOG_I(MODULE_PREFIX, "setJsonDoc debug ns %s key %s value %s", 
                        _nvsNamespace.c_str(), KEY_NAME_FOR_JSON_DOC, jsonDoc.data());
        }
    }
#endif

    // Ok
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Read the JSON document from NVS
/// @return void
void RaftJsonNVS::readJsonDocFromNVS()
{
    std::vector<char, SpiramAwareAllocator<char>> jsonDoc;

    // Read the value from NVS
    if (RaftJsonNVS::getStrFromNVS(_nvsNamespace.c_str(), KEY_NAME_FOR_JSON_DOC, jsonDoc))
    {

        // Update the base class JSON doc (note that this handles 0 length strings)
        updateJsonDoc(jsonDoc.data(), jsonDoc.size() > 0 ? jsonDoc.size() - 1 : 0);

#ifdef DEBUG_NVS_READ_WRITE_OPERATIONS
        // Debug
        LOG_I(MODULE_PREFIX, "readJsonDocFromNVS OK ns %s key %s len %d maxlen %d", 
                    _nvsNamespace.c_str(), KEY_NAME_FOR_JSON_DOC, jsonDoc.size(), _jsonMaxlen);
#endif
    }
    else
    {
        // Update the base class JSON doc (note that this handles 0 length strings)
        updateJsonDoc(nullptr, 0);
    
#ifdef DEBUG_NVS_READ_WRITE_OPERATIONS
        // Debug
        LOG_W(MODULE_PREFIX, "readJsonDocFromNVS FAIL ns %s key %s", 
                    _nvsNamespace.c_str(), KEY_NAME_FOR_JSON_DOC);
#endif
    }

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get the string value of an NVS entry
/// @param pKey the key
/// @param strVec the vector to store the string in
/// @return true if the string was successfully retrieved
bool RaftJsonNVS::getStrFromNVS(const char* pNamespace, const char* pKey, 
                std::vector<char, SpiramAwareAllocator<char>>& strVec)
{
    // Open NVS
    uint32_t nvsHandle = 0;
    esp_err_t err = nvs_open(pNamespace, NVS_READONLY, &nvsHandle);
    if (err != ESP_OK)
    {
#ifdef WARN_ON_NVS_NAMESPACE_NOT_FOUND_FAILURES
        LOG_W(MODULE_PREFIX, "getStrFromNVS nvs_open FAIL ns %s error %s", 
                        pNamespace, esp_err_to_name(err));
#endif
        return false;
    }

    // Call nvs_get_str twice, first to get length
    size_t stringLen = 0;
    err = nvs_get_str(nvsHandle, pKey, nullptr, &stringLen);
    if (err != ESP_OK)
    {
#ifdef WARN_ON_NVS_ACCESS_FAILURES
        LOG_W(MODULE_PREFIX, "getStrFromNVS nvs_get_str len FAILED ns %s error %s", 
                        pNamespace, esp_err_to_name(err));
#endif
        nvs_close(nvsHandle);
        return false;
    }

    // Allocate space (the value returned by nvs_get_str includes the null terminator)
    strVec.resize(stringLen);
    stringLen = strVec.size();
    err = nvs_get_str(nvsHandle, pKey, strVec.data(), &stringLen);
    if (err != ESP_OK)
    {
#ifdef WARN_ON_NVS_ACCESS_FAILURES
        LOG_W(MODULE_PREFIX, "getStrFromNVS nvs_get_str data FAILED ns %s error %s", 
                        pNamespace, esp_err_to_name(err));
#endif
        nvs_close(nvsHandle);
        return false;
    }

    // Ensure string is null-terminated
    if (strVec.size() > 0)
        strVec[strVec.size() - 1] = 0;

    // Close NVS
    nvs_close(nvsHandle);
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Update the JSON document into the base class
/// @param pJsonDoc the JSON document
/// @param jsonDocStrLen the length of the JSON document (excludes terminator)
void RaftJsonNVS::updateJsonDoc(const char* pJsonDoc, uint32_t jsonDocStrLen)
{
    // Empty JSON doc
    const char* pEmptyJsonDoc = "{}";

    // Check the jsonDoc is at least 2 chars long (must at least be "{}")
    if (jsonDocStrLen < 2)
    {
#ifdef WARN_ON_NVS_JSON_DOC_TOO_SHORT
        LOG_W(MODULE_PREFIX, "updateJsonDoc TOO_SHORT namespace %s read: len(%d) maxlen %d", 
                    _nvsNamespace.c_str(), jsonDocStrLen, _jsonMaxlen);
#endif
        // Set to an empty JSON object
        pJsonDoc = pEmptyJsonDoc;
        jsonDocStrLen = strlen(pJsonDoc);
    }

    // Check if the string is too long
    else if ((_jsonMaxlen != 0) && (jsonDocStrLen > _jsonMaxlen))
    {
#ifdef WARN_ON_NVS_JSON_DOC_TOO_LONG
        LOG_W(MODULE_PREFIX, "updateJsonDoc TOO_LONG namespace %s read: len(%d) maxlen %d", 
                    _nvsNamespace.c_str(), jsonDocStrLen, _jsonMaxlen);
#endif
        // Set to an empty JSON object
        pJsonDoc = pEmptyJsonDoc;
        jsonDocStrLen = strlen(pJsonDoc);
    }

    // Store value in parent object (makes a copy of the string)
    setSourceStr(pJsonDoc, true, pJsonDoc+jsonDocStrLen);

#ifdef DEBUG_NVS_READ_WRITE_OPERATIONS_VERBOSE
    // Debug
    LOG_I(MODULE_PREFIX, "updateJsonDoc OK namespace %s len(%d) maxlen %d ... %s", 
                _nvsNamespace.c_str(), jsonDocStrLen, _jsonMaxlen, pJsonDoc);
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Debug function to show information about NVS keys
void RaftJsonNVS::debugShowNVSInfo(bool showContents)
{
    std::list<nvs_entry_info_t> nvsEntries;
    nvs_iterator_t it = NULL;
    esp_err_t res = ESP_OK;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    res = nvs_entry_find("nvs", nullptr, NVS_TYPE_ANY, &it);
#else
    it = nvs_entry_find("nvs", NULL, NVS_TYPE_ANY);
    res = it == NULL ? ESP_FAIL : ESP_OK;
#endif
    while (res == ESP_OK)
    {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info); // Can omit error check if parameters are guaranteed to be non-NULL
        LOG_I(MODULE_PREFIX, "debugShowNVSInfo namespace %.*s key %.*s type %s (%d)", 
                    NVS_NS_NAME_MAX_SIZE, info.namespace_name, 
                    NVS_KEY_NAME_MAX_SIZE, info.key, 
                    getNVSTypeName(info.type), info.type);
        // Extract strings if required
        if (showContents)
            nvsEntries.push_back(info);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        res = nvs_entry_next(&it);
#else
        it = nvs_entry_next(it);
        res = it == NULL ? ESP_FAIL : ESP_OK;
#endif
    }
    nvs_release_iterator(it);

    LOG_I(MODULE_PREFIX, "debugShowNVSInfo namespace %s", "DONE");
    delay(1000);

    // Show strings
    if (showContents)
    {
        for (auto it = nvsEntries.begin(); it != nvsEntries.end(); ++it)
        {
            nvs_entry_info_t info = *it;
            if (info.type == NVS_TYPE_STR)
            {
                std::vector<char, SpiramAwareAllocator<char>> strVec;
                if (RaftJsonNVS::getStrFromNVS(info.namespace_name, info.key, strVec))
                {
                    LOG_I(MODULE_PREFIX, "debugShowNVSInfo STR namespace %.*s key %.*s value %s", 
                                NVS_NS_NAME_MAX_SIZE, info.namespace_name, 
                                NVS_KEY_NAME_MAX_SIZE, info.key, 
                                strVec.data());
                }
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get the string name of the type of an NVS entry
/// @param nvsType the NVS type enumeration
/// @return the string name of the type
const char* RaftJsonNVS::getNVSTypeName(nvs_type_t nvsType)
{
    switch (nvsType)
    {
        case NVS_TYPE_I8:
            return "NVS_TYPE_I8";
        case NVS_TYPE_U8:
            return "NVS_TYPE_U8";
        case NVS_TYPE_I16:
            return "NVS_TYPE_I16";
        case NVS_TYPE_U16:
            return "NVS_TYPE_U16";
        case NVS_TYPE_I32:
            return "NVS_TYPE_I32";
        case NVS_TYPE_U32:
            return "NVS_TYPE_U32";
        case NVS_TYPE_I64:
            return "NVS_TYPE_I64";
        case NVS_TYPE_U64:
            return "NVS_TYPE_U64";
        case NVS_TYPE_STR:
            return "NVS_TYPE_STR";
        case NVS_TYPE_BLOB:
            return "NVS_TYPE_BLOB";
        default:
            return "NVS_TYPE_UNKNOWN";
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Initialise Non-Volatile Storage
/// @param eraseIfCorrupt if true then erase the NVS if it is corrupt
/// @return true if the NVS was initialised successfully
bool RaftJsonNVS::initNVS(bool eraseIfCorrupt)
{
    // Initialize NVS
    esp_err_t nvsInitResult = nvs_flash_init();
    if (nvsInitResult == ESP_OK)
    {
        // Debug
        LOG_I(MODULE_PREFIX, "nvs_flash_init() OK");
        return true;
    }

    // Error message
    LOG_E(MODULE_PREFIX, "nvs_flash_init() failed with error %s (%d)", esp_err_to_name(nvsInitResult), nvsInitResult);

    // Clear flash if required
    if (eraseIfCorrupt && ((nvsInitResult == ESP_ERR_NVS_NO_FREE_PAGES) || (nvsInitResult == ESP_ERR_NVS_NEW_VERSION_FOUND)))
    {
        esp_err_t flashEraseResult = nvs_flash_erase();
        if (flashEraseResult != ESP_OK)
        {
            LOG_E(MODULE_PREFIX, "nvs_flash_erase() failed with error %s (%d)", 
                            esp_err_to_name(flashEraseResult), flashEraseResult);
        }
        nvsInitResult = nvs_flash_init();
        if (nvsInitResult != ESP_OK)
        {
            // Error message
            LOG_W(MODULE_PREFIX, "nvs_flash_init() failed a second time with error %s (%d)", 
                            esp_err_to_name(nvsInitResult), nvsInitResult);
            return false;
        }
    }
    return nvsInitResult == ESP_OK;
}
