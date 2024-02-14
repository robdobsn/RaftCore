
#pragma once

#include "esp_system.h"
#define EVAL_PERF_START(SVar) uint64_t SVar ## _us1 = micros(); uint32_t SVar ## _mem1 = esp_get_free_heap_size();
#define EVAL_PERF_END(SVar) uint32_t SVar ## _us = uint32_t(micros() - SVar ## _us1); uint32_t SVar ## _mem = (SVar ## _mem1 - esp_get_free_heap_size());
#define EVAL_PERF_LOG(SVar, SStr, NLoops) LOG_I(MODULE_PREFIX, "%s: %u us %u bytes", SStr, SVar ## _us / NLoops, SVar ## _mem);
