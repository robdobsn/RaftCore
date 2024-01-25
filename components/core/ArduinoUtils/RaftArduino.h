// ArduinoRaft
// Rob Dobson 2012-2023

#pragma once

#include "sdkconfig.h"
#include "esp_idf_version.h"

#ifdef ARDUINO
#include "Arduino.h"
#else
#include <stdio.h>
#include "ArduinoGPIO.h"
#include "ArduinoTime.h"
#include "ArduinoWString.h"
#endif
