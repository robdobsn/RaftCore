/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// SupervisorStats
// Statistics for supervisor of module execution
//
// Rob Dobson 2018-2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include <inttypes.h>
#include "SupervisorStats.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SupervisorStats::SupervisorStats() : _summaryInfo(NUM_SLOWEST_TO_TRACK)
{
    init();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialise
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SupervisorStats::init()
{
    _summaryInfo.clear();
    _outerLoopInfo.clear();
    _moduleList.clear();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clear stats
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SupervisorStats::clear()
{
    _summaryInfo.clear();
    _outerLoopInfo.clear();
    for (ModInfo &modInfo : _moduleList)
    {
        modInfo.execTimer.clear();
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add a module
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t SupervisorStats::add(const char *name)
{
    if (_moduleList.size() > MAX_MODULES)
        return 0;
    ModInfo modInfo(name);
    uint32_t idxAdded = _moduleList.size();
    _moduleList.push_back(modInfo);
    return idxAdded;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Execution of a module started
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SupervisorStats::execStarted(uint32_t modIdx)
{
    if (modIdx >= _moduleList.size())
        return;
    _moduleList[modIdx].execTimer.started();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Execution of a module ended
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SupervisorStats::execEnded(uint32_t modIdx)
{
    if (modIdx >= _moduleList.size())
        return;
    _moduleList[modIdx].execTimer.ended();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Outer loop start
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SupervisorStats::outerLoopStarted()
{
    _outerLoopInfo.startLoop();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Outer loop end
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SupervisorStats::outerLoopEnded()
{
    _outerLoopInfo.endLoop();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Calculate supervisory stats
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SupervisorStats::calculate()
{
    // Calculate outer loop times
    _summaryInfo.updateOuterLoopStats(_outerLoopInfo);

    // Calculate slowest modules
    _summaryInfo.updateSlowestModules(_moduleList);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get summary string
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String SupervisorStats::getSummaryString() const
{
    // Average loop time
    String outerLoopStr;
    if (_summaryInfo._totalLoops > 0)
    {
        char outerLoopTmp[150];
        snprintf(outerLoopTmp, sizeof(outerLoopTmp), R"("avgUs":%4.2f,"maxUs":%lu,"minUs":%lu)", 
                _summaryInfo._loopTimeAvgUs,
                _summaryInfo._loopTimeMaxUs,
                _summaryInfo._loopTimeMinUs);
        outerLoopStr = outerLoopTmp;
    }

    // Find slowest modules
    char slowestStr[300];
    strlcpy(slowestStr, "", sizeof(slowestStr));

    // Slowest loop activity
    bool isFirst = true;
    for (int modIdx : _summaryInfo._nThSlowestModIdxVec)
    {
        if ((modIdx < 0) || (modIdx > _moduleList.size()))
            break;
        if (!_moduleList[modIdx].execTimer.valid())
            break;
        uint32_t strPos = strnlen(slowestStr, sizeof(slowestStr));
        if (sizeof(slowestStr) <= strPos + 1)
            break;
#if defined(EXEC_TIMER_INCLUDE_CPU_TIME) && defined(ESP_PLATFORM)
        // Include both elapsed and CPU times for ESP32
        snprintf(slowestStr + strPos, sizeof(slowestStr) - strPos,
                 isFirst ? R"("slowUs":{"%s":{"e":%)" PRIu64 R"(,"c":%)" PRIu64 R"(})" : R"(,"%s":{"e":%)" PRIu64 R"(,"c":%)" PRIu64 R"(})",
                 _moduleList[modIdx]._modName.c_str(),
                 _moduleList[modIdx].execTimer.getMaxUs(),
                 _moduleList[modIdx].execTimer.getMaxCpuUs());
#else
        // Only elapsed time for non-ESP32
        snprintf(slowestStr + strPos, sizeof(slowestStr) - strPos,
                 isFirst ? R"("slowUs":{"%s":%)" PRIu64 : R"(,"%s":%)" PRIu64,
                 _moduleList[modIdx]._modName.c_str(),
                 _moduleList[modIdx].execTimer.getMaxUs());
#endif
        isFirst = false;
    }
    if (strnlen(slowestStr, sizeof(slowestStr)) > 0)
    {
        if (outerLoopStr.length() > 0)
            outerLoopStr += ",";
        outerLoopStr += String(slowestStr) + "}";
    }
    return "{" + outerLoopStr + "}";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Update slowest modules
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SupervisorStats::SummaryInfo::updateSlowestModules(const std::vector<ModInfo>& moduleList)
{
    // Fill list of slowest modules with invalid values initially
    for (uint32_t nthIdx = 0; nthIdx < _nThSlowestModIdxVec.size(); nthIdx++)
        _nThSlowestModIdxVec[nthIdx] = -1;

    // Fill list in order
    for (uint32_t nthIdx = 0; nthIdx < _nThSlowestModIdxVec.size(); nthIdx++)
    {
        // Find nth slowest
        int bestIdx = -1;
        uint32_t bestTimeUs = UINT32_MAX;
        for (uint32_t modIdx = 0; modIdx < moduleList.size(); modIdx++)
        {
            // Check if already in the list
            bool alreadyInList = false;
            for (uint32_t chkIdx = 0; chkIdx < _nThSlowestModIdxVec.size(); chkIdx++)
            {
                if (_nThSlowestModIdxVec[chkIdx] < 0)
                    break;
                if (_nThSlowestModIdxVec[chkIdx] == modIdx)
                {
                    alreadyInList = true;
                    break;
                }
            }
            if (alreadyInList)
                continue;

            // Check if better (slower) than previous
            if ((bestIdx == -1) || (bestTimeUs < moduleList[modIdx].execTimer.getMaxUs()))
            {
                bestIdx = modIdx;
                bestTimeUs = moduleList[modIdx].execTimer.getMaxUs();
            }
        }
        if (bestIdx < 0)
            break;

        // Store
        _nThSlowestModIdxVec[nthIdx] = bestIdx;
    }

}
