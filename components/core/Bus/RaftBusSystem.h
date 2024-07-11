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
    /// @return Pointer to the bus or nullptr if not found
    RaftBus* getBusByName(const String& busName);

    /// @brief Get the list of buses
    /// @return List of buses
    const std::list<RaftBus*>& getBusList() const
    {
        return _busList;
    }

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