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
#include "Logger.h"
#include "RaftJson.h"

#ifdef ESP_PLATFORM

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

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Debug function to show information about NVS keys
    /// @param showContents if true then show the contents of the NVS keys
    static void debugShowNVSInfo(bool showContents);

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Initialise Non-Volatile Storage
    /// @param eraseIfCorrupt if true then erase the NVS if it is corrupt
    /// @return true if the NVS was initialised successfully
    static bool initNVS(bool eraseIfCorrupt);

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get the NVS namespace
    /// @return the NVS namespace
    const String& getNVSNamespace() const
    {
        return _nvsNamespace;
    }

private:
    // Key name in NVS
    const char* KEY_NAME_FOR_JSON_DOC = "JSON";

    // Helpers
    void readJsonDocFromNVS();
    void updateJsonDoc(const char* pJsonDoc, uint32_t jsonDocStrLen);

    // Namespace used for NVS library
    String _nvsNamespace;

    // List of callbacks on change of config
    std::vector<RaftJsonChangeCallbackType> _jsonChangeCallbacks;

    // Non-volatile store valid
    bool _nonVolatileStoreValid = true;

    // Max length of JSON document
    uint32_t _jsonMaxlen = 0;

    // NVS initialised
    static bool _nvsInitialised;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get the string value of an NVS entry
    /// @param pKey the key
    /// @param strVec the vector to store the string in
    /// @return true if the string was successfully retrieved
    static bool getStrFromNVS(const char* pNamespace, const char* pKey, std::vector<char, SpiramAwareAllocator<char>>& strVec);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get the string name of the type of an NVS entry
    /// @param nvsType the NVS type enumeration
    /// @return the string name of the type
    static const char* getNVSTypeName(nvs_type_t nvsType);

    // Debug
    static constexpr const char* MODULE_PREFIX = "RaftJsonNVS";
};

#else // ESP_PLATFORM

typedef RaftJson RaftJsonNVS;

#endif // ESP_PLATFORM
