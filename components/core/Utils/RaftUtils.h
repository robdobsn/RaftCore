/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftUtils
//
// Rob Dobson 2012-2022
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <ArduinoOrAlt.h>
#include <vector>
#include <Logger.h>
#include <stdio.h>

namespace Raft
{
    // Test for a timeout handling wrap around
    // Usage example: isTimeout(millis(), myLastTime, 1000)
    // This will return true if myLastTime was set to millis() more than 1000 milliseconds ago
    bool isTimeout(uint64_t curTime, uint64_t lastTime, uint64_t maxDuration);

    // Get time until a timeout handling wrap around
    // Usage example: timeToTimeout(millis(), myLastTime)
    // Returns milliseconds since myLastTime
    uint64_t timeToTimeout(uint64_t curTime, uint64_t lastTime, uint64_t maxDuration);

    // Get time elapsed handling wrap around
    // Usage example: timeElapsed(millis(), myLastTime)
    uint64_t timeElapsed(uint64_t curTime, uint64_t lastTime);

    // Setup a result string
    void setJsonBoolResult(const char* req, String& resp, bool rslt, const char* otherJson = nullptr);

    // Set a result error
    void setJsonErrorResult(const char* req, String& resp, const char* errorMsg, const char* otherJson = nullptr);

    // Set a JSON result
    void setJsonResult(const char* pReq, String& resp, bool rslt, const char* errorMsg = nullptr, const char* otherJson = nullptr);

    // Following code from Unix sources
    unsigned long convIPStrToAddr(String &inStr);

    // Escape a string in JSON
    String escapeJSON(const String& inStr);

    // Convert HTTP query format to JSON
    // JSON only contains name/value pairs and not {}
    String getJSONFromHTTPQueryStr(const char* inStr, bool mustStartWithQuestionMark = true);

    // Get Nth field from string
    String getNthField(const char* inStr, int N, char separator);

    // Get a uint8_t value from the uint8_t pointer passed in
    // Increment the pointer (by 1)
    // Also checks endStop pointer value if provided
    uint16_t getUint8AndInc(const uint8_t*& pVal, const uint8_t* pEndStop = nullptr);

    // Get an int8_t value from the uint8_t pointer passed in
    // Increment the pointer (by 1)
    // Also checks endStop pointer value if provided
    int16_t getInt8AndInc(const uint8_t*& pVal, const uint8_t* pEndStop = nullptr);

    // Get a uint16_t little endian value from the uint8_t pointer passed in
    // Increment the pointer (by 2)
    // Also checks endStop pointer value if provided
    uint16_t getLEUint16AndInc(const uint8_t*& pVal, const uint8_t* pEndStop = nullptr);

    // Get a int16_t little endian value from the uint8_t pointer passed in
    // Increment the pointer (by 2)
    // Also checks endStop pointer value if provided
    int16_t getLEInt16AndInc(const uint8_t*& pVal, const uint8_t* pEndStop = nullptr);

    // Get a uint16_t big endian value from the uint8_t pointer passed in
    // Increment the pointer (by 2)
    // Also checks endStop pointer value if provided
    uint16_t getBEUint16AndInc(const uint8_t*& pVal, const uint8_t* pEndStop = nullptr);

    // Get a int16_t big endian value from the uint8_t pointer passed in
    // Increment the pointer (by 2)
    // Also checks endStop pointer value if provided
    int16_t getBEInt16AndInc(const uint8_t*& pVal, const uint8_t* pEndStop = nullptr);

    // Get a uint32_t big endian value from the uint8_t pointer passed in
    // Increment the pointer (by 4)
    // Also checks endStop pointer value if provided
    uint32_t getBEUint32AndInc(const uint8_t*& pVal, const uint8_t* pEndStop = nullptr);

    // Get a float32_t big endian value from the uint8_t pointer passed in
    // Increment the pointer (by 4)
    // Also checks endStop pointer value if provided
    float getBEfloat32AndInc(const uint8_t*& pVal, const uint8_t* pEndStop = nullptr);

    // Set values into a buffer
    void setBEInt8(uint8_t* pBuf, uint32_t offset, int8_t val);

    // Set values into a buffer
    void setBEUint8(uint8_t* pBuf, uint32_t offset, uint8_t val);

    // Set values into a buffer
    void setBEInt16(uint8_t* pBuf, uint32_t offset, int16_t val);

    // Set values into a buffer
    void setBEUint16(uint8_t* pBuf, uint32_t offset, uint16_t val);

    // Set values into a buffer
    void setBEUint32(uint8_t* pBuf, uint32_t offset, uint32_t val);

    // Set values into a buffer
    // Since ESP32 is little-endian this means reversing the order
    void setBEFloat32(uint8_t* pBuf, uint32_t offset, float val);

    // Get a string from a fixed-length buffer
    // false if the string was truncated
    bool strFromBuffer(const uint8_t* pBuf, uint32_t bufLen, String& outStr, bool asciiOnly=true);

    // Clamp - like std::clamp but for uint32_t
    uint32_t clamp(uint32_t val, uint32_t lo, uint32_t hi);

    // RGB Value
    struct RGBValue
    {
        RGBValue()
        {
            r=0; g=0; b=0;
        }
        RGBValue(uint32_t r_, uint32_t g_, uint32_t b_)
        {
            r=r_; g=g_; b=b_;
        }
        uint32_t r;
        uint32_t g;
        uint32_t b;
    };

    // Get RGB from hex string
    RGBValue getRGBFromHex(const String& colourStr);

    // Get decimal value of hex char
    uint32_t getHexFromChar(int ch);

    // Extract bytes from hex encoded string
    uint32_t getBytesFromHexStr(const char* inStr, uint8_t* outBuf, size_t maxOutBufLen);

    // Generate a hex string from bytes
    void getHexStrFromBytes(const uint8_t* pBuf, uint32_t bufLen, String& outStr);
    
    // Generate a hex string from uint32_t
    void getHexStrFromUint32(const uint32_t* pBuf, uint32_t bufLen, String& outStr, 
                const char* separator);
    
    // Find match in buffer (like strstr for unterminated strings)
    // Returns position in buffer of val or -1 if not found
    int findInBuf(const uint8_t* pBuf, uint32_t bufLen, 
                const uint8_t* pToFind, uint32_t toFindLen);

    // Parse a string into a list of integers
    void parseIntList(const char* pInStr, std::vector<int>& outList, const char* pSep = ",");

    // Log a buffer
    void logHexBuf(const uint8_t* pBuf, uint32_t bufLen, const char* logPrefix, const char* logIntro);

    // Format MAC address
    String formatMACAddr(const uint8_t* pMacAddr, const char* separator);
};

// Name value pair double
struct NameValuePairDouble
{
    NameValuePairDouble(const char* itemName, double itemValue)
    {
        name = itemName;
        value = itemValue;
    }
    String name;
    double value;
};

// ABS macro
#define UTILS_ABS(N) ((N<0)?(-N):(N))
#define UTILS_MAX(A,B) (A > B ? A : B)
#define UTILS_MIN(A,B) (A < B ? A : B)


