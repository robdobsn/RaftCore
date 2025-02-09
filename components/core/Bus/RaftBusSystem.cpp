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

// Debug
// #define DEBUG_RAFT_BUS_SYSTEM_SETUP
// #define DEBUG_GET_BUS_BY_NAME
// #define DEBUG_GET_BUS_BY_NAME_DETAIL
// #define DEBUG_BUS_FACTORY_CREATE
// #define DEBUG_BUS_FACTORY_CREATE_DETAIL

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

#ifdef DEBUG_RAFT_BUS_SYSTEM_SETUP
        LOG_I(MODULE_PREFIX, "setting up bus type %s with %s (raftBusSystem %p)", busType.c_str(), busConfig.c_str(), this);
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
        else
        {
            LOG_E(MODULE_PREFIX, "Failed to create bus type %s (pBusSystem %p)", busType.c_str(), this);
        }
    }

#ifdef DEBUG_RAFT_BUS_SYSTEM_SETUP
    LOG_I(MODULE_PREFIX, "setup numBuses %d", _busList.size());
#endif
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
            SUPERVISE_LOOP_CALL(_supervisorStats, _supervisorBusFirstIdx+busIdx, __loggerGlobalDebugValueBusSys, pBus->loop())
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
#ifdef DEBUG_BUS_FACTORY_CREATE_DETAIL
    LOG_I(MODULE_PREFIX, "busFactoryCreate %s numBusTypes %d (pThis %p)", busConstrName, 
                _busFactoryTypeList.size(), this);
#endif    
    for (const auto& el : _busFactoryTypeList)
    {
#ifdef DEBUG_BUS_FACTORY_CREATE_DETAIL
        LOG_I(MODULE_PREFIX, "busFactoryCreate %s checking %s", busConstrName, el._name.c_str());
#endif
        if (el.nameMatch(busConstrName))
        {
            RaftBus* pBus = el._createFn(busElemStatusCB, busOperationStatusCB);
#ifdef DEBUG_BUS_FACTORY_CREATE
            LOG_I(MODULE_PREFIX, "creating bus %s %s", busConstrName, pBus ? "OK" : "FAILED");
#endif
            return pBus;
        }
    }
#ifdef DEBUG_BUS_FACTORY_CREATE
    LOG_I(MODULE_PREFIX, "creating bus %s not found", busConstrName);
#endif
    return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get bus by name
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftBus* RaftBusSystem::getBusByName(const String& busName)
{
#ifdef DEBUG_GET_BUS_BY_NAME_DETAIL
    LOG_I(MODULE_PREFIX, "getBusByName %s numBuses %d (raftBusSystem %p)", 
                busName.c_str(), _busList.size(), this);
#endif
    for (RaftBus* pBus : _busList)
    {
#ifdef DEBUG_GET_BUS_BY_NAME_DETAIL
        LOG_I(MODULE_PREFIX, "getBusByName %s checking %s", busName.c_str(), pBus->getBusName().c_str());
#endif
        if (pBus && pBus->getBusName().equalsIgnoreCase(busName))
        {
#ifdef DEBUG_GET_BUS_BY_NAME
            LOG_I(MODULE_PREFIX, "getBusByName %s found", busName.c_str());
#endif
            return pBus;
        }
    }
#ifdef DEBUG_GET_BUS_BY_NAME
    LOG_I(MODULE_PREFIX, "getBusByName %s not found", busName.c_str());
#endif
    return nullptr;
}
