/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ConfigMulti
// Configuration handling multiple underlying config objects in a hierarchy
//
// Rob Dobson 2020-2022
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "ConfigMulti.h"
#include "Logger.h"

// #define DEBUG_CONFIG_MULTI_DETAIL

// Logging
#ifdef DEBUG_CONFIG_MULTI_DETAIL
static const char* MODULE_PREFIX = "ConfigMulti";
#endif

ConfigMulti::ConfigMulti()
{
}

ConfigMulti::ConfigMulti(const ConfigMulti& other) : ConfigBase(other)
{
    _configsList = other._configsList;
}

ConfigMulti& ConfigMulti::operator=(const ConfigMulti& other)
{
    _configsList = other._configsList;
    return *this;
}

ConfigMulti::~ConfigMulti()
{
}

void ConfigMulti::addConfig(ConfigBase* pConfig, const char* prefix, bool isMutable)
{
    if (!pConfig)
        return;
    ConfigRec rec(pConfig, prefix, isMutable);
    _configsList.push_back(rec);
}

// Write configuration data to last (in order of adding) mutable element
bool ConfigMulti::writeConfig(const String& configJSONStr)
{
    // Find last mutable element
    for (std::list<ConfigRec>::reverse_iterator rit=_configsList.rbegin(); rit != _configsList.rend(); ++rit)
    {
        if (rit->_isMutable)
            return rit->_pConfig->writeConfig(configJSONStr);
    }

    // Write base if not already done
    return ConfigBase::writeConfig(configJSONStr);
}

String ConfigMulti::getString(const char *dataPath, const char *defaultValue, const char* pPrefix) const
{
    // Get base value
    String retVal = ConfigBase::getString(dataPath, defaultValue, pPrefix);

    // Iterate other configs in the order added
    for (const ConfigRec& rec : _configsList)
    {
        if ((rec._prefix.length() > 0) && (dataPath[0] != '/'))
        {
            String prefixedPath = rec._prefix + "/";
            if (pPrefix)
                prefixedPath += String(pPrefix) + "/";
            prefixedPath += String(dataPath);
            retVal = rec._pConfig->getString(prefixedPath.c_str(), retVal);
#ifdef DEBUG_CONFIG_MULTI_DETAIL
            LOG_I(MODULE_PREFIX, "getString prefix %s dataPath %s retVal %s prefixedPath %s %s", 
                        rec._prefix.c_str(), dataPath, retVal.c_str(), prefixedPath.c_str(), rec._isMutable ? "mutable" : "immutable");
#endif
        }
        else
        {
            retVal = rec._pConfig->getString(dataPath, retVal, pPrefix);
#ifdef DEBUG_CONFIG_MULTI_DETAIL
            LOG_I(MODULE_PREFIX, "getString prefix %s dataPath %s retVal %ld %s", 
                        rec._prefix.c_str(), dataPath, retVal.c_str(), rec._isMutable ? "mutable" : "immutable");
#endif
        }
    }
    return retVal;
}

long ConfigMulti::getLong(const char *dataPath, long defaultValue, const char* pPrefix) const
{
    // Get base value
    long retVal = ConfigBase::getLong(dataPath, defaultValue, pPrefix);

    // Iterate other configs in the order added
    for (const ConfigRec& rec : _configsList)
    {
        if (!rec._pConfig)
            continue;
        if (rec._prefix.length() > 0)
        {
            String prefixedPath = rec._prefix + "/";
            if (pPrefix)
                prefixedPath += String(pPrefix) + "/";
            prefixedPath += String(dataPath);
            retVal = rec._pConfig->getLong(prefixedPath.c_str(), retVal);
#ifdef DEBUG_CONFIG_MULTI_DETAIL
            LOG_I(MODULE_PREFIX, "getLong prefix %s dataPath %s retVal %ld prefixedPath %s %s", 
                        rec._prefix.c_str(), dataPath, retVal, prefixedPath.c_str(), rec._isMutable ? "mutable" : "immutable");
#endif
        }
        else
        {
            retVal = rec._pConfig->getLong(dataPath, retVal, pPrefix);
#ifdef DEBUG_CONFIG_MULTI_DETAIL
            LOG_I(MODULE_PREFIX, "getLong prefix %s dataPath %s retVal %ld %s", 
                        rec._prefix.c_str(), dataPath, retVal, rec._isMutable ? "mutable" : "immutable");
#endif
        }
    }
    return retVal;    
}

bool ConfigMulti::getBool(const char *dataPath, bool defaultValue, const char* pPrefix) const
{
    return getLong(dataPath, defaultValue, pPrefix) != 0;
}

double ConfigMulti::getDouble(const char *dataPath, double defaultValue, const char* pPrefix) const
{
    // Get base value
    double retVal = ConfigBase::getDouble(dataPath, defaultValue, pPrefix);

    // Iterate other configs in the order added
    for (const ConfigRec& rec : _configsList)
    {
        if (rec._prefix.length() > 0)
        {
            String prefixedPath = rec._prefix + "/";
            if (pPrefix)
                prefixedPath += String(pPrefix) + "/";
            prefixedPath += String(dataPath);
            retVal = rec._pConfig->getDouble(prefixedPath.c_str(), retVal);
#ifdef DEBUG_CONFIG_MULTI_DETAIL
            LOG_I(MODULE_PREFIX, "getDouble prefix %s dataPath %s retVal %f prefixedPath %s %s", 
                        rec._prefix.c_str(), dataPath, retVal, prefixedPath.c_str(), rec._isMutable ? "mutable" : "immutable");
#endif
        }
        else
        {
            retVal = rec._pConfig->getDouble(dataPath, retVal, pPrefix);
#ifdef DEBUG_CONFIG_MULTI_DETAIL
            LOG_I(MODULE_PREFIX, "getString prefix %s dataPath %s retVal %f %s", 
                        rec._prefix.c_str(), dataPath, retVal, rec._isMutable ? "mutable" : "immutable");
#endif
        }
    }
    return retVal;    
}

bool ConfigMulti::getArrayElems(const char *dataPath, std::vector<String>& strList, const char* pPrefix) const
{
    // Get base value
    bool retVal = ConfigBase::getArrayElems(dataPath, strList, pPrefix);

    // Iterate other configs in the order added
    for (const ConfigRec& rec : _configsList)
    {
        if (rec._prefix.length() > 0)
        {
            String prefixedPath = rec._prefix + "/";
            if (pPrefix)
                prefixedPath += String(pPrefix) + "/";
            prefixedPath += String(dataPath);
            retVal |= rec._pConfig->getArrayElems(prefixedPath.c_str(), strList);
#ifdef DEBUG_CONFIG_MULTI_DETAIL
            LOG_I(MODULE_PREFIX, "getArrayElems prefix %s dataPath %s retOk %d numElems %d prefixedPath %s %s", 
                        rec._prefix.c_str(), dataPath, retVal, strList.size(), prefixedPath.c_str(), rec._isMutable ? "mutable" : "immutable");
#endif
        }
        else
        {
            retVal |= rec._pConfig->getArrayElems(dataPath, strList, pPrefix);
#ifdef DEBUG_CONFIG_MULTI_DETAIL
            LOG_I(MODULE_PREFIX, "getArrayElems prefix %s dataPath %s retOk %d numElems %d %s", 
                        rec._prefix.c_str(), dataPath, retVal, strList.size(), rec._isMutable ? "mutable" : "immutable");
#endif
        }
    }
    return retVal;    
}

// Contains
bool ConfigMulti::contains(const char *dataPath, const char* pPrefix) const
{
    // Check base
    if (ConfigBase::contains(dataPath, pPrefix))
        return true;

    // Iterate other configs in the order added
    for (const ConfigRec& rec : _configsList)
    {
        if (!rec._pConfig)
            continue;
        if (rec._prefix.length() > 0)
        {
            String prefixedPath = rec._prefix + "/";
            if (pPrefix)
                prefixedPath += String(pPrefix) + "/";
            prefixedPath += String(dataPath);
            if (rec._pConfig->contains(prefixedPath.c_str()))
                return true;
        }
        else
        {
            if (rec._pConfig->contains(dataPath, pPrefix))
                return true;
        }
    }
    return false;   
}


// Register change callback
void ConfigMulti::registerChangeCallback(ConfigChangeCallbackType configChangeCallback)
{
    // Find last mutable element
    for (std::list<ConfigRec>::reverse_iterator rit=_configsList.rbegin(); rit != _configsList.rend(); ++rit)
    {
        if (rit->_isMutable)
            return rit->_pConfig->registerChangeCallback(configChangeCallback);
    }
}