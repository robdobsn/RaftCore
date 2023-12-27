/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ConfigNVS
// Configuration persisted to non-volatile storage
//
// Rob Dobson 2016-2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <ConfigBase.h>
class ArPreferences;

class ConfigNVS : public ConfigBase
{
public:
    ConfigNVS(const char* configNamespace, int configMaxlen);
    virtual ~ConfigNVS();

    // Set the config data from a static source - note that only the
    // pointer is stored so this data MUST be statically allocated
    void setStaticConfigData(const char* pStaticJSONConfigStr)
    {
        _staticConfigData.pDataStrJSONStatic = pStaticJSONConfigStr;
#ifdef FEATURE_NO_CACHE_FLASH_CONFIG_STR
        _staticConfigData.enableCaching = false;
#endif
    }

    // Get config raw string
    virtual String getConfigString() const override final
    {
        // If the non-volatile store is not valid then return the static config
        if (!_nonVolatileStoreValid && _staticConfigData.pDataStrJSONStatic)
            return _staticConfigData.pDataStrJSONStatic;
        return ConfigBase::getConfigString();
    }

    // Get persisted config
    String getPersistedConfig() const
    {
        if (_nonVolatileStoreValid)
            if (ConfigBase::getConfigString().length() > 0)
                return ConfigBase::getConfigString();
        return "{}";
    }

    // Get static config
    String getStaticConfig() const
    {
        if (_staticConfigData.pDataStrJSONStatic)
            return _staticConfigData.pDataStrJSONStatic;
        return "{}";
    }

    // Clear
    virtual void clear() override final;

    // Initialise
    virtual bool setup() override final;

    // Write configuration string
    virtual bool writeConfig(const String& configJSONStr) override final;

    // Register change callback
    virtual void registerChangeCallback(ConfigChangeCallbackType configChangeCallback) override final;

    // Get string
    virtual String getString(const char *dataPath, const char *defaultValue, const char* pPrefix = nullptr) const override final;

    // Get long
    virtual long getLong(const char *dataPath, long defaultValue, const char* pPrefix = nullptr) const override final;

    // Get bool
    virtual bool getBool(const char *dataPath, bool defaultValue, const char* pPrefix = nullptr) const override final;

    // Get double
    virtual double getDouble(const char *dataPath, double defaultValue, const char* pPrefix = nullptr) const override final;

    // Get array elems
    virtual bool getArrayElems(const char *dataPath, std::vector<String>& strList, const char* pPrefix = nullptr) const override final;

    // Check if config contains key
    virtual bool contains(const char *dataPath, const char* pPrefix = nullptr) const override final;

private:
    // Namespace used for ArPreferences lib
    String _configNamespace;

    // ArPreferences instance
    ArPreferences* _pPreferences = nullptr;

    // List of callbacks on change of config
    std::vector<ConfigChangeCallbackType> _configChangeCallbacks;

    // Non-volatile store valid
    bool _nonVolatileStoreValid = true;

    // JSON data for statically allocated storage which is used when
    // a key is not found in the non-volatile store
    JSONDataAndCache _staticConfigData;

    // Get non-volatile config str
    String getNVConfigStr() const;

    // Stats on calls to getNVConfigStr
    uint32_t _callsToGetNVStr = 0;
};
