/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DebugGlobals
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DebugGlobals.h"
#include "sdkconfig.h"

#ifdef DEBUG_USING_GLOBAL_VALUES
volatile int32_t __loggerGlobalDebugValueSysMan = -1;
volatile int32_t __loggerGlobalDebugValueDevMan = -1;
volatile int32_t __loggerGlobalDebugValueBusSys = -1;

String Raft::getDebugGlobalsJson(bool includeOuterBrackets)
{
    char outStr[100];
    snprintf(outStr, sizeof(outStr), "\"SysMan\":%d,\"DevMan\":%d,\"BusSys\":%d",
        (int)__loggerGlobalDebugValueSysMan,
        (int)__loggerGlobalDebugValueDevMan,
        (int)__loggerGlobalDebugValueBusSys);
    if (includeOuterBrackets)
        return R"({"globs":)" + String(outStr) + "}";
    return outStr;
}

#endif
