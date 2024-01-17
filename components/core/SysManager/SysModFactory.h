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
typedef SysModBase* (*SysModCreateFn)(const char* pSysModName);

class SysModFactory
{
public:
    SysModFactory();
    ~SysModFactory();

    // Register a SysMod
    void registerSysMod(const char* pSysModClassName, SysModCreateFn pSysModCreateFn);

private:

    class SysModClassDef
    {
    public:
        SysModClassDef(const char* pSysModClassName, SysModCreateFn pSysModCreateFn)
        {
            _sysModClassName = pSysModClassName;
            _pSysModCreateFn = pSysModCreateFn;
        }
        String _sysModClassName;
        SysModCreateFn _pSysModCreateFn;
    };

    // List of SysMod classes
    std::list<SysModClassDef> _sysModClassDefs;
};
