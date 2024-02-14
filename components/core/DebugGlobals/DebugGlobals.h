/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Debug Globals
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include "RaftArduino.h"

#define DEBUG_GLOBAL_VALUE
#ifdef DEBUG_GLOBAL_VALUE
extern volatile int32_t __loggerGlobalDebugValue0;
extern volatile int32_t __loggerGlobalDebugValue1;
extern volatile int32_t __loggerGlobalDebugValue2;
extern volatile int32_t __loggerGlobalDebugValue3;
extern volatile int32_t __loggerGlobalDebugValue4;
#define DEBUG_GLOB_VAR_NAME(x) __loggerGlobalDebugValue ## x
#endif

class DebugGlobals
{
public:
    static String getDebugJson(bool includeOuterBrackets)
    {
        char outStr[100];
        snprintf(outStr, sizeof(outStr), "[%ld,%ld,%ld,%ld,%ld]", 
            (long)__loggerGlobalDebugValue0,
            (long)__loggerGlobalDebugValue1,
            (long)__loggerGlobalDebugValue2,
            (long)__loggerGlobalDebugValue3,
            (long)__loggerGlobalDebugValue4);
        if (includeOuterBrackets)
            return R"({"globs":)" + String(outStr) + "}";
        return outStr;
    }
};