// RaftCore Main Include File
// Rob Dobson 2024

#pragma once

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#include "sdkconfig.h"
#include "esp_intr_alloc.h"
#include "esp_idf_version.h"
#endif

#include <stdint.h>
#include <vector>
#include "RaftUtils.h"
#include "RaftArduino.h"
#include "RaftDevice.h"
#include "RaftJson.h"
#include "RaftJsonPrefixed.h"
#include "RaftJsonNVS.h"
#include "ConfigPinMap.h"
#include "DebounceButton.h"
#include "DeviceManager.h"
#include "RestAPIEndpointManager.h"
#include "RaftSysMod.h"
#include "ThreadSafeQueue.h"

#ifdef ESP_PLATFORM
#include "LEDPixels.h"
#include "RaftMQTTClient.h"
#include "RaftCoreApp.h"
#define FUNCTION_DECORATOR_IRAM_ATTR IRAM_ATTR
#else
#define FUNCTION_DECORATOR_IRAM_ATTR
#endif