////////////////////////////////////////////////////////////////////////////////
//
// DemoDevice.h
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftDevice.h"
#include "RaftJson.h"
#include <cmath>

class DemoDevice : public RaftDevice
{
public:
    DemoDevice(const char* pDeviceClassName, const char* pConfigStr);
    virtual ~DemoDevice();

    // RaftDevice overrides
    virtual void setup() override;
    virtual void loop() override;
    virtual void postSetup() override;
    virtual String getStatusJSON() const override;
    virtual std::vector<uint8_t> getStatusBinary() const override;
    virtual String getDebugJSON(bool includePlugAndPlayInfo) const override;
    virtual uint32_t getDeviceInfoTimestampMs(bool includeElemOnlineStatusChanges, bool includePollDataUpdates) const override;
    virtual void registerForDeviceData(RaftDeviceDataChangeCB dataChangeCB, uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo) override;

    // Demo-specific methods
    bool configureDemoDevice(const String& deviceType, uint32_t sampleRateMs, uint32_t durationMs);
    bool isDemoActive() const;
    String getDemoDeviceType() const;

private:
    // Demo configuration
    String _demoDeviceType;
    uint32_t _sampleRateMs = 1000;
    uint32_t _durationMs = 0;
    uint32_t _startTimeMs = 0;
    uint32_t _lastUpdateMs = 0;
    bool _isActive = false;

    // Device type information
    RaftJson _deviceTypeInfo;
    
    // Current demo data
    String _currentDataJSON;
    std::vector<uint8_t> _currentDataBinary;
    uint32_t _dataTimestampMs = 0;

    // Callback information
    RaftDeviceDataChangeCB _dataChangeCB = nullptr;
    uint32_t _minTimeBetweenReportsMs = 1000;
    const void* _pCallbackInfo = nullptr;

    // Demo data generation
    void generateDemoData();
    double generateSensorValue(const String& fieldName, double minVal, double maxVal, uint32_t timeMs);
    String generateDeviceTypeJSON();
    std::vector<uint8_t> generateDeviceTypeBinary();

    // Helper methods
    bool loadDeviceTypeInfo(const String& deviceType);
    void updateTimestamp();

    // Constants
    static constexpr const char* MODULE_PREFIX = "DemoDevice";
    static constexpr uint32_t DEFAULT_SAMPLE_RATE_MS = 1000;
    static constexpr uint32_t MIN_SAMPLE_RATE_MS = 10;
    static constexpr uint32_t MAX_SAMPLE_RATE_MS = 60000;
};
