////////////////////////////////////////////////////////////////////////////////
//
// DemoDevice.cpp
//
////////////////////////////////////////////////////////////////////////////////

#include "DemoDevice.h"
#include "DeviceTypeRecords.h"
#include "RaftUtils.h"
#include "RaftArduino.h"
#include <cmath>
#include <algorithm>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
DemoDevice::DemoDevice(const char* pDeviceClassName, const char* pConfigStr)
    : RaftDevice(pDeviceClassName, pConfigStr)
{
    // Initialize timestamps
    _startTimeMs = millis();
    _lastUpdateMs = _startTimeMs;
    updateTimestamp();
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
    // Call base class setup
    RaftDevice::setup();
        
    LOG_I(MODULE_PREFIX, "setup device %s", getDeviceName().c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Post setup
void DemoDevice::postSetup()
{
    // Call base class post setup
    RaftDevice::postSetup();
    
    LOG_I(MODULE_PREFIX, "postSetup device %s", getDeviceName().c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Loop
void DemoDevice::loop()
{
    // Check if demo is active
    if (!_isActive)
        return;

    // Check if duration has expired
    if (_durationMs > 0)
    {
        uint32_t elapsedMs = millis() - _startTimeMs;
        if (elapsedMs >= _durationMs)
        {
            _isActive = false;
            LOG_I(MODULE_PREFIX, "loop demo duration expired for %s", _demoDeviceType.c_str());
            return;
        }
    }        // Check if it's time to generate new data
    uint32_t currentTimeMs = millis();
    if (Raft::isTimeout(currentTimeMs, _lastUpdateMs, _sampleRateMs))
    {
        generateDemoData();
        _lastUpdateMs = currentTimeMs;
        updateTimestamp();
        
        // Notify data change callbacks
        if (_dataChangeCB && _currentDataBinary.size() > 0)
        {
            _dataChangeCB(getDeviceTypeIndex(), _currentDataBinary, _pCallbackInfo);
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Configure demo device
bool DemoDevice::configureDemoDevice(const String& deviceType, uint32_t sampleRateMs, uint32_t durationMs)
{
    String chosenType = deviceType;
    uint32_t chosenSampleRate = sampleRateMs;
    // If no type provided or not found, default to LSM6DS
    if (chosenType.isEmpty() || !loadDeviceTypeInfo(chosenType)) {
        chosenType = "LSM6DS";
        if (!loadDeviceTypeInfo(chosenType)) {
            LOG_E(MODULE_PREFIX, "configureDemoDevice failed to load default device type LSM6DS");
            return false;
        }
    }
    // Store configuration
    _demoDeviceType = chosenType;
    // Default to 1s if not specified or out of range
    if (chosenSampleRate < MIN_SAMPLE_RATE_MS || chosenSampleRate > MAX_SAMPLE_RATE_MS)
        chosenSampleRate = 1000;
    _sampleRateMs = chosenSampleRate;
    _durationMs = durationMs;
    _startTimeMs = millis();
    _lastUpdateMs = _startTimeMs;
    _isActive = true;

    // Generate initial data
    generateDemoData();
    updateTimestamp();

    LOG_I(MODULE_PREFIX, "configureDemoDevice %s rate=%dms duration=%dms", 
          _demoDeviceType.c_str(), _sampleRateMs, _durationMs);

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if demo is active
bool DemoDevice::isDemoActive() const
{
    return _isActive;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get demo device type
String DemoDevice::getDemoDeviceType() const
{
    return _demoDeviceType;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get status JSON
String DemoDevice::getStatusJSON() const
{
    if (!_isActive || _currentDataJSON.length() == 0)
        return "{}";
    
    return _currentDataJSON;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get status binary
std::vector<uint8_t> DemoDevice::getStatusBinary() const
{
    if (!_isActive)
        return std::vector<uint8_t>();
    
    return _currentDataBinary;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get debug JSON
String DemoDevice::getDebugJSON(bool includePlugAndPlayInfo) const
{
    String debugStr = "{";
    debugStr += "\"name\":\"" + getDeviceName() + "\",";
    debugStr += "\"type\":\"" + _demoDeviceType + "\",";
    debugStr += "\"active\":" + String(_isActive ? "true" : "false") + ",";
    debugStr += "\"sampleRate\":" + String(_sampleRateMs) + ",";
    debugStr += "\"duration\":" + String(_durationMs);
    
    if (_isActive && _durationMs > 0)
    {
        uint32_t elapsedMs = millis() - _startTimeMs;
        uint32_t remainingMs = (_durationMs > elapsedMs) ? (_durationMs - elapsedMs) : 0;
        debugStr += ",\"remaining\":" + String(remainingMs);
    }
    
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
/// @brief Load device type information
bool DemoDevice::loadDeviceTypeInfo(const String& deviceType)
{
    // Get device type info from global device type records
    extern DeviceTypeRecords deviceTypeRecords;
    String deviceTypeInfoStr = deviceTypeRecords.getDevTypeInfoJsonByTypeName(deviceType, false);
    
    if (deviceTypeInfoStr.length() == 0 || deviceTypeInfoStr == "{}")
    {
        LOG_W(MODULE_PREFIX, "loadDeviceTypeInfo device type %s not found", deviceType.c_str());
        return false;
    }

    // Parse device type info
    _deviceTypeInfo.setJsonDoc(deviceTypeInfoStr.c_str());
    
    LOG_I(MODULE_PREFIX, "loadDeviceTypeInfo loaded %s: %s", 
          deviceType.c_str(), deviceTypeInfoStr.c_str());
    
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Generate demo data
void DemoDevice::generateDemoData()
{
    if (!_isActive)
        return;

    uint32_t currentTimeMs = millis();
    
    // Generate JSON data
    _currentDataJSON = generateDeviceTypeJSON();
    
    // Generate binary data  
    _currentDataBinary = generateDeviceTypeBinary();
    
    // Update timestamp
    _dataTimestampMs = currentTimeMs;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Generate sensor value with realistic patterns
double DemoDevice::generateSensorValue(const String& fieldName, double minVal, double maxVal, uint32_t timeMs)
{
    // Helper function for random number generation
    auto getRandomFloat = []() -> double {
        return ((double)random() / RAND_MAX) * 2.0 - 1.0; // -1.0 to 1.0
    };

    // Use different patterns for different sensor types
    double t = timeMs / 1000.0; // Convert to seconds
    double range = maxVal - minVal;
    double center = (minVal + maxVal) / 2.0;
    double value = center;

    // Apply patterns based on field name
    if (fieldName.indexOf("temp") >= 0 || fieldName.indexOf("Temp") >= 0)
    {
        // Temperature: slow sine wave with noise
        value = center + (range * 0.3) * sin(t * 0.1) + (range * 0.1) * sin(t * 0.7) + (range * 0.05) * getRandomFloat();
    }
    else if (fieldName.indexOf("accel") >= 0 || fieldName.indexOf("Accel") >= 0 || 
             fieldName.indexOf("gyro") >= 0 || fieldName.indexOf("Gyro") >= 0)
    {
        // Accelerometer/Gyro: more dynamic with occasional spikes
        value = center + (range * 0.2) * sin(t * 2.0) + (range * 0.15) * cos(t * 5.0) + (range * 0.1) * getRandomFloat();
        
        // Occasional spike every ~10 seconds
        if ((int(t) % 10) < 1 && fmod(t, 1.0) < 0.1)
        {
            value += (range * 0.5) * getRandomFloat();
        }
    }
    else if (fieldName.indexOf("light") >= 0 || fieldName.indexOf("Light") >= 0 || 
             fieldName.indexOf("lux") >= 0 || fieldName.indexOf("Lux") >= 0)
    {
        // Light sensor: gradual changes with some variation
        value = center + (range * 0.4) * sin(t * 0.05) + (range * 0.2) * sin(t * 0.3) + (range * 0.1) * getRandomFloat();
    }
    else if (fieldName.indexOf("dist") >= 0 || fieldName.indexOf("Dist") >= 0 || 
             fieldName.indexOf("range") >= 0 || fieldName.indexOf("Range") >= 0)
    {
        // Distance sensor: step changes with noise
        int step = int(t / 3.0) % 4; // Change every 3 seconds
        double stepValue = minVal + (range * step / 4.0);
        value = stepValue + (range * 0.05) * getRandomFloat();
    }
    else if (fieldName.indexOf("press") >= 0 || fieldName.indexOf("Press") >= 0)
    {
        // Pressure: very slow changes
        value = center + (range * 0.1) * sin(t * 0.02) + (range * 0.05) * getRandomFloat();
    }
    else if (fieldName.indexOf("humid") >= 0 || fieldName.indexOf("Humid") >= 0)
    {
        // Humidity: slow changes
        value = center + (range * 0.2) * sin(t * 0.03) + (range * 0.1) * getRandomFloat();
    }
    else
    {
        // Default: gentle sine wave with noise
        value = center + (range * 0.3) * sin(t * 0.5) + (range * 0.1) * getRandomFloat();
    }

    // Constrain to valid range
    return std::max(minVal, std::min(value, maxVal));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Generate device type JSON
String DemoDevice::generateDeviceTypeJSON()
{
    String jsonStr = "{";
    bool firstField = true;
    uint32_t currentTimeMs = millis();

    // Get poll responses from device type info
    std::vector<String> pollResponses;
    _deviceTypeInfo.getArrayElems("resp", pollResponses);

    for (const String& respStr : pollResponses)
    {
        RaftJson respJson;
        respJson.setJsonDoc(respStr.c_str());
        
        // Get fields from this response
        std::vector<String> fields;
        respJson.getArrayElems("vals", fields);
        
        for (const String& fieldStr : fields)
        {
            RaftJson fieldJson;
            fieldJson.setJsonDoc(fieldStr.c_str());
            
            String fieldName = fieldJson.getString("n", "");
            String fieldType = fieldJson.getString("t", "u");
            double minVal = fieldJson.getDouble("min", 0.0);
            double maxVal = fieldJson.getDouble("max", 100.0);
            
            if (fieldName.length() == 0)
                continue;
                
            if (!firstField)
                jsonStr += ",";
            
            // Generate value based on type
            if (fieldType == "f" || fieldType == "d")
            {
                // Float/double
                double value = generateSensorValue(fieldName, minVal, maxVal, currentTimeMs);
                jsonStr += "\"" + fieldName + "\":" + String(value, 3);
            }
            else
            {
                // Integer types
                int32_t value = (int32_t)generateSensorValue(fieldName, minVal, maxVal, currentTimeMs);
                jsonStr += "\"" + fieldName + "\":" + String(value);
            }
            
            firstField = false;
        }
    }
    
    jsonStr += "}";
    return jsonStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Generate device type binary
std::vector<uint8_t> DemoDevice::generateDeviceTypeBinary()
{
    std::vector<uint8_t> binaryData;
    binaryData.reserve(64);
    
    uint32_t currentTimeMs = millis();
    
    // Add device identifier (could be device type index)
    binaryData.push_back(0x01); // Demo device marker
    binaryData.push_back(_demoDeviceType.length());
    for (char c : _demoDeviceType)
    {
        binaryData.push_back((uint8_t)c);
    }
    
    // Add timestamp (4 bytes)
    binaryData.push_back((currentTimeMs) & 0xFF);
    binaryData.push_back((currentTimeMs >> 8) & 0xFF);
    binaryData.push_back((currentTimeMs >> 16) & 0xFF);
    binaryData.push_back((currentTimeMs >> 24) & 0xFF);
    
    // Add some demo sensor data
    std::vector<String> pollResponses;
    _deviceTypeInfo.getArrayElems("resp", pollResponses);

    for (const String& respStr : pollResponses)
    {
        RaftJson respJson;
        respJson.setJsonDoc(respStr.c_str());
        
        std::vector<String> fields;
        respJson.getArrayElems("vals", fields);
        
        for (const String& fieldStr : fields)
        {
            RaftJson fieldJson;
            fieldJson.setJsonDoc(fieldStr.c_str());
            
            String fieldName = fieldJson.getString("n", "");
            String fieldType = fieldJson.getString("t", "u");
            double minVal = fieldJson.getDouble("min", 0.0);
            double maxVal = fieldJson.getDouble("max", 100.0);
            
            if (fieldName.length() == 0)
                continue;
                
            // Generate and add binary value
            if (fieldType == "f")
            {
                // Float (4 bytes)
                float value = (float)generateSensorValue(fieldName, minVal, maxVal, currentTimeMs);
                uint8_t* pBytes = (uint8_t*)&value;
                for (int i = 0; i < 4; i++)
                {
                    binaryData.push_back(pBytes[i]);
                }
            }
            else if (fieldType == "d")
            {
                // Double (8 bytes) - convert to float for space
                float value = (float)generateSensorValue(fieldName, minVal, maxVal, currentTimeMs);
                uint8_t* pBytes = (uint8_t*)&value;
                for (int i = 0; i < 4; i++)
                {
                    binaryData.push_back(pBytes[i]);
                }
            }
            else if (fieldType == "u16")
            {
                // 16-bit unsigned
                uint16_t value = (uint16_t)generateSensorValue(fieldName, minVal, maxVal, currentTimeMs);
                binaryData.push_back(value & 0xFF);
                binaryData.push_back((value >> 8) & 0xFF);
            }
            else if (fieldType == "i16")
            {
                // 16-bit signed
                int16_t value = (int16_t)generateSensorValue(fieldName, minVal, maxVal, currentTimeMs);
                binaryData.push_back(value & 0xFF);
                binaryData.push_back((value >> 8) & 0xFF);
            }
            else
            {
                // Default: 8-bit unsigned
                uint8_t value = (uint8_t)generateSensorValue(fieldName, minVal, maxVal, currentTimeMs);
                binaryData.push_back(value);
            }
        }
    }
    
    return binaryData;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Update timestamp
void DemoDevice::updateTimestamp()
{
    _dataTimestampMs = millis();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Register for device data notifications
void DemoDevice::registerForDeviceData(RaftDeviceDataChangeCB dataChangeCB, uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo)
{
    _dataChangeCB = dataChangeCB;
    _minTimeBetweenReportsMs = minTimeBetweenReportsMs;
    _pCallbackInfo = pCallbackInfo;
    
    LOG_I(MODULE_PREFIX, "registerForDeviceData demo device %s callback registered", getDeviceName().c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
