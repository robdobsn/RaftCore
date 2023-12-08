/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DebounceButton.cpp
// Callback on button press via a hardware pin
//
// Rob Dobson 2018-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DebounceButton.h"
#include <RaftUtils.h>
#include "Logger.h"
#include "RaftArduino.h"
#include <driver/gpio.h>

// Constructor
DebounceButton::DebounceButton()
{
    _buttonPin = -1;
    _buttonActiveLevel = 0;
    _lastCheckMs = 0;
    _firstPass = true;
    _lastStableVal = false;
    _timeInPresentStateMs = 0;
    _activeRepeatTimeMs = DEFAULT_ACTIVE_REPEAT_MS;
    _debounceMs = DEFAULT_PIN_DEBOUNCE_MS;
    _callback = nullptr;
    _repeatCount = 0;
}

DebounceButton::~DebounceButton()
{
    if (_buttonPin >= 0)
    {
        gpio_reset_pin((gpio_num_t)_buttonPin);
    }
}

// Setup
void DebounceButton::setup(int pin, bool pullup, bool activeLevel, 
        DebounceButtonCallback cb, uint32_t debounceMs, uint16_t activeRepeatTimeMs)
{
    // Settings
    _buttonPin = pin;
    _buttonActiveLevel = activeLevel;
    _debounceMs = debounceMs;
    _activeRepeatTimeMs = activeRepeatTimeMs;

    // State
    _lastCheckMs = millis();
    _firstPass = true;
    _lastStableVal = 0;
    _timeInPresentStateMs = 0;
    _callback = cb;

    // Setup the input pin
    if (_buttonPin >= 0)
    {
        // GPIO config
        gpio_config_t io_conf;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = (1ULL << _buttonPin);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
        gpio_config(&io_conf);
    }
}

void DebounceButton::service()
{
    // Pin valid check
    if (_buttonPin < 0)
        return;

    // Check time for check
    uint64_t curMs = millis();
    if (Raft::isTimeout(curMs, _lastCheckMs, PIN_CHECK_MS))
    {
        // Accumulate ms elapsed since state of button (pressed/unpressed) changed
        _timeInPresentStateMs += (curMs - _lastCheckMs);

        // Last check update
        _lastCheckMs = curMs;

        // Check first time we've monitored
        if (_firstPass)
        {
            _lastStableVal = (gpio_get_level((gpio_num_t)_buttonPin) != 0) == _buttonActiveLevel;
            _firstPass = false;
            return;
        }

        // Check for change of state
        bool curVal = (gpio_get_level((gpio_num_t)_buttonPin) != 0) == _buttonActiveLevel;

        // Check if changed
        if (curVal != _lastStableVal)
        {
            // See if at threshold for detection
            if (_timeInPresentStateMs > _debounceMs)
            {
                // Set active/inactive
                _lastStableVal = curVal;

                // Call callback
                if (_callback)
                    _callback(curVal, _timeInPresentStateMs, 0);

                // Reset time in state
                _timeInPresentStateMs = 0;
                _lastRepeatTimeMs = curMs;
                _repeatCount = 0;
            }
        }
        else
        {
            // Check if active
            if (curVal)
            {
                if ((_activeRepeatTimeMs != 0) && (Raft::isTimeout(curMs, _lastRepeatTimeMs, _activeRepeatTimeMs)))
                {
                    _lastRepeatTimeMs = curMs;
                    _repeatCount++;
                    // Call callback
                    if (_callback)
                        _callback(curVal, _timeInPresentStateMs, _repeatCount);
                }
            }
        }
    }
}
