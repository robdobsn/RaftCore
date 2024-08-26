/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Status indicator
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftArduino.h"

class StatusIndicator
{
public:
    StatusIndicator();
    virtual ~StatusIndicator();
    void setup(const char *pName, int hwPin, bool onLevel, uint32_t onMs, uint32_t shortOffMs, uint32_t longOffMs);

    // Set status code
    // The indicator provides a series of pulses with short gaps between them followed by a long gap
    // The code indicates the number of short gaps
    // A code of 0 means off
    // A code of 1 means a single pulse followed by a long gap
    // A code of 2 means two pulses (short gap between) followed by a long gap
    // A code of 3 means three pulses (short gaps between) followed by a long gap, etc
    // The timeout returns to code of 0 after the specified time
    void setStatusCode(int code, uint32_t timeoutMs = 0);
    void loop();

private:
    // Settings
    String _name;
    int _hwPin = -1;
    bool _onLevel = false;
    uint32_t _onMs = 0;
    uint32_t _longOffMs = 0;
    uint32_t _shortOffMs = 0;

    // Setup flag
    bool _isSetup = false;

    // State
    int _curCode = 0;
    int _curCodePos = 0;
    bool _isOn = false;
    uint32_t _changeLastMs = 0;
    uint32_t _timeoutMs = 0;

    // Debug
    static constexpr const char* MODULE_PREFIX = "StInd";
};
