/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Factory for SysMods (System Modules)
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <list>
#include "RaftArduino.h"

class SysModBase;

// SysMod create function
typedef SysModBase* (*SysModCreateFn)(const char* pSysModName, RaftJsonIF& sysConfig);

class SysModFactory
{
public:
    // Register a SysMod
    void registerSysMod(const char* pSysModClassName, SysModCreateFn pSysModCreateFn, uint8_t priority1to10, bool defaultEn)
    {
        sysModClassDefs.push_back(SysModClassDef(pSysModClassName, pSysModCreateFn, priority1to10, defaultEn));
    }

    class SysModClassDef
    {
    public:
        SysModClassDef(const char* pSysModClassName, SysModCreateFn pSysModCreateFn, uint8_t priority1to10, bool defaultEn)
        {
            name = pSysModClassName;
            pCreateFn = pSysModCreateFn;
            defaultEnabled = defaultEn;
            this->priority1to10 = priority1to10;
        }
        String name;
        SysModCreateFn pCreateFn = nullptr;
        bool defaultEnabled = false;
        uint8_t priority1to10 = 0;
    };

    // List of SysMod classes
    std::list<SysModClassDef> sysModClassDefs;

    // Priority values
    static const uint8_t PRIORITY_HIGHEST = 1;
    static const uint8_t PRIORITY_LOWEST = 10;
};
