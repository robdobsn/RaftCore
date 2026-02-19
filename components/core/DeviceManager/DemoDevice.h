////////////////////////////////////////////////////////////////////////////////
//
// DemoDevice.h
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftDevice.h"
#include "RaftJson.h"
#include "DeviceTypeRecordDynamic.h"
#include <cmath>

class DemoDevice : public RaftDevice
{
public:
    DemoDevice(const char* pDeviceClassName, const char* pConfigStr);
    virtual ~DemoDevice();

    // Create function
    static RaftDevice* create(const char* pClassName, const char* pDevConfigJson)
    {
        return new DemoDevice(pClassName, pDevConfigJson);
    }

    // RaftDevice overrides
    virtual void setup() override;
    virtual void loop() override;
    virtual String getStatusJSON() const override;
    virtual std::vector<uint8_t> getStatusBinary() const override;
    virtual String getDebugJSON(bool includePlugAndPlayInfo) const override;
    virtual uint32_t getDeviceInfoTimestampMs(bool includeElemOnlineStatusChanges, bool includePollDataUpdates) const override;
    virtual bool getDeviceTypeRecord(DeviceTypeRecordDynamic& devTypeRec) const override;
    
    // Device type for publishing
    virtual String getConfiguredDeviceType() const override { return "ACCDEMO"; }

private:
    // Demo configuration
    uint32_t _sampleRateMs = DEFAULT_SAMPLE_RATE_MS;
    uint32_t _lastUpdateMs = 0;
    uint32_t _dataTimestampMs = 0;

    // Current demo data values
    float _currentAx = 0.0f;
    float _currentAy = 0.0f; 
    float _currentAz = 0.0f;
    float _currentGx = 0.0f;
    float _currentGy = 0.0f;
    float _currentGz = 0.0f;

    // Demo data generation
    void generateDemoData();
    void generateACCDEMOData(uint32_t timeMs);
    void formDeviceDataResponse(std::vector<uint8_t>& data) const;

    // Constants
    static constexpr const char* MODULE_PREFIX = "DemoDevice";
    static constexpr uint32_t DEFAULT_SAMPLE_RATE_MS = 100; // 10Hz default
    static constexpr uint32_t MIN_SAMPLE_RATE_MS = 10;      // 100Hz max
    static constexpr uint32_t MAX_SAMPLE_RATE_MS = 60000;   // 1 minute max
};
