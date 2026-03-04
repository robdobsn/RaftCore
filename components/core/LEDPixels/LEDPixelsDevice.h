////////////////////////////////////////////////////////////////////////////////
//
// LEDPixelsDevice.h
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftCore.h"
#include "LEDPixels.h"

class RestAPIEndpointManager;
class APISourceInfo;

class LEDPixelsDevice : public RaftDevice
{
public:
    /// @brief Constructor
    /// @param pClassName class name
    /// @param pDevConfigJson device configuration JSON
    LEDPixelsDevice(const char* pClassName, const char *pDevConfigJson);

    /// @brief Destructor
    virtual ~LEDPixelsDevice();

    /// @brief Create function for device factory
    /// @param pClassName class name
    /// @param pDevConfigJson device configuration JSON
    /// @return RaftDevice* pointer to the created device
    static RaftDevice* create(const char* pClassName, const char* pDevConfigJson)
    {
        return new LEDPixelsDevice(pClassName, pDevConfigJson);
    }

    /// @brief Setup the device
    virtual void setup();

    /// @brief Main loop for the device (called frequently)
    virtual void loop() override final;

    /// @brief Add REST API endpoints
    /// @param endpointManager Manager for REST API endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& endpointManager) override final;

private:

    // Debug
    static constexpr const char *MODULE_PREFIX = "LEDPixelsDevice";

    // LED pixels
    LEDPixels _ledPixels;

    // API
    RaftRetCode apiLED(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);
};
