/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftJsonNVS
// JSON persisted to non-volatile storage
//
// Rob Dobson 2016-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include "nvs.h"
#include "Logger.h"
#include "RaftJson.h"

class RaftJsonNVS : public RaftJson
{
public:
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Constructor
    /// @param nvsNamespace the namespace to use for the NVS library ArPreferences
    /// @param jsonMaxlen the maximum length of the JSON document (0 means no limit)
    RaftJsonNVS(const char* nvsNamespace, int jsonMaxlen = 0) :
        _nvsNamespace(nvsNamespace),
        _jsonMaxlen(jsonMaxlen)
    {
        // Load from NVS
        readJsonDocFromNVS();
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Destructor
    virtual ~RaftJsonNVS()
    {
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Register a callback for JSON change - used by RaftJsonIF implementations that support
    ///        changes to the JSON document
    /// @param jsonChangeCallback the callback to be called when the JSON document changes
    virtual void registerChangeCallback(RaftJsonChangeCallbackType configChangeCallback) override final
    {
        // Save callback if valid and not already present
        if (configChangeCallback)
        {
            // Would have been nice to check if the pointer is already in the list but for some reason
            // == isn't implemented on two std::function objects
            _jsonChangeCallbacks.push_back(configChangeCallback);
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set new contents for the JSON document
    /// @param pJsonDoc the new JSON document
    /// @return true if the JSON document was successfully set, false if the JSON document was too long
    /// @note This is used by RaftJsonIF implementations that support changes to the JSON document
    ///       Implementations that store to NVS or similar may persist the new JSON document
    virtual bool setJsonDoc(const char* pJsonDoc) override;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get max length of JSON document
    /// @return the maximum length of the JSON document (0 means no limit)
    virtual uint32_t getMaxJsonLenOr0ForNoLimit() const
    {
        return _jsonMaxlen;
    }


//     // Set the config data from a static source - note that only the
//     // pointer is stored so this data MUST be statically allocated
//     void setStaticConfigData(const char* pStaticJSONConfigStr)
//     {
//         _staticConfigData.pDataStrJSONStatic = pStaticJSONConfigStr;
// #ifdef FEATURE_NO_CACHE_FLASH_CONFIG_STR
//         _staticConfigData.enableCaching = false;
// #endif
//     }

//     // Get config raw string
//     virtual String getConfigString() const override final
//     {
//         // If the non-volatile store is not valid then return the static config
//         if (!_nonVolatileStoreValid && _staticConfigData.pDataStrJSONStatic)
//             return _staticConfigData.pDataStrJSONStatic;
//         return ConfigBase::getConfigString();
//     }

//     // Get persisted config
//     String getPersistedConfig() const
//     {
//         if (_nonVolatileStoreValid)
//             if (ConfigBase::getConfigString().length() > 0)
//                 return ConfigBase::getConfigString();
//         return "{}";
//     }

//     // Get static config
//     String getStaticConfig() const
//     {
//         if (_staticConfigData.pDataStrJSONStatic)
//             return _staticConfigData.pDataStrJSONStatic;
//         return "{}";
//     }

    // // Clear
    // virtual void clear() override final;

    // // Initialise
    // virtual bool setup() override final;

private:
    // Key name in NVS
    const char* KEY_NAME_FOR_JSON_DOC = "JSON";

    // Helpers
    void readJsonDocFromNVS();
    void setJsonDoc(const char* pJsonDoc, uint32_t jsonDocStrLen);

    // Namespace used for NVS library
    String _nvsNamespace;

    // List of callbacks on change of config
    std::vector<RaftJsonChangeCallbackType> _jsonChangeCallbacks;

    // Non-volatile store valid
    bool _nonVolatileStoreValid = true;

    // Max length of JSON document
    uint32_t _jsonMaxlen = 0;

    // Stats on calls to getStrFromNVS
    mutable uint32_t _statsCallsToGetNVStr = 0;

};
