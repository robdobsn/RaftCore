/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Debug Globals
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <stdio.h>
#include "ArduinoWString.h"

#ifdef DEBUG_USING_GLOBAL_VALUES
extern volatile int32_t __loggerGlobalDebugValueSysMan;
extern volatile int32_t __loggerGlobalDebugValueDevMan;
extern volatile int32_t __loggerGlobalDebugValueBusSys;

namespace Raft
{
    String getDebugGlobalsJson(bool includeOuterBrackets);
};

#endif
