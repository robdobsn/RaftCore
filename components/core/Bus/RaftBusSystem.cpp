/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Bus System
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftBusSystem.h"
#include "RaftJsonPrefixed.h"
#include "RaftJson.h"

// Warn
#define WARN_ON_NO_BUSES_DEFINED

static const char *MODULE_PREFIX = "RaftBusSystem";

// Debug supervisor step (for hangup detection within a service call)
// Uses global logger variables - see logger.h
#define DEBUG_GLOB_HWDEVMAN 2

// Global object
RaftBusSystem raftBusSystem;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftBusSystem::RaftBusSystem()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftBusSystem::~RaftBusSystem()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftBusSystem::setup(const char* busConfigName, const RaftJsonIF& config, 
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
        RaftBus* pNewBus = busFactoryCreate(busType.c_str(), busElemStatusCB, busOperationStatusCB);

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

void RaftBusSystem::loop()
{
    uint32_t busIdx = 0;
    for (RaftBus* pBus : _busList)
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

void RaftBusSystem::deinit()
{
    for (RaftBus* pBus : _busList)
    {
        if (pBus)
            delete pBus;
    }
    _busList.clear();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Register Bus type
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftBusSystem::registerBus(const char* busConstrName, RaftBusFactoryCreatorFn busCreateFn)
{
    // See if already registered
    RaftBusFactoryTypeDef newElem(busConstrName, busCreateFn);
    for (const RaftBusFactoryTypeDef& el : _busFactoryTypeList)
    {
        if (el.isIdenticalTo(newElem))
            return;
    }
    _busFactoryTypeList.push_back(newElem);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create bus of specified type
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftBus* RaftBusSystem::busFactoryCreate(const char* busConstrName, BusElemStatusCB busElemStatusCB, 
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

RaftBus* RaftBusSystem::getBusByName(const String& busName)
{
    for (RaftBus* pBus : _busList)
    {
        if (pBus && pBus->getBusName().equalsIgnoreCase(busName))
            return pBus;
    }
    return nullptr;
}
