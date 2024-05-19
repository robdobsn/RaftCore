/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Status indicator
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "StatusIndicator.h"
#include "RaftUtils.h"

// Debug
// #define DEBUG_STATUS_INDICATOR_SETUP
// #define DEBUG_STATUS_INDICATOR_CODE
#if defined(DEBUG_STATUS_INDICATOR_CODE) || defined(DEBUG_STATUS_INDICATOR_SETUP)
// Log prefix
static const char *MODULE_PREFIX = "StInd";
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

StatusIndicator::StatusIndicator()
{
}

StatusIndicator::~StatusIndicator()
{
    // Restore pin to input if setup
    if (_isSetup)
    {
        pinMode(_hwPin, INPUT);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void StatusIndicator::setup(const char *pName, int hwPin, bool onLevel, uint32_t onMs, 
                uint32_t shortOffMs, uint32_t longOffMs)
{
    // Check if setup already - if so restore pin to input
    if (_isSetup)
    {
        pinMode(_hwPin, INPUT);
        _isSetup = false;
    }

    // Save settings
    _name = pName;
    _hwPin = hwPin;
    _onLevel = onLevel;
    _onMs = onMs;
    _shortOffMs = shortOffMs;
    _longOffMs = longOffMs;

#ifdef DEBUG_STATUS_INDICATOR_SETUP
    LOG_I(MODULE_PREFIX, "setup name %s pin %d onLevel %d onMs %d shortMs %d longMs %d",
          _name.c_str(), _hwPin, _onLevel, (int)_onMs, (int)_shortOffMs, (int)_longOffMs);
#endif

    // Setup pin
    if (_hwPin >= 0)
    {
        pinMode(_hwPin, OUTPUT);
        digitalWrite(_hwPin, !_onLevel);
        _isSetup = true;
    }

    // Reset state
    _isOn = false;
    _curCodePos = 0;
    _curCode = 0;
    _changeLastMs = millis();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set status code
// See header file for more details
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void StatusIndicator::setStatusCode(int code, uint32_t timeoutMs)
{
    // Ignore if no change or not setup
    if ((_curCode == code) || (!_isSetup))
        return;

#ifdef DEBUG_STATUS_INDICATOR_CODE
    LOG_I(MODULE_PREFIX, "setCode %d curCode %d isSetup %d timeoutMs %d", 
                code, _curCode, _isSetup, (int)timeoutMs);
#endif

    // Set new code
    _curCode = code;
    _curCodePos = 0;
    _changeLastMs = millis();
    _timeoutMs = timeoutMs;
    if (code == 0)
    {
        digitalWrite(_hwPin, !_onLevel);
        _isOn = false;
    }
    else
    {
        digitalWrite(_hwPin, _onLevel);
        _isOn = true;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void StatusIndicator::loop()
{
    // Check if active
    if (!_isSetup)
        return;

    // Code 0 means stay off
    if (_curCode == 0)
        return;

    // Check for timeout
    if ((_timeoutMs > 0) && (Raft::isTimeout(millis(), _changeLastMs, _timeoutMs)))
    {
        setStatusCode(0);
        return;
    }

    // Handle the code
    if (_isOn)
    {
        if (Raft::isTimeout(millis(), _changeLastMs, _onMs))
        {
            _isOn = false;
            digitalWrite(_hwPin, !_onLevel);
            _changeLastMs = millis();
        }
    }
    else
    {
        if (Raft::isTimeout(millis(), _changeLastMs, 
                (_curCodePos == _curCode-1) ? _longOffMs : _shortOffMs))
        {
            _isOn = true;
            digitalWrite(_hwPin, _onLevel);
            _changeLastMs = millis();
            _curCodePos++;
            if (_curCodePos >= _curCode)
                _curCodePos = 0;
        }
    }
}
