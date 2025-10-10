/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Factory for SysMods (System Modules)
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <list>
#include <vector>

class RaftSysMod;

// SysMod create function
typedef RaftSysMod* (*SysModCreateFn)(const char* pSysModName, RaftJsonIF& sysConfig);

class SysModFactory
{
public:

    ///////////////////////////////////////////////////////////////////////
    /// @brief Register a SysMod in the factory
    /// @param pClassName Name of the SysMod class
    /// @param pCreateFn Function to create the SysMod
    /// @param alwaysEnable If true then the SysMod is always enabled
    /// @param pDependencyListCSV Comma separated list of dependencies
    void registerSysMod(const char* pClassName, SysModCreateFn pCreateFn, bool alwaysEnable = false, const char* pDependencyListCSV = nullptr)
    {
        if (!pClassName || !pCreateFn)
            return;
        sysModClassDefs.push_back(SysModClassDef(pClassName, pCreateFn, alwaysEnable, pDependencyListCSV));
    }

    class SysModClassDef
    {
    public:
        ///////////////////////////////////////////////////////////////////////
        /// @brief Constructor
        /// @param pClassName Name of the SysMod class
        /// @param pCreateFn Function to create the SysMod
        /// @param alwaysEnable If true then the SysMod is always enabled
        /// @param pDependencyListCSV Comma separated list of dependencies
        SysModClassDef(const char* pClassName, SysModCreateFn pCreateFn, bool alwaysEnable, const char* pDependencyListCSV)
        {
            this->name = pClassName;
            this->pCreateFn = pCreateFn;
            this->alwaysEnable = alwaysEnable;
            if (pDependencyListCSV)
            {
                // Split dependency list on commas
                String depListStr(pDependencyListCSV);
                unsigned int startPos = 0;
                while (startPos < depListStr.length())
                {
                    int commaPos = depListStr.indexOf(',', startPos);
                    if (commaPos < 0)
                        commaPos = depListStr.length();
                    String depName = depListStr.substring(startPos, commaPos);
                    dependencyList.push_back(depName);
                    startPos = commaPos + 1;
                }
            }
        }
        String name;
        SysModCreateFn pCreateFn = nullptr;
        bool alwaysEnable = false;
        std::vector<String> dependencyList;
    };

    // List of SysMod classes
    std::list<SysModClassDef> sysModClassDefs;
};
