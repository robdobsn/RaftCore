/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftUtils
//
// Rob Dobson 2012-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include <stdio.h>
#include "Logger.h"
#include "RaftArduino.h"
#include "RaftRetCode.h"

namespace Raft
{
    /// @brief Check if a time limit has expired (taking into account counter wrapping)
    /// @param curTime Current time (in the same units as other parameters)
    /// @param lastTime Time of last event
    /// @param maxDuration Maximum duration before timeout
    /// @return true if the time limit has expired
    /// @example isTimeout(millis(), myLastTime, 1000) - returns true if myLastTime was set to millis() more than 1000 milliseconds ago
    bool isTimeout(uint64_t curTime, uint64_t lastTime, uint64_t maxDuration);

    /// @brief Calculate the before a time-out occurs (handling counter wrapping)
    /// @param curTime Current time (in the same units as other parameters)
    /// @param lastTime Time of last event
    /// @param maxDuration Maximum duration before timeout
    /// @return Time before timeout (0 if already expired)
    uint64_t timeToTimeout(uint64_t curTime, uint64_t lastTime, uint64_t maxDuration);

    /// @brief Calculate the time elapsed since a timer started (handling counter wrapping)
    /// @param curTime Current time (in the same units as other parameters)
    /// @param lastTime Timer started
    /// @return Time elapsed
    uint64_t timeElapsed(uint64_t curTime, uint64_t lastTime);

    /// @brief Set results for JSON comms to a bool value
    /// @param pReq Request string
    /// @param resp Response string
    /// @param rslt Result value
    /// @param otherJson Additional JSON to add to the response
    /// @return RaftRetCode
    RaftRetCode setJsonBoolResult(const char* req, String& resp, bool rslt, const char* otherJson = nullptr);

    /// @brief Set results for JSON comms with an error message
    /// @param pReq Request string
    /// @param resp Response string
    /// @param errorMsg Error message
    /// @param otherJson Additional JSON to add to the response
    /// @return RaftRetCode
    RaftRetCode setJsonErrorResult(const char* req, String& resp, const char* errorMsg, const char* otherJson = nullptr);

    /// @brief Set results for JSON comms with result type, error message and additional JSON
    /// @param pReq Request string
    /// @param resp Response string
    /// @param rslt Result value
    /// @param errorMsg Error message
    /// @param otherJson Additional JSON to add to the response
    /// @return RaftRetCode
    RaftRetCode setJsonResult(const char* pReq, String& resp, bool rslt, const char* errorMsg = nullptr, const char* otherJson = nullptr);

    /// @brief Escape string using hex character encoding for control characters
    /// @param inStr Input string
    /// @return Escaped string
    String escapeString(const String& inStr);

    /// @brief Unescape string handling hex character encoding for control characters
    /// @param inStr Input string
    /// @return Unescaped string
    String unescapeString(const String& inStr);

    /// @brief Convert HTTP query format to JSON
    /// @param inStr Input string
    /// @param mustStartWithQuestionMark true if the input string must start with a question mark
    /// @return JSON string (only contains name/value pairs and not {})
    String getJSONFromHTTPQueryStr(const char* inStr, bool mustStartWithQuestionMark = true);

    /// @brief Get Nth field from delimited string
    /// @param inStr Input string
    /// @param N Field number
    /// @param separator Field separator character
    /// @return Nth field
    String getNthField(const char* inStr, int N, char separator);

    /// @brief Get a uint8_t value from the uint8_t pointer passed in and increment the pointer
    /// @param pBuf Pointer to the buffer
    /// @param pEndStop Pointer to the end of the buffer
    /// @return uint8_t value
    uint16_t getUint8AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop = nullptr);

    /// @brief Get a int8_t value from the uint8_t pointer passed in and increment the pointer
    /// @param pBuf Pointer to the buffer
    /// @param pEndStop Pointer to the end of the buffer
    /// @return int8_t value
    int16_t getInt8AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop = nullptr);

    /// @brief Get a uint16_t little endian value from the uint8_t pointer passed in and increment the pointer
    /// @param pBuf Pointer to the buffer
    /// @param pEndStop Pointer to the end of the buffer
    /// @return uint16_t value
    uint16_t getLEUint16AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop = nullptr);

    /// @brief Get a int16_t little endian value from the uint8_t pointer passed in and increment the pointer
    /// @param pBuf Pointer to the buffer
    /// @param pEndStop Pointer to the end of the buffer
    /// @return int16_t value
    int16_t getLEInt16AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop = nullptr);

    /// @brief Get a uint16_t big endian value from the uint8_t pointer passed in and increment the pointer
    /// @param pBuf Pointer to the buffer
    /// @param pEndStop Pointer to the end of the buffer
    /// @return uint16_t value
    uint16_t getBEUint16AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop = nullptr);

    /// @brief Get a int16_t big endian value from the uint8_t pointer passed in and increment the pointer
    /// @param pBuf Pointer to the buffer
    /// @param pEndStop Pointer to the end of the buffer
    /// @return int16_t value
    int16_t getBEInt16AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop = nullptr);

    /// @brief Get a uint32_t little endian value from the uint8_t pointer passed in and increment the pointer
    /// @param pBuf Pointer to the buffer
    /// @param pEndStop Pointer to the end of the buffer
    /// @return uint32_t value
    uint32_t getLEUint32AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop = nullptr);

    /// @brief Get a int32_t little endian value from the uint8_t pointer passed in and increment the pointer
    /// @param pBuf Pointer to the buffer
    /// @param pEndStop Pointer to the end of the buffer
    /// @return int32_t value
    int32_t getLEInt32AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop = nullptr);

    /// @brief Get a uint32_t big endian value from the uint8_t pointer passed in and increment the pointer
    /// @param pBuf Pointer to the buffer
    /// @param pEndStop Pointer to the end of the buffer
    /// @return uint32_t value
    uint32_t getBEUint32AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop = nullptr);

    /// @brief Get a int32_t big endian value from the uint8_t pointer passed in and increment the pointer
    /// @param pBuf Pointer to the buffer
    /// @param pEndStop Pointer to the end of the buffer
    /// @return int32_t value
    int32_t getBEInt32AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop = nullptr);

    /// @brief Get a uint64_t little endian value from the uint8_t pointer passed in and increment the pointer
    /// @param pBuf Pointer to the buffer
    /// @param pEndStop Pointer to the end of the buffer
    /// @return uint64_t value
    uint64_t getLEUint64AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop);

    /// @brief Get a int64_t little endian value from the uint8_t pointer passed in and increment the pointer
    /// @param pBuf Pointer to the buffer
    /// @param pEndStop Pointer to the end of the buffer
    /// @return int64_t value
    int64_t getLEInt64AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop);

    /// @brief Get a uint64_t big endian value from the uint8_t pointer passed in and increment the pointer
    /// @param pBuf Pointer to the buffer
    /// @param pEndStop Pointer to the end of the buffer
    /// @return uint64_t value
    uint64_t getBEUInt64AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop);

    /// @brief Get a int64_t big endian value from the uint8_t pointer passed in and increment the pointer
    /// @param pBuf Pointer to the buffer
    /// @param pEndStop Pointer to the end of the buffer
    /// @return int64_t value
    int64_t getBEInt64AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop);

    /// @brief Get a float32_t little endian value from the uint8_t pointer passed in and increment the pointer
    /// @param pBuf Pointer to the buffer
    /// @param pEndStop Pointer to the end of the buffer
    /// @return float32_t value
    float getLEfloat32AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop = nullptr);

    /// @brief Get a float32_t big endian value from the uint8_t pointer passed in and increment the pointer
    /// @param pBuf Pointer to the buffer
    /// @param pEndStop Pointer to the end of the buffer
    /// @return float32_t value
    float getBEfloat32AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop = nullptr);

    /// @brief Get a double64 little endian value from the uint8_t pointer passed in and increment the pointer
    /// @param pBuf Pointer to the buffer
    /// @param pEndStop Pointer to the end of the buffer
    /// @return double64 value
    double getLEdouble64AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop = nullptr);

    /// @brief Get a double64 big endian value from the uint8_t pointer passed in and increment the pointer
    /// @param pBuf Pointer to the buffer
    /// @param pEndStop Pointer to the end of the buffer
    /// @return double64 value
    double getBEdouble64AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop = nullptr);

    /// @brief Set a value into a byte buffer with big or little endian format
    /// @param pBuf Pointer to the buffer
    /// @param offset Offset into the buffer
    /// @param val Value to set
    /// @param numBytes Number of bytes to set
    /// @param bigEndian true if big-endian format
    /// @return New offset
    uint32_t setBytes(uint8_t* pBuf, uint32_t offset, uint64_t val, uint32_t numBytes, bool bigEndian);

    /// @brief Set an int8_t value into a buffer in big-endian format
    /// @param pBuf Pointer to the buffer
    /// @param offset Offset into the buffer
    /// @param val Value to set
    /// @return New offset
    uint32_t setInt8(uint8_t* pBuf, uint32_t offset, int8_t val);
    uint32_t setBEInt8(uint8_t* pBuf, uint32_t offset, int8_t val);

    /// @brief Set a uint8_t value into a buffer in big-endian format
    /// @param pBuf Pointer to the buffer
    /// @param offset Offset into the buffer
    /// @param val Value to set
    /// @return New offset
    uint32_t setUint8(uint8_t* pBuf, uint32_t offset, uint8_t val);
    uint32_t setBEUint8(uint8_t* pBuf, uint32_t offset, uint8_t val);

    /// @brief Set a int16_t value into a buffer in big-endian format
    /// @param pBuf Pointer to the buffer
    /// @param offset Offset into the buffer
    /// @param val Value to set
    /// @return New offset
    uint32_t setBEInt16(uint8_t* pBuf, uint32_t offset, int16_t val);

    /// @brief Set a int16_t value into a buffer in little endian format
    /// @param pBuf Pointer to the buffer
    /// @param offset Offset into the buffer
    /// @param val Value to set
    /// @return New offset
    uint32_t setLEInt16(uint8_t* pBuf, uint32_t offset, int16_t val);

    /// @brief Set a uint16_t value into a buffer in big-endian format
    /// @param pBuf Pointer to the buffer
    /// @param offset Offset into the buffer
    /// @param val Value to set
    /// @return New offset
    uint32_t setBEUint16(uint8_t* pBuf, uint32_t offset, uint16_t val);

    /// @brief Set a uint16_t value into a buffer in little endian format
    /// @param pBuf Pointer to the buffer
    /// @param offset Offset into the buffer
    /// @param val Value to set
    /// @return New offset
    uint32_t setLEUint16(uint8_t* pBuf, uint32_t offset, uint16_t val);

    /// @brief Set a uint32_t value into a buffer in big-endian format
    /// @param pBuf Pointer to the buffer
    /// @param offset Offset into the buffer
    /// @param val Value to set
    /// @return New offset
    uint32_t setBEUint32(uint8_t* pBuf, uint32_t offset, uint32_t val);

    /// @brief Set a uint32_t value into a buffer in little endian format
    /// @param pBuf Pointer to the buffer
    /// @param offset Offset into the buffer
    /// @param val Value to set
    /// @return New offset
    uint32_t setLEUint32(uint8_t* pBuf, uint32_t offset, uint32_t val);

    /// @brief Set a int32_t value into a buffer in big-endian format
    /// @param pBuf Pointer to the buffer
    /// @param offset Offset into the buffer
    /// @param val Value to set
    /// @return New offset
    uint32_t setBEInt32(uint8_t* pBuf, uint32_t offset, int32_t val);

    /// @brief Set a int32_t value into a buffer in little endian format
    /// @param pBuf Pointer to the buffer
    /// @param offset Offset into the buffer
    /// @param val Value to set
    /// @return New offset
    uint32_t setLEInt32(uint8_t* pBuf, uint32_t offset, int32_t val);

    /// @brief Set a uint64_t value into a buffer in big-endian format
    /// @param pBuf Pointer to the buffer
    /// @param offset Offset into the buffer
    /// @param val Value to set
    /// @return New offset
    uint32_t setBEUint64(uint8_t* pBuf, uint32_t offset, uint64_t val);

    /// @brief Set a uint64_t value into a buffer in little endian format
    /// @param pBuf Pointer to the buffer
    /// @param offset Offset into the buffer
    /// @param val Value to set
    /// @return New offset
    uint32_t setLEUint64(uint8_t* pBuf, uint32_t offset, uint64_t val);

    /// @brief Set a int64_t value into a buffer in big-endian format
    /// @param pBuf Pointer to the buffer
    /// @param offset Offset into the buffer
    /// @param val Value to set
    /// @return New offset
    uint32_t setBEInt64(uint8_t* pBuf, uint32_t offset, int64_t val);

    /// @brief Set a int64_t value into a buffer in little endian format
    /// @param pBuf Pointer to the buffer
    /// @param offset Offset into the buffer
    /// @param val Value to set
    /// @return New offset
    uint32_t setLEInt64(uint8_t* pBuf, uint32_t offset, int64_t val);

    /// @brief Set a float (32 bit) value into a buffer in big-endian format
    /// @param pBuf Pointer to the buffer
    /// @param offset Offset into the buffer
    /// @param val Value to set
    /// @return New offset
    uint32_t setBEFloat32(uint8_t* pBuf, uint32_t offset, float val);

    /// @brief Set a float (32 bit) value into a buffer in little endian format
    /// @param pBuf Pointer to the buffer
    /// @param offset Offset into the buffer
    /// @param val Value to set
    /// @return New offset
    uint32_t setLEFloat32(uint8_t* pBuf, uint32_t offset, float val);

    /// @brief Set a double (64 bit) value into a buffer in big-endian format
    /// @param pBuf Pointer to the buffer
    /// @param offset Offset into the buffer
    /// @param val Value to set
    /// @return New offset
    uint32_t setBEDouble64(uint8_t* pBuf, uint32_t offset, double val);

    /// @brief Set a double (64 bit) value into a buffer in little endian format
    /// @param pBuf Pointer to the buffer
    /// @param offset Offset into the buffer
    /// @param val Value to set
    /// @return New offset
    uint32_t setLEDouble64(uint8_t* pBuf, uint32_t offset, double val);

    /// @brief Clamp a value between two values
    /// @param val input value
    /// @param lo lower limit (inclusive)
    /// @param hi upper limit (inclusive)
    /// @return clamped value
    uint32_t clamp(uint32_t val, uint32_t lo, uint32_t hi);

    /// @brief RGB value
    /// @struct RGBValue
    struct RGBValue
    {
        RGBValue()
        {
            r=0; g=0; b=0;
        }
        RGBValue(uint8_t r_, uint8_t g_, uint8_t b_)
        {
            r=r_; g=g_; b=b_;
        }
        String toStr() const
        {
            return String(r) + "," + String(g) + "," + String(b);
        }
        uint8_t r;
        uint8_t g;
        uint8_t b;
    };

    /// @brief Get an RGB value from a hex string which may be in the form of RRGGBB or #RRGGBB
    /// @param colourStr String containing the hex colour
    /// @return RGBValue structure containing the RGB values
    RGBValue getRGBFromHex(const String& colourStr);

    /// @brief Get decimal value of a hex character
    /// @param ch Hex character
    /// @return Decimal value of the hex character
    uint32_t getHexFromChar(int ch);

    /// @brief Extract bytes from hex encoded string
    /// @param inStr Hex encoded string
    /// @param outBuf Buffer to receive the bytes
    /// @param maxOutBufLen Maximum length of the output buffer
    /// @return Number of bytes extracted
    uint32_t getBytesFromHexStr(const char* inStr, uint8_t* outBuf, size_t maxOutBufLen);

    /// @brief Convert a byte array to a hex string (no separator)
    /// @param pBuf Pointer to the byte array
    /// @param bufLen Length of the byte array
    /// @param outStr String to receive the hex string
    void getHexStrFromBytes(const uint8_t* pBuf, uint32_t bufLen, String& outStr);

    /// @brief Get a hex string from byte array
    /// @param pBuf Pointer to the byte array
    /// @param bufLen Length of the byte array
    /// @return Hex string
    String getHexStr(const uint8_t* pBuf, uint32_t bufLen);

    /// @brief Get a zero padded hex string from uint32_t value
    /// @param val Value to convert
    /// @param prefixOx Include 0x prefix
    /// @return Hex string
    String getHexStr(uint32_t val, bool prefixOx);

    /// @brief Get a zero padded hex string from uint16_t value
    /// @param val Value to convert
    /// @param prefixOx Include 0x prefix
    /// @return Hex string
    String getHexStr(uint16_t val, bool prefixOx);

    /// @brief Get a zero padded hex string from uint8_t value
    /// @param val Value to convert
    /// @param prefixOx Include 0x prefix
    /// @return Hex string
    String getHexStr(uint8_t val, bool prefixOx);

    /// @brief Convert a byte array to a hex string
    /// @param pBuf Pointer to the byte array
    /// @param bufLen Length of the byte array
    /// @param outStr String to receive the hex string
    /// @param separator Separator between bytes
    void hexDump(const uint8_t* pBuf, uint32_t bufLen, String& outStr, const char* separator = "");

    /// @brief Generate a hex string from uInt32 with no space between hex digits (e.g. 55aa55aa, etc)
    /// @param pBuf Pointer to the uint32_t array
    /// @param bufLen Length of the array
    /// @param outStr String to receive the hex string
    /// @param separator Separator between hex digits
    void getHexStrFromUint32(const uint32_t* pBuf, uint32_t bufLen, String& outStr, 
                const char* separator);

    /// @brief Log out a buffer in hex format
    /// @param pBuf Pointer to the buffer
    /// @param bufLen Length of the buffer
    /// @param logPrefix Prefix for the log message
    /// @param logIntro Intro for the log message
    void logHexBuf(const uint8_t* pBuf, uint32_t bufLen, const char* logPrefix, const char* logIntro);

    /// @brief Get string from part of buffer with optional hex and ascii
    /// @param pBuf Pointer to the buffer
    /// @param bufLen Length of the buffer
    /// @param includeHex Include hex in the output
    /// @param includeAscii Include ascii in the output
    /// @return String containing the buffer contents
    String getBufStrHexAscii(const void* pBuf, uint32_t bufLen, bool includeHex, bool includeAscii);

    /// @brief Convert IP string to IP address bytes
    /// @param inStr Input string
    /// @param pIpAddr Pointer to the IP address bytes
    /// @return true if the conversion was successful
    /// @note This code from Unix sources
    unsigned long convIPStrToAddr(String &inStr);

    /// @brief Format MAC address
    /// @param pMacAddr Pointer to the MAC address bytes
    /// @param separator Separator between MAC address bytes
    /// @return Formatted MAC address
    String formatMACAddr(const uint8_t* pMacAddr, const char* separator);

    /// @brief Find match in buffer (like strstr for unterminated strings)
    /// @param pBuf Pointer to the buffer
    /// @param bufLen Length of the buffer
    /// @param pToFind Pointer to the string to find
    /// @param toFindLen Length of the string to find
    /// @return Position in buffer of val or -1 if not found
    int findInBuf(const uint8_t* pBuf, uint32_t bufLen, 
                const uint8_t* pToFind, uint32_t toFindLen);

    /// @brief Parse a string into a list of integers
    /// @param pInStr Pointer to the input string
    /// @param outList List to receive the integers
    /// @param pSep Separator string
    void parseIntList(const char* pInStr, std::vector<int>& outList, const char* pSep = ",");

    /// @brief Get string for RaftRetCode
    /// @param retc RaftRetCode value
    const char* getRetCodeStr(RaftRetCode retc);

    /// @brief Convert UUID128 string to uint8_t array
    /// @param uuid128Str UUID128 string
    /// @param pUUID128 Pointer to the UUID128 array
    /// @param reverseOrder Reverse the order of the UUID128 array
    /// @return true if successful
    static const uint32_t UUID128_BYTE_COUNT = 16;
    bool uuid128FromString(const char* uuid128Str, uint8_t* pUUID128, bool reverseOrder = true);

    /// @brief Trim a String including removing trailing null terminators
    /// @param str String to trim
    void trimString(String& str);

    /// @brief Format a string with a variable number of arguments
    /// @param maxLen Maximum length of the formatted string
    /// @param fmt Format string
    /// @param ... Variable arguments
    /// @return Formatted string
    String formatString(uint32_t maxLen, const char* fmt, ...);
};

/// @brief Name value pair double
/// @struct NameValuePairDouble
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


