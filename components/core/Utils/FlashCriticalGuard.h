/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// FlashCriticalGuard
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "FlashCriticalSectionIF.h"

class FlashCriticalGuard
{
public:
    FlashCriticalGuard(FlashCriticalSectionIF* section, const char* reason)
        : _section(section), _reason(reason)
    {
        if (_section)
        {
            _section->enterFlashCritical(_reason);
            _active = true;
        }
    }

    FlashCriticalGuard(const char* reason)
        : FlashCriticalGuard(FlashCriticalSectionIF::getGlobal(), reason)
    {
    }

    ~FlashCriticalGuard()
    {
        if (_active && _section)
        {
            _section->exitFlashCritical(_reason);
        }
    }

    FlashCriticalGuard(const FlashCriticalGuard&) = delete;
    FlashCriticalGuard& operator=(const FlashCriticalGuard&) = delete;

private:
    FlashCriticalSectionIF* _section = nullptr;
    const char* _reason = nullptr;
    bool _active = false;
};
