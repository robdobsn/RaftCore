/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Bus Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "BusManager.h"
#include "RaftJsonPrefixed.h"
#include "RaftJson.h"

// Warn
#define WARN_ON_NO_BUSES_DEFINED

static const char *MODULE_PREFIX = "BusManager";

// Debug supervisor step (for hangup detection within a service call)
// Uses global logger variables - see logger.h
#define DEBUG_GLOB_HWDEVMAN 2

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BusManager::BusManager()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BusManager::~BusManager()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusManager::setup(const char* busConfigName, const RaftJsonIF& config, 
                BusElemStatusCB busElemStatusCB, BusOperationStatusCB busOperationStatusCB)
{
    // Config prefixed for buses
    RaftJsonPrefixed busesConfig(config, busConfigName);

    // Buses list
    std::vector<String> busesListJSONStrings;
    if (!busesConfig.getArrayElems("buslist", busesListJSONStrings))
    {
#ifdef WARN_ON_NO_BUSES_DEFINED
        LOG_W(MODULE_PREFIX, "No buses defined");
#endif
        return;
    }

    // Iterate bus configs
    for (RaftJson busConfig : busesListJSONStrings)
    {
        // Get bus type
        String busType = busConfig.getString("type", "");

#ifdef DEBUG_BUSES_CONFIGURATION
        LOG_I(MODULE_PREFIX, "setting up bus type %s with %s", busType.c_str(), busConfig.c_str());
#endif

        // Create bus
        BusBase* pNewBus = busFactoryCreate(busType.c_str(), busElemStatusCB, busOperationStatusCB);

        // Setup if valid
        if (pNewBus)
        {
            if (pNewBus->setup(busConfig))
            {
                // Add to bus list
                _busList.push_back(pNewBus);

                // Add to supervisory
                _supervisorStats.add(pNewBus->getBusName().c_str());
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loop
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusManager::loop()
{
    uint32_t busIdx = 0;
    for (BusBase* pBus : _busList)
    {
        if (pBus)
        {
            SUPERVISE_LOOP_CALL(_supervisorStats, _supervisorBusFirstIdx+busIdx, DEBUG_GLOB_HWDEVMAN, pBus->loop())
        }
        busIdx++;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Deinit
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusManager::deinit()
{
    for (BusBase* pBus : _busList)
    {
        if (pBus)
            delete pBus;
    }
    _busList.clear();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Register Bus type
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusManager::registerBus(const char* busConstrName, BusFactoryCreatorFn busCreateFn)
{
    // See if already registered
    BusFactoryTypeDef newElem(busConstrName, busCreateFn);
    for (const BusFactoryTypeDef& el : _busFactoryTypeList)
    {
        if (el.isIdenticalTo(newElem))
            return;
    }
    _busFactoryTypeList.push_back(newElem);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create bus of specified type
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BusBase* BusManager::busFactoryCreate(const char* busConstrName, BusElemStatusCB busElemStatusCB, 
                        BusOperationStatusCB busOperationStatusCB)
{
    for (const auto& el : _busFactoryTypeList)
    {
        if (el.nameMatch(busConstrName))
        {
#ifdef DEBUG_BUS_FACTORY_CREATE
            LOG_I(MODULE_PREFIX, "create bus %s", busConstrName);
#endif
            return el._createFn(busElemStatusCB, busOperationStatusCB);
        }
    }
    return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get bus by name
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BusBase* BusManager::getBusByName(const String& busName)
{
    for (BusBase* pBus : _busList)
    {
        if (pBus && pBus->getBusName().equalsIgnoreCase(busName))
            return pBus;
    }
    return nullptr;
}
