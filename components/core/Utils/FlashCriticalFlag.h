/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// FlashCriticalFlag
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define FLASH_CRIT_DATA_ATTR DRAM_ATTR
#else
#define FLASH_CRIT_DATA_ATTR
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Global flag set during flash critical sections (readable from ISRs).
extern FLASH_CRIT_DATA_ATTR volatile bool g_flashCriticalActive;

#ifdef __cplusplus
}
#endif
