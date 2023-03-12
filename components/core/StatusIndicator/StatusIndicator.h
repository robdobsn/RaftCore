/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Status indicator
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <ArduinoOrAlt.h>

class StatusIndicator
{
public:
    StatusIndicator();
    virtual ~StatusIndicator();
    void setup(const char *pName, int hwPin, bool onLevel, uint32_t onMs, uint32_t shortOffMs, uint32_t longOffMs);
    void setStatusCode(int code, uint32_t timeoutMs = 0);
    void service();

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
};
