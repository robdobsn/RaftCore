/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Bus Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftArduino.h"
#include "RaftBus.h"
#include "SupervisorStats.h"
#include <list>

// Bus factory creator function
typedef RaftBus* (*RaftBusFactoryCreatorFn)(BusElemStatusCB busElemStatusCB, BusOperationStatusCB busOperationStatusCB);

// Bus factory type
class RaftBusFactoryTypeDef
{
public:
    RaftBusFactoryTypeDef(const String& name, RaftBusFactoryCreatorFn createFn)
    {
        _name = name;
        _createFn = createFn;
    }
    bool isIdenticalTo(const RaftBusFactoryTypeDef& other) const
    {
        if (!_name.equalsIgnoreCase(other._name))
            return false;
        return _createFn == other._createFn;
    }
    bool nameMatch(const String& name) const
    {
        return _name.equalsIgnoreCase(name);
    }
    String _name;
    RaftBusFactoryCreatorFn _createFn;
};

// Bus system
class RaftBusSystem
{
public:
    RaftBusSystem();
    virtual ~RaftBusSystem();

    /// @brief Register a bus type
    /// @param busConstrName Name of the bus type
    /// @param busCreateFn Function to create a bus of this type
    void registerBus(const char* busConstrName, RaftBusFactoryCreatorFn busCreateFn);

    /// @brief Setup buses
    void setup(const char* busConfigName, const RaftJsonIF& config, 
                BusElemStatusCB busElemStatusCB, BusOperationStatusCB busOperationStatusCB);

    /// @brief Loop for buses
    void loop();

    /// @brief Deinit
    void deinit();

    /// @brief Get a bus by name
    /// @param busName Name of the bus
    /// @param allowBusNumberInsteadOfName If true, if bus not found by name, will try to parse busName as a number and find by number
    /// @return Pointer to the bus or nullptr if not found
    RaftBus* getBusByName(const String& busName, bool allowBusNumberInsteadOfName = false) const;

    /// @brief Get a bus by number
    /// @param busNum Number of the bus (starting from RaftDeviceID::BUS_NUM_FIRST_BUS)
    /// @return Pointer to the bus or nullptr if not found
    RaftBus* getBusByNumber(BusNumType busNum) const;

    /// @brief Get the list of buses
    /// @return List of buses
    const std::list<RaftBus*>& getBusList() const
    {
        return _busList;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set virtual pin levels on IO expander (pins must be on the same expander or on GPIO)
    /// @param numPins - number of pins to set
    /// @param pPinNums - array of pin numbers
    /// @param pLevels - array of levels (0 for low)
    /// @param pResultCallback - callback for result when complete/failed
    /// @param pCallbackData - callback data
    /// @return RAFT_OK if successful
    RaftRetCode virtualPinsSet(uint32_t numPins, const int* pPinNums, const uint8_t* pLevels, 
                VirtualPinSetCallbackType pResultCallback = nullptr, void* pCallbackData = nullptr);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get virtual pin level on IO expander
    /// @param pinNum - pin number
    /// @param vPinCallback - callback for virtual pin changes
    /// @param pCallbackData - callback data
    /// @return RAFT_OK if successful
    RaftRetCode virtualPinRead(int pinNum, VirtualPinReadCallbackType vPinCallback, void* pCallbackData = nullptr);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Enable bus slot
    /// @param pBusName - bus name
    /// @param slotNum - slot number (slots are numbered from 1)
    /// @param enablePower - true to enable, false to disable
    /// @param enableData - true to enable data, false to disable
    /// @return RAFT_OK if successful
    RaftRetCode enableSlot(const char* pBusName, uint32_t slotNum, bool enablePower, bool enableData);

private:
    // Debug
    static constexpr const char *MODULE_PREFIX = "RaftBusSystem";

    // List of bus types that can be created
    std::list<RaftBusFactoryTypeDef> _busFactoryTypeList;

    // Bus Factory
    RaftBus* busFactoryCreate(const char* busName, BusElemStatusCB busElemStatusCB, 
                        BusOperationStatusCB busOperationStatusCB);

    // List of buses
    std::list<RaftBus*> _busList;

    // Supervisor statistics for bus stats
    SupervisorStats _supervisorStats;
    uint8_t _supervisorBusFirstIdx = 0;
};

// Access to single instance
extern RaftBusSystem raftBusSystem;