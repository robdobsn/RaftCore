// ArduinoRaft
// Rob Dobson 2012-2023

#pragma once

#ifdef ARDUINO
#include "Arduino.h"
#else
#include <stdio.h>
#include "ArduinoGPIO.h"
#include "ArduinoTime.h"
#include "ArduinoWString.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "esp_idf_version.h"
#include "esp_attr.h"
#else
#define IRAM_ATTR
#endif
#endif
