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
#include "VirtualPinResult.h"
#include "RaftBusConsts.h"

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
            if (pNewBus->setup(RaftDeviceID::BUS_NUM_FIRST_BUS + _busList.size(), busConfig))
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
/// @brief Get a bus by name
/// @param busName Name of the bus
/// @param allowBusNumberInsteadOfName If true, if bus not found by name, will try to parse busName as a number and find by number
/// @return Pointer to the bus or nullptr if not found
RaftBus* RaftBusSystem::getBusByName(const String& busName, bool allowBusNumberInsteadOfName) const
{
    // First try to find by name
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

    // If not found and allowed, try to find by number
    if (allowBusNumberInsteadOfName)
    {
        char* endPtr = nullptr;
        long busNum = strtol(busName.c_str(), &endPtr, 10);
        if (endPtr != busName.c_str() && *endPtr == '\0')
        {
            return getBusByNumber(busNum);
        }
    }

    // Not found
    return nullptr;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get a bus by number
/// @param busNum Number of the bus (starting from RaftDeviceID::BUS_NUM_FIRST_BUS)
/// @return Pointer to the bus or nullptr if not found
RaftBus* RaftBusSystem::getBusByNumber(BusNumType busNum) const
{
#ifdef DEBUG_GET_BUS_BY_NAME_DETAIL
    LOG_I(MODULE_PREFIX, "getBusByNumber %d numBuses %d (raftBusSystem %p)", 
                busNum, _busList.size(), this);
#endif
    for (RaftBus* pBus : _busList)
    {
#ifdef DEBUG_GET_BUS_BY_NAME_DETAIL
        LOG_I(MODULE_PREFIX, "getBusByNumber %d checking %s", busNum, pBus->getBusName().c_str());
#endif
        if (pBus && pBus->getBusNum() == busNum)
        {
#ifdef DEBUG_GET_BUS_BY_NAME
            LOG_I(MODULE_PREFIX, "getBusByNumber %d found", busNum);
#endif
            return pBus;
        }
    }
#ifdef DEBUG_GET_BUS_BY_NAME
    LOG_I(MODULE_PREFIX, "getBusByNumber %d not found", busNum);
#endif
    return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set virtual pin levels on IO expander (pins must be on the same expander or on GPIO)
/// @param numPins - number of pins to set
/// @param pPinNums - array of pin numbers
/// @param pLevels - array of levels (0 for low)
/// @param pResultCallback - callback for result when complete/failed
/// @param pCallbackData - callback data
/// @return RAFT_OK if successful
RaftRetCode RaftBusSystem::virtualPinsSet(uint32_t numPins, const int* pPinNums, const uint8_t* pLevels, 
            VirtualPinSetCallbackType pResultCallback, void* pCallbackData)
{
    // Check valid
    if (!pPinNums || !pLevels)
        return RAFT_INVALID_DATA;

    // See if any bus handles this
    for (RaftBus* pBus : _busList)
    {
        if (pBus)
        {
            RaftRetCode retc = pBus->virtualPinsSet(numPins, pPinNums, pLevels, pResultCallback, pCallbackData);
            if (retc == RAFT_OK)
                return retc;
        }
    }

    // Not handled so use regular GPIO
    for (uint32_t idx = 0; idx < numPins; idx++)
    {
        pinMode(pPinNums[idx], OUTPUT);
        digitalWrite(pPinNums[idx], pLevels[idx] ? HIGH : LOW);
    }

    // Check for callback
    if (pResultCallback)
    {
        pResultCallback(pCallbackData, RAFT_OK);
    }
    return RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get virtual pin level on IO expander
/// @param pinNum - pin number
/// @param vPinCallback - callback for virtual pin changes
/// @param pCallbackData - callback data
RaftRetCode RaftBusSystem::virtualPinRead(int pinNum, VirtualPinReadCallbackType vPinCallback, void* pCallbackData)
{
    // See if any bus handles this
    for (RaftBus* pBus : _busList)
    {
        if (pBus)
        {
            RaftRetCode retc = pBus->virtualPinRead(pinNum, vPinCallback, pCallbackData);
            if (retc == RAFT_OK)
                return retc;
        }
    }

    // Not handled so use regular GPIO
    if (vPinCallback)
        vPinCallback(pCallbackData, VirtualPinResult(pinNum, digitalRead(pinNum), RAFT_OK));

    // Done
    return RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Enable bus slot
/// @param pBusName - bus name
/// @param slotNum - slot number (slots are numbered from 1)
/// @param enablePower - true to enable, false to disable
/// @param enableData - true to enable data, false to disable
/// @return RAFT_OK if successful
RaftRetCode RaftBusSystem::enableSlot(const char* pBusName, uint32_t slotNum, bool enablePower, bool enableData)
{
    // Get the bus
    RaftBus* pBus = getBusByName(pBusName, true);
    if (!pBus)
        return RAFT_BUS_INVALID;

    // Enable slot
    return pBus->enableSlot(slotNum, enablePower, enableData);
}

