// RaftCore Main Include File
// Rob Dobson 2024

#pragma once

#include <stdint.h>
#include <vector>
#include <functional>
#include <list>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#include "sdkconfig.h"
#include "esp_intr_alloc.h"
#include "esp_idf_version.h"
#endif

#include "RaftUtils.h"
#include "RaftArduino.h"
#include "RaftJson.h"
#include "RaftJsonPrefixed.h"
#include "RaftJsonNVS.h"
#include "ConfigPinMap.h"
#include "DebounceButton.h"
#include "RestAPIEndpointManager.h"
#include "RaftSysMod.h"
#include "RaftThreading.h"
#include "ThreadSafeQueue.h"
#include "RaftDevice.h"
#include "DeviceManager.h"

#ifdef ESP_PLATFORM
#include "LEDPixels.h"
#include "RaftMQTTClient.h"
#include "RaftCoreApp.h"
#define FUNCTION_DECORATOR_IRAM_ATTR IRAM_ATTR
#else
#define FUNCTION_DECORATOR_IRAM_ATTR
#endif
