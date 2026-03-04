/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// FlashCriticalSectionIF
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>

class FlashCriticalListener
{
public:
    virtual ~FlashCriticalListener() = default;
    virtual void onFlashCriticalEnter() = 0;
    virtual void onFlashCriticalExit() = 0;
};

class FlashCriticalSectionIF
{
public:
    virtual ~FlashCriticalSectionIF() = default;

    virtual void enterFlashCritical(const char* reason) = 0;
    virtual void exitFlashCritical(const char* reason) = 0;
    virtual void registerListener(FlashCriticalListener* listener) = 0;
    virtual void unregisterListener(FlashCriticalListener* listener) = 0;

    static void setGlobal(FlashCriticalSectionIF* section)
    {
        _globalSection = section;
    }

    static FlashCriticalSectionIF* getGlobal()
    {
        return _globalSection;
    }

private:
    static FlashCriticalSectionIF* _globalSection;
};
