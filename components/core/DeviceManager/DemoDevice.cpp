////////////////////////////////////////////////////////////////////////////////
//
// DemoDevice.cpp
//
////////////////////////////////////////////////////////////////////////////////

#include "DemoDevice.h"
#include "DeviceManager.h"
#include "DeviceTypeRecordDynamic.h"
#include "RaftUtils.h"
#include "RaftArduino.h"
#include <cmath>
#include <cstdio>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
/// @param pClassName class name
/// @param pDevConfigJson device configuration JSON
DemoDevice::DemoDevice(const char* pDeviceClassName, const char* pConfigStr)
    : RaftDevice(pDeviceClassName, pConfigStr)
{
    // Initialize timestamps
    _lastUpdateMs = millis();
    _dataTimestampMs = _lastUpdateMs;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor
DemoDevice::~DemoDevice()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup
void DemoDevice::setup()
{
    // Extract demo settings from device config
    _sampleRateMs = deviceConfig.getInt("sampleRateMs", DEFAULT_SAMPLE_RATE_MS);
    _deviceType = deviceConfig.getString("type", "ACCDEMO");
    if (_deviceType.length() == 0)
        _deviceType = "ACCDEMO";

    // Long-sample demo (even size so the wave is whole int16 elements)
    _sampleBytes = deviceConfig.getInt("sampleBytes", 0);
    if (_sampleBytes > 0)
    {
        if (_sampleBytes < MIN_LONG_SAMPLE_BYTES)
            _sampleBytes = MIN_LONG_SAMPLE_BYTES;
        if (_sampleBytes > MAX_LONG_SAMPLE_BYTES)
            _sampleBytes = MAX_LONG_SAMPLE_BYTES;
        _sampleBytes &= ~1u;
    }

    // Clamp sample rate to reasonable bounds
    if (_sampleRateMs < MIN_SAMPLE_RATE_MS)
        _sampleRateMs = MIN_SAMPLE_RATE_MS;
    if (_sampleRateMs > MAX_SAMPLE_RATE_MS)
        _sampleRateMs = MAX_SAMPLE_RATE_MS;

    // Call base class setup
    RaftDevice::setup();
    
    // Generate initial data
    generateDemoData();
        
    LOG_I(MODULE_PREFIX, "setup device %s type %s rate=%dms sampleBytes %d", getDeviceID().toString().c_str(),
                _deviceType.c_str(), _sampleRateMs, _sampleBytes);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Loop
void DemoDevice::loop()
{
    // Check if it's time to generate new data
    uint32_t currentTimeMs = millis();
    if (Raft::isTimeout(currentTimeMs, _lastUpdateMs, _sampleRateMs))
    {
        generateDemoData();
        _lastUpdateMs = currentTimeMs;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get status JSON following DevicePower.cpp pattern
String DemoDevice::getStatusJSON() const
{
    // Buffer
    std::vector<uint8_t> data;
    if (_sampleBytes > 0)
        formLongSampleResponse(data);
    else
        formDeviceDataResponse(data);

    // Return JSON in the same format as DevicePower (outer key is this device's address)
    return "{\"" + String(getDeviceID().getAddress()) + "\":{\"x\":\"" + Raft::getHexStr(data.data(), data.size()) +
            "\",\"_i\":\"" + String(getDeviceTypeIndex()) + "\"}}";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get status binary following DevicePower.cpp pattern
std::vector<uint8_t> DemoDevice::getStatusBinary() const
{
    // Get device data
    std::vector<uint8_t> data;
    if (_sampleBytes > 0)
        formLongSampleResponse(data);
    else
        formDeviceDataResponse(data);

    // Buffer
    std::vector<uint8_t> binBuf;

    // Generate binary device message using this device's own bus/address (a fixed address 0
    // would collide with the first static device)
    genBinaryDataMsg(binBuf, getDeviceTypeIndex(), DeviceOnlineState::ONLINE, 0, data);

    // Return binary data
    return binBuf;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get debug JSON
String DemoDevice::getDebugJSON(bool includePlugAndPlayInfo) const
{
    String debugStr = "{";
    RaftJson::appendStringField(debugStr, "name", getDeviceID().toString().c_str());
    RaftJson::appendStringField(debugStr, "type", getConfiguredDeviceType().c_str());
    RaftJson::appendRawField(debugStr, "sampleRate", String(_sampleRateMs).c_str());
    debugStr += "}";
    return debugStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device info timestamp
uint32_t DemoDevice::getDeviceInfoTimestampMs(bool includeElemOnlineStatusChanges, bool includePollDataUpdates) const
{
    return _dataTimestampMs;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Generate demo data
void DemoDevice::generateDemoData()
{
    uint32_t currentTimeMs = millis();
    
    // Long-sample demo just counts samples (the wave is formed from the count and time);
    // otherwise generate ACCDEMO-specific data
    if (_sampleBytes > 0)
        _sampleCount++;
    else
        generateACCDEMOData(currentTimeMs);
    
    // Update timestamp
    _dataTimestampMs = currentTimeMs;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Generate ACCDEMO specific data following Device.cpp test mode pattern
void DemoDevice::generateACCDEMOData(uint32_t timeMs)
{
    // Generate simulated IMU data with sine and triangle waves
    float time_factor = timeMs * 0.001f; // Convert to seconds
    
    // Helper function for triangle wave generation
    auto triangleWave = [](float t, float phase) -> float {
        float phase_shifted = t + phase;
        float period = 2.0f * M_PI;
        float normalized = fmod(phase_shifted, period);
        if (normalized < 0) normalized += period;
        
        if (normalized < M_PI / 2.0f) {
            return 2.0f * normalized / M_PI;
        } else if (normalized < 3.0f * M_PI / 2.0f) {
            return 2.0f - 2.0f * normalized / M_PI;
        } else {
            return 2.0f * normalized / M_PI - 4.0f;
        }
    };
    
    // Accelerometer data as sine waves with phase shifts (in g units)
    // 30 samples per cycle at 10Hz = 3 seconds per cycle = 1/3 Hz frequency
    float freq_accel = 1.0f / 3.0f; // 0.333 Hz frequency for 30 samples per cycle
    float ax = 0.1f * sin(time_factor * freq_accel * 2.0f * M_PI);                    // Phase 0
    float ay = 0.1f * sin(time_factor * freq_accel * 2.0f * M_PI + 2.0f * M_PI / 3.0f); // Phase 120°
    float az = 0.1f * sin(time_factor * freq_accel * 2.0f * M_PI + 4.0f * M_PI / 3.0f); // Phase 240°
    
    // Gyroscope data as triangle waves with phase shifts (in deg/s)
    // 50 samples per cycle at 10Hz = 5 seconds per cycle = 1/5 Hz frequency
    float freq_gyro = 1.0f / 5.0f; // 0.2 Hz frequency for 50 samples per cycle
    float gx = 10.0f * triangleWave(time_factor * freq_gyro * 2.0f * M_PI, 0.0f);                    // Phase 0
    float gy = 10.0f * triangleWave(time_factor * freq_gyro * 2.0f * M_PI, 2.0f * M_PI / 3.0f);      // Phase 120°
    float gz = 10.0f * triangleWave(time_factor * freq_gyro * 2.0f * M_PI, 4.0f * M_PI / 3.0f);      // Phase 240°
    
    // Store current data values for formDeviceDataResponse
    _currentAx = ax;
    _currentAy = ay;
    _currentAz = az;
    _currentGx = gx;
    _currentGy = gy;
    _currentGz = gz;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Form device data response
/// @param data (out) Data buffer
void DemoDevice::formDeviceDataResponse(std::vector<uint8_t>& data) const
{
    // Get time of last status update (16-bit timestamp like DevicePower)
    uint16_t timeVal = (uint16_t)(_dataTimestampMs & 0xFFFF);
    data.push_back((timeVal >> 8) & 0xFF);
    data.push_back(timeVal & 0xFF);
    
    // Accelerometer data (6 bytes, scaled to int16)
    int16_t ax_int = (int16_t)(_currentAx * 1000); // Scale to mg
    int16_t ay_int = (int16_t)(_currentAy * 1000);
    int16_t az_int = (int16_t)(_currentAz * 1000);
    
    data.push_back((ax_int >> 8) & 0xFF);
    data.push_back(ax_int & 0xFF);
    data.push_back((ay_int >> 8) & 0xFF);
    data.push_back(ay_int & 0xFF);
    data.push_back((az_int >> 8) & 0xFF);
    data.push_back(az_int & 0xFF);
    
    // Gyroscope data (6 bytes, scaled to int16)
    int16_t gx_int = (int16_t)(_currentGx * 100); // Scale to 0.01 deg/s
    int16_t gy_int = (int16_t)(_currentGy * 100);
    int16_t gz_int = (int16_t)(_currentGz * 100);
    
    data.push_back((gx_int >> 8) & 0xFF);
    data.push_back(gx_int & 0xFF);
    data.push_back((gy_int >> 8) & 0xFF);
    data.push_back(gy_int & 0xFF);
    data.push_back((gz_int >> 8) & 0xFF);
    data.push_back(gz_int & 0xFF);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Form a long-sample demo response: 16-bit ms timestamp, 16-bit sample count, then a
///        signed 16-bit sine wave across the elements that drifts with the count
/// @param data (out) Data buffer (2 + _sampleBytes bytes)
void DemoDevice::formLongSampleResponse(std::vector<uint8_t>& data) const
{
    uint32_t numElems = (_sampleBytes - 2) / 2;
    data.reserve(2 + _sampleBytes);

    // Time of last update (16-bit ms, as the other direct-connect devices)
    uint16_t timeVal = (uint16_t)(_dataTimestampMs & 0xFFFF);
    data.push_back((timeVal >> 8) & 0xFF);
    data.push_back(timeVal & 0xFF);

    // Sample count (big-endian)
    data.push_back((_sampleCount >> 8) & 0xFF);
    data.push_back(_sampleCount & 0xFF);

    // One cycle across the elements, phase advancing with the count (little-endian int16)
    for (uint32_t i = 0; i < numElems; i++)
    {
        float phase = 2.0f * M_PI * ((float)i / numElems + _sampleCount * 0.02f);
        int16_t val = (int16_t)(1000.0f * sinf(phase));
        data.push_back(val & 0xFF);
        data.push_back((val >> 8) & 0xFF);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get the device type record for this device so that it can be added to the device type records
/// @param devTypeRec (out) Device type record
/// @return true if the device has a device type record
bool DemoDevice::getDeviceTypeRecord(DeviceTypeRecordDynamic& devTypeRec) const
{
    // Device info JSON for ACCDEMO following DevicePower pattern
    static const char* devInfoJson = R"~({"name":"ACCDEMO Demo IMU","desc":"ACCDEMO Accelerometer/Gyroscope","manu":"Demo","type":"ACCDEMO")~"
            R"~(,"resp":{"b":14,"a":[)~"
            R"~({"n":"ax","t":">h","u":"mg","r":[-2000,2000],"d":1000,"f":".3f","o":"float"},)~"
            R"~({"n":"ay","t":">h","u":"mg","r":[-2000,2000],"d":1000,"f":".3f","o":"float"},)~"
            R"~({"n":"az","t":">h","u":"mg","r":[-2000,2000],"d":1000,"f":".3f","o":"float"},)~"
            R"~({"n":"gx","t":">h","u":"deg/s","r":[-2000,2000],"d":100,"f":".2f","o":"float"},)~"
            R"~({"n":"gy","t":">h","u":"deg/s","r":[-2000,2000],"d":100,"f":".2f","o":"float"},)~"
            R"~({"n":"gz","t":">h","u":"deg/s","r":[-2000,2000],"d":100,"f":".2f","o":"float"})~"
            R"~(]}})~";

    // Long-sample demo: counter + sine wave array attribute (shown as a spectrum)
    if (_sampleBytes > 0)
    {
        uint32_t numElems = (_sampleBytes - 2) / 2;
        char longInfoJson[400];
        snprintf(longInfoJson, sizeof(longInfoJson),
                R"~({"name":"Long Sample Demo","desc":"%u-byte samples","manu":"Demo","type":"%s",)~"
                R"~("resp":{"b":%u,"a":[)~"
                R"~({"n":"count","t":">H","r":[0,65535],"f":"d","o":"uint16"},)~"
                R"~({"n":"wave","t":"<h[%u]","r":[-1000,1000],"f":"d","o":"int16","vs":false,"vt":"spectrum"})~"
                R"~(]}})~",
                (unsigned)_sampleBytes, _deviceType.c_str(), (unsigned)_sampleBytes, (unsigned)numElems);
        devTypeRec = DeviceTypeRecordDynamic(
            getConfiguredDeviceType().c_str(),
            "",
            "",
            "",
            "",
            _sampleBytes,
            longInfoJson,
            nullptr
        );
        return true;
    }

    // Set the device type record
    devTypeRec = DeviceTypeRecordDynamic(
        getConfiguredDeviceType().c_str(),
        "",
        "",
        "",
        "",
        1, // Device type index for demo
        devInfoJson,
        nullptr
    );

    return true;
}
