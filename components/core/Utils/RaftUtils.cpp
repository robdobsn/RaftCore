/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftUtils
//
// Rob Dobson 2012-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <limits.h>
#include "Logger.h"
#include "RaftUtils.h"

#ifndef INADDR_NONE
#define INADDR_NONE         ((uint32_t)0xffffffffUL)
#endif
#include <stdarg.h>

// Debug
// #define DEBUG_EXTRACT_NAME_VALUES
#ifdef DEBUG_EXTRACT_NAME_VALUES
static const char* MODULE_PREFIX = "Utils";
#endif

/// @brief Check if a time limit has expired (taking into account counter wrapping)
/// @param curTime Current time (in the same units as other parameters)
/// @param lastTime Time of last event
/// @param maxDuration Maximum duration before timeout
/// @return true if the time limit has expired
/// @example isTimeout(millis(), myLastTime, 1000) - returns true if myLastTime was set to millis() more than 1000 milliseconds ago
bool Raft::isTimeout(uint64_t curTime, uint64_t lastTime, uint64_t maxDuration)
{
    if (curTime >= lastTime)
    {
        return (curTime > lastTime + maxDuration);
    }
    return (ULONG_LONG_MAX - (lastTime - curTime) > maxDuration);
}

/// @brief Calculate the before a time-out occurs (handling counter wrapping)
/// @param curTime Current time (in the same units as other parameters)
/// @param lastTime Time of last event
/// @param maxDuration Maximum duration before timeout
/// @return Time before timeout (0 if already expired)
uint64_t Raft::timeToTimeout(uint64_t curTime, uint64_t lastTime, uint64_t maxDuration)
{
    if (curTime >= lastTime)
    {
        if (curTime > lastTime + maxDuration)
        {
            return 0;
        }
        return maxDuration - (curTime - lastTime);
    }
    if (ULONG_LONG_MAX - (lastTime - curTime) > maxDuration)
    {
        return 0;
    }
    return maxDuration - (ULONG_LONG_MAX - (lastTime - curTime));
}

/// @brief Calculate the time elapsed since a timer started (handling counter wrapping)
/// @param curTime Current time (in the same units as other parameters)
/// @param lastTime Timer started
/// @return Time elapsed
uint64_t Raft::timeElapsed(uint64_t curTime, uint64_t lastTime)
{
    if (curTime >= lastTime)
        return curTime - lastTime;
    return (ULONG_LONG_MAX - lastTime) + 1 + curTime;
}

/// @brief Set results for JSON comms to a bool value
/// @param pReq Request string
/// @param resp Response string
/// @param rslt Result value
/// @param otherJson Additional JSON to add to the response
/// @return RaftRetCode
RaftRetCode Raft::setJsonBoolResult(const char* pReq, String& resp, bool rslt, const char* otherJson)
{
    String additionalJson = ((otherJson) && (otherJson[0] != '\0')) ? otherJson + String(",") : "";
    String reqStr = escapeString(pReq, true);
    resp = "{\"req\":\"" + reqStr + "\"," + additionalJson + String("\"rslt\":") + (rslt ? "\"ok\"}" : "\"fail\"}");
    return rslt ? RaftRetCode::RAFT_OK : RaftRetCode::RAFT_OTHER_FAILURE;
}

/// @brief Set results for JSON comms with an error message
/// @param pReq Request string
/// @param resp Response string
/// @param errorMsg Error message
/// @param otherJson Additional JSON to add to the response
/// @return RaftRetCode
RaftRetCode Raft::setJsonErrorResult(const char* pReq, String& resp, const char* errorMsg, const char* otherJson, RaftRetCode retCode)
{
    String additionalJson = ((otherJson) && (otherJson[0] != 0)) ? otherJson + String(",") : "";
    String errorMsgStr = errorMsg ? errorMsg : "Unknown error";
    String reqStr = escapeString(pReq, true);
    resp = "{\"req\":\"" + reqStr + "\"," + additionalJson + "\"rslt\":\"fail\",\"error\":\"" + errorMsgStr + "\"}";
    return retCode;
}

/// @brief Set results for JSON comms with result type, error message and additional JSON
/// @param pReq Request string
/// @param resp Response string
/// @param rslt Result value
/// @param errorMsg Error message
/// @param otherJson Additional JSON to add to the response
/// @return RaftRetCode
RaftRetCode Raft::setJsonResult(const char* pReq, String& resp, bool rslt, const char* errorMsg, const char* otherJson)
{
    if (rslt)
        setJsonBoolResult(pReq, resp, rslt, otherJson);
    else
        setJsonErrorResult(pReq, resp, errorMsg, otherJson);
    return rslt ? RaftRetCode::RAFT_OK : RaftRetCode::RAFT_OTHER_FAILURE;
}

/// @brief Escape string using hex character encoding for control characters
/// @param pStr Input string
/// @param escapeQuotesToBackslashQuotes true if quotes should be escaped to backslash quotes (otherwise to hex)
/// @return Escaped string
String Raft::escapeString(const char* pStr, bool escapeQuotesToBackslashQuotes)
{
    if (!pStr)
        return "";
    String outStr;
    // Reserve a bit more than the inStr length
    outStr.reserve((strlen(pStr) * 3) / 2);
    // Replace chars with escapes as needed
    while (*pStr != '\0')
    {
        int c = *pStr;
        if (c == '"' || c == '\\' || ('\x00' <= c && c <= '\x1f'))
        {
            if (escapeQuotesToBackslashQuotes && c == '"')
            {
                outStr += "\\\"";
            }
            else
            {
                outStr += "\\u";
                String cx = String(c, 16);
                for (unsigned int j = 0; j < 4-cx.length(); j++)
                    outStr += "0";
                outStr += cx;
            }
        }
        else
        {
            outStr += (char)c;
        }
        pStr++;
    }
    return outStr;
}

/// @brief Unescape string handling hex character encoding for control characters
/// @param pStr Input string
/// @return Escaped string
String Raft::unescapeString(const char* pStr)
{
    String outStr;
    // Reserve inStr length
    uint32_t inStrLen = strlen(pStr);
    outStr.reserve(inStrLen);
    // Replace escapes with chars
    while (*pStr != '\0')
    {
        int c = *pStr;
        if (c == '\\')
        {
            pStr++;
            if (*pStr == 0)
                break;
            if (*pStr == 'u')
            {
                pStr++;
                String cx = "";
                for (unsigned int j = 0; j < 4; j++)
                {
                    if (*pStr == 0)
                        break;
                    cx += *pStr;
                    pStr++;
                }
                c = strtol(cx.c_str(), NULL, 16);
            }
            else if (*pStr == 'x')
            {
                pStr++;
                String cx = "";
                for (unsigned int j = 0; j < 2; j++)
                {
                    if (*pStr == 0)
                        break;
                    cx += *pStr;
                    pStr++;
                }
                c = strtol(cx.c_str(), NULL, 16);
            }
            else if (*pStr == 'n')
                c = '\n';
            else if (*pStr == 'r')
                c = '\r';
            else if (*pStr == 't')
                c = '\t';
            else if (*pStr == 'b')
                c = '\b';
            else if (*pStr == 'f')
                c = '\f';
            else if (*pStr == '"')
                c = '"';
            else if (*pStr == '\\')
                c = '\\';
            else
                c = 0;
        }
        outStr += (char)c;
        pStr++;
    }
    return outStr;
}

/// @brief Convert HTTP query format to JSON
/// @param inStr Input string
/// @param mustStartWithQuestionMark true if the input string must start with a question mark
/// @return JSON string (only contains name/value pairs and not {})
String Raft::getJSONFromHTTPQueryStr(const char* inStr, bool mustStartWithQuestionMark, bool includeBraces)
{
    String outStr;
    static const uint32_t MAX_HTTP_QUERY_LEN = 4096;
    outStr.reserve((strnlen(inStr, MAX_HTTP_QUERY_LEN) * 3) / 2);
    const char* pStr = inStr;
    bool isActive = !mustStartWithQuestionMark;
    String curName;
    String curVal;
    bool inValue = false;
    while (*pStr)
    {
        // Handle start of query
        if (!isActive)
        {
            if (*pStr != '?')
            {
                pStr++;
                continue;
            }
            isActive = true;
        }
        
        // Ignore ? symbol
        if (*pStr == '?')
        {
            pStr++;
            continue;
        }

        // Check for separator values
        if (*pStr == '=')
        {
            inValue = true;
            curVal = "";
            pStr++;
            continue;
        }

        // Check for delimiter
        if (*pStr == '&')
        {
            // Store value
            if (inValue && (curName.length() > 0))
            {
                if (outStr.length() > 0)
                    outStr += ",";
                outStr += "\"" + curName + "\":" + "\"" + curVal + "\"";
            }
            inValue = false;
            curName = "";
            pStr++;
            continue;
        }

        // Form name or value
        if (inValue)
            curVal += *pStr;
        else
            curName += *pStr;
        pStr++;
    }

    // Finish up
    if (inValue && (curName.length() > 0))
    {
        if (outStr.length() > 0)
            outStr += ",";
        outStr += "\"" + curName + "\":" + "\"" + curVal + "\"";
    }
    return includeBraces ? "{" + outStr + "}" : outStr;
}

/// @brief Get Nth field from delimited string
/// @param inStr Input string
/// @param N Field number
/// @param separator Field separator character
/// @return Nth field
String Raft::getNthField(const char* inStr, int N, char separator)
{
	String retStr;
	// Find separators
	const char* pStr = inStr;
	int len = -1;
	const char* pField = NULL;
	int sepIdx = 0;
	// Find the field and length
	while (true)
	{
		// Check if this is the start of the field we want
		if ((sepIdx == N) && (*pStr != '\0') && (pField == NULL))
			pField = pStr;
		// Check for separator (or end)
		if ((*pStr == separator) || (*pStr == '\0'))
			sepIdx++;
		// See if we have found the end point of the field
		if (sepIdx == N + 1)
		{
			len = pStr - pField;
			break;
		}
		// End?
		if (*pStr == '\0')
			break;
		// Bump
		pStr++;
	}
	// Return if invalid
	if ((pField == NULL) || (len == -1))
		return retStr;
	// Create buffer for the string
	char* pTmpStr = new char[len + 1];
	if (!pTmpStr)
		return retStr;
	memcpy(pTmpStr, pField, len);
	pTmpStr[len] = 0;
	retStr = pTmpStr;
	delete[] pTmpStr;
	return retStr;
}

/// @brief Get a uint8_t value from the uint8_t pointer passed in and increment the pointer
/// @param pBuf Pointer to the buffer
/// @param pEndStop Pointer to the end of the buffer
/// @return uint8_t value
uint16_t Raft::getUInt8AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop)
{
    const size_t varSize = sizeof(uint8_t);
    if (!pBuf || (pEndStop && (pBuf >= pEndStop)))
        return 0;
    uint8_t val = *pBuf;
    pBuf += varSize;
    return val;
}

/// @brief Get a int8_t value from the uint8_t pointer passed in and increment the pointer
/// @param pBuf Pointer to the buffer
/// @param pEndStop Pointer to the end of the buffer
/// @return int8_t value
int16_t Raft::getInt8AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop)
{
    const size_t  varSize = sizeof(int8_t);
    if (!pBuf || (pEndStop && (pBuf >= pEndStop)))
        return 0;
    int8_t val = *((int8_t*)pBuf);
    pBuf += varSize;
    return val;
}

/// @brief Get a uint16_t little endian value from the uint8_t pointer passed in and increment the pointer
/// @param pBuf Pointer to the buffer
/// @param pEndStop Pointer to the end of the buffer
/// @return uint16_t value
uint16_t Raft::getLEUInt16AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop)
{
    const size_t  varSize = sizeof(uint16_t);
    if (!pBuf || (pEndStop && (pBuf + varSize > pEndStop)))
        return 0;
    uint16_t val = static_cast<uint16_t>(pBuf[0]) | (static_cast<uint16_t>(pBuf[1]) << 8);
    pBuf += varSize;
    return val;
}

/// @brief Get a int16_t little endian value from the uint8_t pointer passed in and increment the pointer
/// @param pBuf Pointer to the buffer
/// @param pEndStop Pointer to the end of the buffer
/// @return int16_t value
int16_t Raft::getLEInt16AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop)
{
    const size_t  varSize = sizeof(int16_t);
    if (!pBuf || (pEndStop && (pBuf + varSize > pEndStop)))
        return 0;
    int16_t val = pBuf[0] | (pBuf[1] << 8);
    pBuf += varSize;
    return val;
}

/// @brief Get a uint16_t big endian value from the uint8_t pointer passed in and increment the pointer
/// @param pBuf Pointer to the buffer
/// @param pEndStop Pointer to the end of the buffer
/// @return uint16_t value
uint16_t Raft::getBEUInt16AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop)
{
    const size_t  varSize = sizeof(uint16_t);
    if (!pBuf || (pEndStop && (pBuf + varSize > pEndStop)))
        return 0;
    uint16_t val = (pBuf[0] << 8) | pBuf[1];
    pBuf += varSize;
    return val;
}

/// @brief Get a int16_t big endian value from the uint8_t pointer passed in and increment the pointer
/// @param pBuf Pointer to the buffer
/// @param pEndStop Pointer to the end of the buffer
/// @return int16_t value
int16_t Raft::getBEInt16AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop)
{
    const size_t  varSize = sizeof(int16_t);
    if (!pBuf || (pEndStop && (pBuf + varSize > pEndStop)))
        return 0;
    int16_t val = (pBuf[0] << 8) | pBuf[1];
    pBuf += varSize;
    return val;
}

/// @brief Get a uint32_t little endian value from the uint8_t pointer passed in and increment the pointer
/// @param pBuf Pointer to the buffer
/// @param pEndStop Pointer to the end of the buffer
/// @return uint32_t value
uint32_t Raft::getLEUInt32AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop)
{
    const size_t  varSize = sizeof(uint32_t);
    if (!pBuf || (pEndStop && (pBuf + varSize > pEndStop)))
        return 0;
    uint32_t val = pBuf[0] | (pBuf[1] << 8) | (pBuf[2] << 16) | (pBuf[3] << 24);
    pBuf += varSize;
    return val;
}

/// @brief Get a int32_t little endian value from the uint8_t pointer passed in and increment the pointer
/// @param pBuf Pointer to the buffer
/// @param pEndStop Pointer to the end of the buffer
/// @return int32_t value
int32_t Raft::getLEInt32AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop)
{
    const size_t  varSize = sizeof(int32_t);
    if (!pBuf || (pEndStop && (pBuf + varSize > pEndStop)))
        return 0;
    int32_t val = pBuf[0] | (pBuf[1] << 8) | (pBuf[2] << 16) | (pBuf[3] << 24);
    pBuf += varSize;
    return val;
}

/// @brief Get a uint32_t big endian value from the uint8_t pointer passed in and increment the pointer
/// @param pBuf Pointer to the buffer
/// @param pEndStop Pointer to the end of the buffer
/// @return uint32_t value
uint32_t Raft::getBEUInt32AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop)
{
    const size_t  varSize = sizeof(uint32_t);
    if (!pBuf || (pEndStop && (pBuf + varSize > pEndStop)))
        return 0;
    uint32_t val = (pBuf[0] << 24) | (pBuf[1] << 16) | (pBuf[2] << 8) | pBuf[3];
    pBuf += varSize;
    return val;
}

/// @brief Get a int32_t big endian value from the uint8_t pointer passed in and increment the pointer
/// @param pBuf Pointer to the buffer
/// @param pEndStop Pointer to the end of the buffer
/// @return int32_t value
int32_t Raft::getBEInt32AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop)
{
    const size_t  varSize = sizeof(int32_t);
    if (!pBuf || (pEndStop && (pBuf + varSize > pEndStop)))
        return 0;
    int32_t val = (pBuf[0] << 24) | (pBuf[1] << 16) | (pBuf[2] << 8) | pBuf[3];
    pBuf += varSize;
    return val;
}

/// @brief Get a uint64_t little endian value from the uint8_t pointer passed in and increment the pointer
/// @param pBuf Pointer to the buffer
/// @param pEndStop Pointer to the end of the buffer
/// @return uint64_t value
uint64_t Raft::getLEUInt64AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop)
{
    const size_t  varSize = sizeof(uint64_t);
    if (!pBuf || (pEndStop && (pBuf + varSize > pEndStop)))
        return 0;
    uint64_t val = 0;
    for (size_t i = 0; i < sizeof(uint64_t); ++i) {
        val |= static_cast<uint64_t>(pBuf[i]) << (8 * i);
    }
    pBuf += varSize;
    return val;
}

/// @brief Get a int64_t little endian value from the uint8_t pointer passed in and increment the pointer
/// @param pBuf Pointer to the buffer
/// @param pEndStop Pointer to the end of the buffer
/// @return int64_t value
int64_t Raft::getLEInt64AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop)
{
    const size_t  varSize = sizeof(int64_t);
    if (!pBuf || (pEndStop && (pBuf + varSize > pEndStop)))
        return 0;
    int64_t val = 0;
    for (size_t i = 0; i < sizeof(int64_t); ++i) {
        val |= static_cast<uint64_t>(pBuf[i]) << (8 * i);
    }
    pBuf += varSize;
    return val;
}

/// @brief Get a uint64_t big endian value from the uint8_t pointer passed in and increment the pointer
/// @param pBuf Pointer to the buffer
/// @param pEndStop Pointer to the end of the buffer
/// @return uint64_t value
uint64_t Raft::getBEUInt64AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop)
{
    const size_t  varSize = sizeof(uint64_t);
    if (!pBuf || (pEndStop && (pBuf + varSize > pEndStop)))
        return 0;
    uint64_t val = 0;
    for (size_t i = 0; i < sizeof(int64_t); ++i) {
        val = (val << 8) | pBuf[i];
    }
    pBuf += varSize;
    return val;
}

/// @brief Get a int64_t big endian value from the uint8_t pointer passed in and increment the pointer
/// @param pBuf Pointer to the buffer
/// @param pEndStop Pointer to the end of the buffer
/// @return int64_t value
int64_t Raft::getBEInt64AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop)
{
    const size_t  varSize = sizeof(int64_t);
    if (!pBuf || (pEndStop && (pBuf + varSize > pEndStop)))
        return 0;
    int64_t val = 0;
    for (size_t i = 0; i < sizeof(int64_t); ++i) {
        val = (val << 8) | pBuf[i];
    }
    pBuf += varSize;
    return val;
}

/// @brief Get a float32_t little endian value from the uint8_t pointer passed in and increment the pointer
/// @param pBuf Pointer to the buffer
/// @param pEndStop Pointer to the end of the buffer
/// @return float32_t value
float Raft::getLEfloat32AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop)
{
    uint32_t temp = getLEUInt32AndInc(pBuf, pEndStop);
    float val;
    memcpy(&val, &temp, sizeof(val));
    return val;
}

/// @brief Get a float32_t big endian value from the uint8_t pointer passed in and increment the pointer
/// @param pBuf Pointer to the buffer
/// @param pEndStop Pointer to the end of the buffer
/// @return float32_t value
float Raft::getBEfloat32AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop)
{
    uint32_t temp = getBEUInt32AndInc(pBuf, pEndStop);
    float val;
    memcpy(&val, &temp, sizeof(val));
    return val;
}

/// @brief Get a double64 little endian value from the uint8_t pointer passed in and increment the pointer
/// @param pBuf Pointer to the buffer
/// @param pEndStop Pointer to the end of the buffer
/// @return double64 value
double Raft::getLEdouble64AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop)
{
    uint64_t temp = getLEUInt64AndInc(pBuf, pEndStop);
    double val;
    memcpy(&val, &temp, sizeof(val));
    return val;
}

/// @brief Get a double64 big endian value from the uint8_t pointer passed in and increment the pointer
/// @param pBuf Pointer to the buffer
/// @param pEndStop Pointer to the end of the buffer
/// @return double64 value
double Raft::getBEdouble64AndInc(const uint8_t*& pBuf, const uint8_t* pEndStop)
{
    uint64_t temp = getBEUInt64AndInc(pBuf, pEndStop);
    double val;
    memcpy(&val, &temp, sizeof(val));
    return val;
}

/// @brief Set a value into a byte buffer with big or little endian format
/// @param pBuf Pointer to the buffer
/// @param offset Offset into the buffer
/// @param val Value to set
/// @param numBytes Number of bytes to set
/// @param bigEndian true if big-endian format
/// @return New offset
uint32_t Raft::setBytes(uint8_t* pBuf, uint32_t offset, uint64_t val, uint32_t numBytes, bool bigEndian)
{
    if (!pBuf)
        return offset;
    if (bigEndian)
    {
        for (uint32_t i = 0; i < numBytes; ++i)
        {
            pBuf[offset + i] = (val >> (8 * (numBytes - i - 1))) & 0xff;
        }
    }
    else
    {
        for (uint32_t i = 0; i < numBytes; ++i)
        {
            pBuf[offset + i] = (val >> (8 * i)) & 0xff;
        }
    }
    return offset + numBytes;
}

/// @brief Set an int8_t value into a buffer
/// @param pBuf Pointer to the buffer
/// @param offset Offset into the buffer
/// @param val Value to set
/// @return New offset
uint32_t Raft::setInt8(uint8_t* pBuf, uint32_t offset, int8_t val)
{
    if (!pBuf)
        return offset;
    *(pBuf+offset) = val;
    return offset + sizeof(int8_t);
}

/// @return New offset
uint32_t Raft::setBEInt8(uint8_t* pBuf, uint32_t offset, int8_t val)
{
    if (!pBuf)
        return offset;
    *(pBuf+offset) = val;
    return offset + sizeof(int8_t);
}

/// @brief Set an uint8_t value into a buffer
/// @param pBuf Pointer to the buffer
/// @param offset Offset into the buffer
/// @param val Value to set
/// @return New offset
uint32_t Raft::setUInt8(uint8_t* pBuf, uint32_t offset, uint8_t val)
{
    if (!pBuf)
        return offset;
    *(pBuf+offset) = val;
    return offset + sizeof(uint8_t);
}

/// @return New offset
uint32_t Raft::setBEUInt8(uint8_t* pBuf, uint32_t offset, uint8_t val)
{
    if (!pBuf)
        return offset;
    *(pBuf+offset) = val;
    return offset + sizeof(uint8_t);
}

/// @brief Set a int16_t value into a buffer in big-endian format
/// @param pBuf Pointer to the buffer
/// @param offset Offset into the buffer
/// @param val Value to set
/// @return New offset
uint32_t Raft::setBEInt16(uint8_t* pBuf, uint32_t offset, int16_t val)
{
    if (!pBuf)
        return offset;
    *(pBuf+offset) = (val >> 8) & 0x0ff;
    *(pBuf+offset+1) = val & 0xff;
    return offset + sizeof(int16_t);
}

/// @brief Set a int16_t value into a buffer in little endian format
/// @param pBuf Pointer to the buffer
/// @param offset Offset into the buffer
/// @param val Value to set
/// @return New offset
uint32_t Raft::setLEInt16(uint8_t* pBuf, uint32_t offset, int16_t val)
{
    if (!pBuf)
        return offset;
    *(pBuf+offset) = val & 0xff;
    *(pBuf+offset+1) = (val >> 8) & 0x0ff;
    return offset + sizeof(int16_t);
}

/// @brief Set a uint16_t value into a buffer in big-endian format
/// @param pBuf Pointer to the buffer
/// @param offset Offset into the buffer
/// @param val Value to set
/// @return New offset
uint32_t Raft::setBEUInt16(uint8_t* pBuf, uint32_t offset, uint16_t val)
{
    if (!pBuf)
        return offset;
    *(pBuf+offset) = (val >> 8) & 0x0ff;
    *(pBuf+offset+1) = val & 0xff;
    return offset + sizeof(uint16_t);
}

/// @brief Set a uint16_t value into a buffer in little endian format
/// @param pBuf Pointer to the buffer
/// @param offset Offset into the buffer
/// @param val Value to set
/// @return New offset
uint32_t Raft::setLEUInt16(uint8_t* pBuf, uint32_t offset, uint16_t val)
{
    if (!pBuf)
        return offset;
    *(pBuf+offset) = val & 0xff;
    *(pBuf+offset+1) = (val >> 8) & 0x0ff;
    return offset + sizeof(uint16_t);
}

/// @brief Set a uint32_t value into a buffer in big endian format
/// @param pBuf Pointer to the buffer
/// @param offset Offset into the buffer
/// @param val Value to set
/// @return New offset
uint32_t Raft::setBEUInt32(uint8_t* pBuf, uint32_t offset, uint32_t val)
{
    if (!pBuf)
        return offset;
    *(pBuf+offset) = (val >> 24) & 0x0ff;
    *(pBuf+offset+1) = (val >> 16) & 0x0ff;
    *(pBuf+offset+2) = (val >> 8) & 0x0ff;
    *(pBuf+offset+3) = val & 0xff;
    return offset + sizeof(uint32_t);
}

/// @brief Set a uint32_t value into a buffer in little endian format
/// @param pBuf Pointer to the buffer
/// @param offset Offset into the buffer
/// @param val Value to set
/// @return New offset
uint32_t Raft::setLEUInt32(uint8_t* pBuf, uint32_t offset, uint32_t val)
{
    if (!pBuf)
        return offset;
    *(pBuf+offset) = val & 0xff;
    *(pBuf+offset+1) = (val >> 8) & 0x0ff;
    *(pBuf+offset+2) = (val >> 16) & 0x0ff;
    *(pBuf+offset+3) = (val >> 24) & 0xff;
    return offset + sizeof(uint32_t);
}

/// @brief Set a int32_t value into a buffer in big endian format
/// @param pBuf Pointer to the buffer
/// @param offset Offset into the buffer
/// @param val Value to set
/// @return New offset
uint32_t Raft::setBEInt32(uint8_t* pBuf, uint32_t offset, int32_t val)
{
    if (!pBuf)
        return offset;
    *(pBuf+offset) = (val >> 24) & 0x0ff;
    *(pBuf+offset+1) = (val >> 16) & 0x0ff;
    *(pBuf+offset+2) = (val >> 8) & 0x0ff;
    *(pBuf+offset+3) = val & 0xff;
    return offset + sizeof(int32_t);
}

/// @brief Set a int32_t value into a buffer in little endian format
/// @param pBuf Pointer to the buffer
/// @param offset Offset into the buffer
/// @param val Value to set
/// @return New offset
uint32_t Raft::setLEInt32(uint8_t* pBuf, uint32_t offset, int32_t val)
{
    if (!pBuf)
        return offset;
    *(pBuf+offset) = val & 0xff;
    *(pBuf+offset+1) = (val >> 8) & 0x0ff;
    *(pBuf+offset+2) = (val >> 16) & 0x0ff;
    *(pBuf+offset+3) = (val >> 24) & 0xff;
    return offset + sizeof(int32_t);
}

/// @brief Set a uint64_t value into a buffer in big endian format
/// @param pBuf Pointer to the buffer
/// @param offset Offset into the buffer
/// @param val Value to set
/// @return New offset
uint32_t Raft::setBEUInt64(uint8_t* pBuf, uint32_t offset, uint64_t val)
{
    if (!pBuf)
        return offset;
    for (size_t i = 0; i < sizeof(uint64_t); ++i) {
        pBuf[offset + i] = (val >> (8 * (sizeof(uint64_t) - i - 1))) & 0xff;
    }
    return offset + sizeof(uint64_t);
}

/// @brief Set a uint64_t value into a buffer in little endian format
/// @param pBuf Pointer to the buffer
/// @param offset Offset into the buffer
/// @param val Value to set
/// @return New offset
uint32_t Raft::setLEUInt64(uint8_t* pBuf, uint32_t offset, uint64_t val)
{
    if (!pBuf)
        return offset;
    for (size_t i = 0; i < sizeof(uint64_t); ++i) {
        pBuf[offset + i] = (val >> (8 * i)) & 0xff;
    }
    return offset + sizeof(uint64_t);
}

/// @brief Set a int64_t value into a buffer in big endian format
/// @param pBuf Pointer to the buffer
/// @param offset Offset into the buffer
/// @param val Value to set
/// @return New offset
uint32_t Raft::setBEInt64(uint8_t* pBuf, uint32_t offset, int64_t val)
{
    if (!pBuf)
        return offset;
    for (size_t i = 0; i < sizeof(int64_t); ++i) {
        pBuf[offset + i] = (val >> (8 * (sizeof(int64_t) - i - 1))) & 0xff;
    }
    return offset + sizeof(int64_t);
}

/// @brief Set a int64_t value into a buffer in little endian format
/// @param pBuf Pointer to the buffer
/// @param offset Offset into the buffer
/// @param val Value to set
/// @return New offset
uint32_t Raft::setLEInt64(uint8_t* pBuf, uint32_t offset, int64_t val)
{
    if (!pBuf)
        return offset;
    for (size_t i = 0; i < sizeof(int64_t); ++i) {
        pBuf[offset + i] = (val >> (8 * i)) & 0xff;
    }
    return offset + sizeof(int64_t);
}

/// @brief Set a float32_t value into a buffer in big endian format
/// @param pBuf Pointer to the buffer
/// @param offset Offset into the buffer
/// @param val Value to set
/// @return New offset
uint32_t Raft::setBEFloat32(uint8_t* pBuf, uint32_t offset, float val)
{
    if (!pBuf)
        return offset;
    uint8_t* pFloat = (uint8_t*)(&val)+3;
    uint8_t* pBufOff = pBuf + offset;
    for (int i = 0; i < 4; i++)
        *pBufOff++ = *pFloat--;
    return offset + sizeof(float);
}

/// @brief Set a float32_t value into a buffer in little endian format
/// @param pBuf Pointer to the buffer
/// @param offset Offset into the buffer
/// @param val Value to set
/// @return New offset
uint32_t Raft::setLEFloat32(uint8_t* pBuf, uint32_t offset, float val)
{
    if (!pBuf)
        return offset;
    uint8_t* pFloat = (uint8_t*)(&val);
    uint8_t* pBufOff = pBuf + offset;
    for (int i = 0; i < 4; i++)
        *pBufOff++ = *pFloat++;
    return offset + sizeof(float);
}

/// @brief Set a double64 value into a buffer in big endian format
/// @param pBuf Pointer to the buffer
/// @param offset Offset into the buffer
/// @param val Value to set
/// @return New offset
uint32_t Raft::setBEDouble64(uint8_t* pBuf, uint32_t offset, double val)
{
    if (!pBuf)
        return offset;
    uint8_t* pDouble = (uint8_t*)(&val)+7;
    uint8_t* pBufOff = pBuf + offset;
    for (int i = 0; i < 8; i++)
        *pBufOff++ = *pDouble--;
    return offset + sizeof(double);
}

/// @brief Set a double64 value into a buffer in little endian format
/// @param pBuf Pointer to the buffer
/// @param offset Offset into the buffer
/// @param val Value to set
/// @return New offset
uint32_t Raft::setLEDouble64(uint8_t* pBuf, uint32_t offset, double val)
{
    if (!pBuf)
        return offset;
    uint8_t* pDouble = (uint8_t*)(&val);
    uint8_t* pBufOff = pBuf + offset;
    for (int i = 0; i < 8; i++)
        *pBufOff++ = *pDouble++;
    return offset + sizeof(double);
}

/// @brief Clamp a value between two values
/// @param val input value
/// @param lo lower limit (inclusive)
/// @param hi upper limit (inclusive)
/// @return clamped value
uint32_t Raft::clamp(uint32_t val, uint32_t lo, uint32_t hi)
{
    if (val < lo)
        val = lo;
    if (val > hi)
        val = hi;
    return val;
}

/// @brief Get an RGB value from a hex string which may be in the form of RRGGBB or #RRGGBB
/// @param colourStr String containing the hex colour
/// @return RGBValue structure containing the RGB values
Raft::RGBValue Raft::getRGBFromHex(const String& colourStr)
{
    uint32_t colourRGB = strtoul(colourStr.startsWith("#") ? colourStr.c_str() + 1 : colourStr.c_str(), nullptr, 16);
    return RGBValue((colourRGB >> 16) & 0xff, (colourRGB >> 8) & 0xff, colourRGB & 0xff);
}

/// @brief Get decimal value of a hex character
/// @param ch Hex character
/// @return Decimal value of the hex character
uint32_t Raft::getHexFromChar(int ch)
{
    ch = toupper(ch);
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    else if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get bytes from hex-encoded string
/// @param inStr Input string
/// @param outBuf Buffer to receive the bytes
/// @param maxOutBufLen Maximum length of the output buffer
/// @return Number of bytes copied
uint32_t Raft::getBytesFromHexStr(const char* inStr, uint8_t* outBuf, size_t maxOutBufLen)
{
    // Check valid
    if (!inStr)
        return 0;

    // Skip initial "0x" if present
    if (inStr[0] == '0' && (inStr[1] == 'x' || inStr[1] == 'X'))
        inStr += 2;

    // Clear initially
    uint32_t inStrLen = strnlen(inStr, maxOutBufLen * 2);
    uint32_t numBytes = maxOutBufLen < inStrLen / 2 ? maxOutBufLen : inStrLen / 2;
    uint32_t posIdx = 0;
    for (uint32_t byteIdx = 0; byteIdx < numBytes; byteIdx++)
    {
        uint32_t nyb0Idx = (inStr[posIdx++] & 0x1F) ^ 0x10;
        uint32_t nyb1Idx = (inStr[posIdx++] & 0x1F) ^ 0x10;
        outBuf[byteIdx] = (__RAFT_CHAR_TO_NYBBLE[nyb0Idx] << 4) + __RAFT_CHAR_TO_NYBBLE[nyb1Idx];
    };
    return numBytes;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get bytes from hex-encoded string
/// @param inStr Input string
/// @param maxOutBufLen Maximum number of bytes to return
/// @return Vector containing the decoded bytes (up to maxOutBufLen)
std::vector<uint8_t> Raft::getBytesFromHexStr(const char* inStr, size_t maxOutBufLen)
{
    // Check valid
    if (!inStr)
        return std::vector<uint8_t>();

    // Skip initial "0x" if present
    if (inStr[0] == '0' && (inStr[1] == 'x' || inStr[1] == 'X'))
        inStr += 2;

    // Ensure input string has an even number of characters
    size_t inStrLen = strnlen(inStr, maxOutBufLen * 2);

    // Determine number of bytes to decode, limited by maxOutBufLen
    size_t numBytes = std::min(inStrLen / 2, maxOutBufLen);
    std::vector<uint8_t> outBuf(numBytes);
    getBytesFromHexStr(inStr, outBuf.data(), numBytes);
    return outBuf;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Convert a byte array to a hex string (no separator)
/// @param pBuf Pointer to the byte array
/// @param bufLen Length of the byte array
/// @param outStr String to receive the hex string
void Raft::getHexStrFromBytes(const uint8_t* pBuf, uint32_t bufLen, String& outStr)
{
    hexDump(pBuf, bufLen, outStr, "");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get a hex string from byte array
/// @param pBuf Pointer to the byte array
/// @param bufLen Length of the byte array
/// @param offset Offset into the buffer
/// @param sep Separator between bytes
/// @param offset Offset into the buffer
/// @param maxBytes Maximum number of bytes to include (-1 for all)
/// @return Hex string
String Raft::getHexStr(const uint8_t* pBuf, uint32_t bufLen, const char* pSep, uint32_t offset, int maxBytes)
{
    String outStr;
    hexDump(pBuf, bufLen, outStr, pSep, offset, maxBytes);
    return outStr;
}

/// @brief Get a hex string from a uint8_t vector
/// @param inVec Input vector
/// @param offset Offset into the vector
/// @param sep Separator between bytes
/// @param offset Offset into the vector
/// @param maxBytes Maximum number of bytes to include (-1 for all)
/// @return Hex string
String Raft::getHexStr(const std::vector<uint8_t>& inVec, const char* pSep, uint32_t offset, int maxBytes)
{
    String outStr;
    hexDump(inVec.data(), inVec.size(), outStr, pSep, offset, maxBytes);
    return outStr;
}

/// @brief Get a hex string from a SpiramAwareUint8Vector
/// @param inVec Input vector
/// @param offset Offset into the vector
/// @param sep Separator between bytes
/// @param offset Offset into the vector
/// @param maxBytes Maximum number of bytes to include (-1 for all)
/// @return Hex string
String Raft::getHexStr(const SpiramAwareUint8Vector& inVec, const char* pSep, uint32_t offset, int maxBytes)
{
    String outStr;
    hexDump(inVec.data(), inVec.size(), outStr, pSep, offset,maxBytes);
    return outStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get a zero padded hex string from uint32_t value
/// @param val Value to convert
/// @param prefixOx Include 0x prefix
/// @return Hex string
String Raft::getHexStr(uint32_t val, bool prefixOx)
{
    char tmpStr[20];
    snprintf(tmpStr, sizeof(tmpStr), "%s%08lx", prefixOx ? "0x" : "", (unsigned long) val);
    return String(tmpStr);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get a zero padded hex string from uint16_t value
/// @param val Value to convert
/// @param prefixOx Include 0x prefix
/// @return Hex string
String Raft::getHexStr(uint16_t val, bool prefixOx)
{
    char tmpStr[20];
    snprintf(tmpStr, sizeof(tmpStr), "%s%04x", prefixOx ? "0x" : "", val);
    return String(tmpStr);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get a zero padded hex string from uint8_t value
/// @param val Value to convert
/// @param prefixOx Include 0x prefix
/// @return Hex string
String Raft::getHexStr(uint8_t val, bool prefixOx)
{
    char tmpStr[20];
    snprintf(tmpStr, sizeof(tmpStr), "%s%02x", prefixOx ? "0x" : "", val);
    return String(tmpStr);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Convert a byte array to a hex string
/// @param pBuf Pointer to the byte array
/// @param bufLen Length of the byte array
/// @param outStr String to receive the hex string
/// @param separator Separator between bytes
/// @param offset Offset into the buffer
/// @param maxBytes Maximum number of bytes to include (-1 for all)
void Raft::hexDump(const uint8_t* pBuf, uint32_t bufLen, String& outStr, const char* pSeparator, uint32_t offset, int maxBytes)
{
    // Setup outStr
    outStr = "";

    // Check valid
    if (!pBuf || (bufLen == 0) || !pSeparator || (offset >= bufLen))
        return;

    // Adjust buffer pointer and length
    pBuf += offset;
    bufLen -= offset;

    // Get number of bytes to include
    uint32_t numBytes = maxBytes >= 0 ? maxBytes : bufLen;

    // Size outStr
    int itemLen = 2 + strnlen(pSeparator, 10);
    outStr.reserve(numBytes * itemLen);

    // Generate hex
    for (uint32_t i = 0; i < numBytes; i++)
    {
        char tmpStr[10];
        snprintf(tmpStr, sizeof(tmpStr), "%02x%s", pBuf[i], pSeparator);
        outStr += tmpStr;
    }
}

/// @brief Generate a hex string from uInt32 with no space between hex digits (e.g. 55aa55aa, etc)
/// @param pBuf Pointer to the uint32_t array
/// @param bufLen Length of the array
/// @param outStr String to receive the hex string
/// @param separator Separator between hex digits
void Raft::getHexStrFromUint32(const uint32_t* pBuf, uint32_t bufLen, String& outStr, const char* separator)
{
    // Setup outStr
    outStr = "";
    outStr.reserve(bufLen * (8 + strnlen(separator, 20)));

    // Generate hex
    for (uint32_t i = 0; i < bufLen; i++)
    {
        char tmpStr[20];
        snprintf(tmpStr, sizeof(tmpStr), "%08lx", (unsigned long) pBuf[i]);
        outStr += tmpStr;
        if (i != bufLen-1)
            outStr += separator;
    }
}

/// @brief Get string from part of buffer with optional hex and ascii
/// @param pBuf Pointer to the buffer
/// @param bufLen Length of the buffer
/// @param includeHex Include hex in the output
/// @param includeAscii Include ascii in the output
/// @return String containing the buffer contents
String Raft::getBufStrHexAscii(const void* pBuf, uint32_t bufLen, bool includeHex, bool includeAscii)
{
    String outStr;
    if (includeHex)
    {
        getHexStrFromBytes((const uint8_t*)pBuf, bufLen, outStr);
    }
    if (includeAscii)
    {
        if (outStr.length() > 0)
            outStr += " ";
        String asciiBuf = String((const char*)pBuf, bufLen);
        asciiBuf.replace("\n", "<LF>");
        asciiBuf.replace("\r", "<CR>");
        outStr += asciiBuf;
    }
    return outStr;
}

/// @brief Log out a buffer in hex format
/// @param pBuf Pointer to the buffer
/// @param bufLen Length of the buffer
/// @param logPrefix Prefix for the log message
/// @param logIntro Intro for the log message
void Raft::logHexBuf(const uint8_t* pBuf, uint32_t bufLen, const char* logPrefix, const char* logIntro)
{
    if (!pBuf)
        return;
    // Output log string with prefix and length only if > 16 bytes
    if (bufLen > 16) {
        LOG_I(logPrefix, "%s len %d", logIntro, bufLen);
    }

    // Iterate over buffer
    uint32_t bufPos = 0;
    while (bufPos < bufLen)
    {
        char outBuf[400];
        strcpy(outBuf, "");
        char tmpBuf[10];
        uint32_t linePos = 0;
        while ((linePos < 16) && (bufPos < bufLen))
        {
            snprintf(tmpBuf, sizeof(tmpBuf), "%02x ", pBuf[bufPos]);
            strlcat(outBuf, tmpBuf, sizeof(outBuf));
            bufPos++;
            linePos++;
        }
        if (bufLen <= 16) {
            LOG_I(logPrefix, "%s len %d: %s", logIntro, bufLen, outBuf);
        } else {
            LOG_I(logPrefix, "%s", outBuf);
        }
    }
}

/// @brief Convert IP string to IP address bytes
/// @param inStr Input string
/// @param pIpAddr Pointer to the IP address bytes
/// @return true if the conversion was successful
/// @note This code from Unix sources
unsigned long Raft::convIPStrToAddr(String &inStr)
{
    char buf[30];
    char *cp = buf;

    inStr.toCharArray(cp, 29);
    unsigned long val, base, n;
    char c;
    unsigned long parts[4], *pp = parts;

    for (;;)
    {
        /*
            * Collect number up to ``.''.
            * Values are specified as for C:
            * 0x=hex, 0=octal, other=decimal.
            */
        val = 0;
        base = 10;
        if (*cp == '0')
        {
            if ((*++cp == 'x') || (*cp == 'X'))
            {
                base = 16, cp++;
            }
            else
            {
                base = 8;
            }
        }
        while ((c = *cp) != '\0')
        {
            if (isascii(c) && isdigit(c))
            {
                val = (val * base) + (c - '0');
                cp++;
                continue;
            }
            if ((base == 16) && isascii(c) && isxdigit(c))
            {
                val = (val << 4) +
                        (c + 10 - (islower(c) ? 'a' : 'A'));
                cp++;
                continue;
            }
            break;
        }
        if (*cp == '.')
        {
            /*
                * Internet format:
                *  a.b.c.d
                *  a.b.c (with c treated as 16-bits)
                *  a.b (with b treated as 24 bits)
                */
            if ((pp >= parts + 3) || (val > 0xff))
            {
                return (INADDR_NONE);
            }
            *pp++ = val, cp++;
        }
        else
        {
            break;
        }
    }

    /*
        * Check for trailing characters.
        */
    if (*cp && (!isascii(*cp) || !isspace(*cp)))
    {
        return (INADDR_NONE);
    }

    /*
        * Concoct the address according to
        * the number of parts specified.
        */
    n = pp - parts + 1;
    switch (n)
    {
    case 1: /* a -- 32 bits */
        break;

    case 2: /* a.b -- 8.24 bits */
        if (val > 0xffffff)
        {
            return (INADDR_NONE);
        }
        val |= parts[0] << 24;
        break;

    case 3: /* a.b.c -- 8.8.16 bits */
        if (val > 0xffff)
        {
            return (INADDR_NONE);
        }
        val |= (parts[0] << 24) | (parts[1] << 16);
        break;

    case 4: /* a.b.c.d -- 8.8.8.8 bits */
        if (val > 0xff)
        {
            return (INADDR_NONE);
        }
        val |= (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8);
        break;
    }
    return (val);
}

/// @brief Format MAC address
/// @param pMacAddr Pointer to the MAC address bytes
/// @param separator Separator between MAC address bytes
/// @return Formatted MAC address
String Raft::formatMACAddr(const uint8_t* pMacAddr, const char* separator, bool isReversed)
{
    String outStr;
    const uint32_t MAC_ADDR_LEN = 6;
    outStr.reserve(MAC_ADDR_LEN * 2 + MAC_ADDR_LEN);
    for (uint32_t i = 0; i < MAC_ADDR_LEN; i++)
    {
        char tmpStr[10];
        snprintf(tmpStr, sizeof(tmpStr), "%02x", pMacAddr[isReversed ? MAC_ADDR_LEN-1-i : i]);
        outStr += tmpStr;
        if (i != MAC_ADDR_LEN-1)
            outStr += separator;
    }
    return outStr;
}

/// @brief Find match in buffer (like strstr for unterminated strings)
/// @param pBuf Pointer to the buffer
/// @param bufLen Length of the buffer
/// @param pToFind Pointer to the string to find
/// @param toFindLen Length of the string to find
/// @return Position in buffer of val or -1 if not found
int Raft::findInBuf(const uint8_t* pBuf, uint32_t bufLen, 
            const uint8_t* pToFind, uint32_t toFindLen)
{
    for (uint32_t i = 0; i < bufLen; i++)
    {
        if (pBuf[i] == pToFind[0])
        {
            for (uint32_t j = 0; j < toFindLen; j++)
            {
                uint32_t k = i + j;
                if (k >= bufLen)
                    return -1;
                if (pBuf[k] != pToFind[j])
                    break;
                if (j == toFindLen-1)
                    return i;
            }
        }
    }
    return -1;
}

/// @brief Find match in buffer (like strstr for unterminated strings)
/// @param buf buffer
/// @param offset offset into buffer
/// @param pToFind Pointer to the string to find
/// @param toFindLen Length of the string to find
/// @return Position in buffer of val or -1 if not found
int Raft::findInBuf(const SpiramAwareUint8Vector& buf, uint32_t offset,
            const uint8_t* pToFind, uint32_t toFindLen)
{
    if (offset >= buf.size())
        return -1;
    int rslt = findInBuf(buf.data() + offset, buf.size() - offset, pToFind, toFindLen);
    if (rslt >= 0)
        rslt += offset;
    return rslt;
}

/// @brief Parse a string into a list of integers, handling ranges.
/// @param pInStr Pointer to the input string.
/// @param outList List to receive the integers.
/// @param pSep List separator (default: "," if nullptr).
/// @param pListSep Range separator (default: "-" if nullptr).
/// @param maxNum Maximum number of integers to parse.
/// @return true if all integers were parsed (false if maxNum was reached).
/// @note This handles ranges of integers in the form of "1-5,7,9-12".
bool Raft::parseIntList(const char* pInStr, std::vector<int>& outList,
                      const char* pSep, const char* pListSep, uint32_t maxNum) 
{
    // Clear the list and check inputs valid
    outList.clear();
    if (!pInStr || maxNum == 0)
        return false;

    // Check for nullptrs
    pSep = pSep ? pSep : ",";
    pListSep = pListSep ? pListSep : "-";

    // Parse the string
    const char* ptr = pInStr;
    while (*ptr != '\0')
    {
        // Parse the start of a number or range
        char* endPtr = nullptr;
         // Parse the first number
        int start = strtol(ptr, &endPtr, 0);
        if (ptr == endPtr)
            break;

        // Move to end of number and skip spaces
        ptr = endPtr;
        while (*ptr == ' ')
            ++ptr;

        // Check if this is a range
        if (strncmp(ptr, pListSep, strlen(pListSep)) == 0)
        {
            // Skip the range separator
            ptr += strlen(pListSep);
            // Parse the second number
            int end = strtol(ptr, &endPtr, 0);
            if (ptr != endPtr && start <= end)
            {
                // Add all numbers in the range
                for (int i = start; i <= end; ++i)
                {
                    outList.push_back(i);
                    // Stop parsing if maxNum is reached
                    if (outList.size() >= maxNum)
                        return false;
                }
            }
            // Move past the range
            ptr = endPtr;
        }
        else
        {
            // Single number
            outList.push_back(start);
            // Stop parsing if maxNum is reached
            if (outList.size() >= maxNum)
                return false; 
        }

        // Skip the separator, if present
        if (strncmp(ptr, pSep, strlen(pSep)) == 0)
    {
            ptr += strlen(pSep);
    }

        // Skip spaces
        while (*ptr == ' ')
            ++ptr;

    }

    return true;
}

/// @brief Get string for RaftRetCode
/// @param retc RaftRetCode value
const char* Raft::getRetCodeStr(RaftRetCode retc)
{
    switch(retc)
    {
        case RAFT_OK: return "OK";
        case RAFT_BUSY: return "BUSY";
        case RAFT_POS_MISMATCH: return "POS_MISMATCH";
        case RAFT_NOT_XFERING: return "NOT_XFERING";
        case RAFT_NOT_STREAMING: return "NOT_STREAMING";
        case RAFT_SESSION_NOT_FOUND: return "SESSION_NOT_FOUND";
        case RAFT_CANNOT_START: return "CANNOT_START";
        case RAFT_INVALID_DATA: return "INVALID_DATA";
        case RAFT_INVALID_OBJECT: return "INVALID_OBJECT";
        case RAFT_INVALID_OPERATION: return "INVALID_OPERATION";
        case RAFT_INSUFFICIENT_RESOURCE: return "INSUFFICIENT_RESOURCE";
        case RAFT_OTHER_FAILURE: return "OTHER_FAILURE";
        case RAFT_NOT_IMPLEMENTED: return "NOT_IMPLEMENTED";
        case RAFT_BUS_PENDING: return "BUS_PENDING";
        case RAFT_BUS_HW_TIME_OUT: return "BUS_HW_TIME_OUT";
        case RAFT_BUS_ACK_ERROR: return "BUS_ACK_ERROR";
        case RAFT_BUS_ARB_LOST: return "BUS_ARB_LOST";
        case RAFT_BUS_SW_TIME_OUT: return "BUS_SW_TIME_OUT";
        case RAFT_BUS_INVALID: return "BUS_INVALID";
        case RAFT_BUS_NOT_READY: return "BUS_NOT_READY";
        case RAFT_BUS_INCOMPLETE: return "BUS_INCOMPLETE";
        case RAFT_BUS_BARRED: return "BUS_BARRED";
        case RAFT_BUS_NOT_INIT: return "BUS_NOT_INIT";
        case RAFT_BUS_STUCK: return "BUS_STUCK";
        case RAFT_BUS_SLOT_POWER_UNSTABLE: return "BUS_SLOT_POWER_UNSTABLE";
        case RAFT_FS_BUSY: return "FS_BUSY";
        case RAFT_FS_NOT_SETUP: return "FS_NOT_SETUP";
        case RAFT_FS_FOLDER_NOT_FOUND: return "FS_FOLDER_NOT_FOUND";
        case RAFT_FS_FILE_NOT_FOUND: return "FS_FILE_NOT_FOUND";
        case RAFT_FS_FILE_EXISTS: return "FS_FILE_EXISTS";
        case RAFT_FS_FILE_TOO_BIG: return "FS_FILE_TOO_BIG";
        case RAFT_FS_FILE_WRITE_ERROR: return "FS_FILE_WRITE_ERROR";
        case RAFT_FS_FILE_READ_ERROR: return "FS_FILE_READ_ERROR";
        case RAFT_FS_FILE_OPEN_ERROR: return "FS_FILE_OPEN_ERROR";
        case RAFT_FS_OTHER_ERROR: return "FS_OTHER_ERROR";
        case RAFT_MOTION_NO_MOVEMENT: return "MOTION_NO_MOVEMENT";
        case RAFT_MOTION_BELOW_MIN_DISTANCE: return "MOTION_BELOW_MIN_DISTANCE";
        case RAFT_MOTION_NO_STEPS: return "MOTION_NO_STEPS";
        case RAFT_MOTION_BUSY: return "MOTION_BUSY";
        case RAFT_MOTION_HOMING_REQUIRED: return "MOTION_HOMING_REQUIRED";
        default: return "UNKNOWN";
    }
};

/// @brief Convert UUID128 string to uint8_t array
/// @param uuid128Str UUID128 string
/// @param pUUID128 Pointer to the UUID128 array
/// @param reverseOrder Reverse the order of the UUID128 array
/// @return true if successful
bool Raft::uuid128FromString(const char* uuid128Str, uint8_t* pUUID128, bool reverseOrder)
{
    // Check length (handle version with or without dashes)
    uint32_t slen = strlen(uuid128Str);
    if ((slen != UUID128_BYTE_COUNT*2) && (slen != UUID128_BYTE_COUNT*2+4))
        return false;

    // Convert
    uint32_t byteIdx = 0;
    for (uint32_t i = 0; i < slen; i++)
    {
        // Skip dashes (actually any non-alphanumeric in case any other punctuation used)
        if (!isalnum(uuid128Str[i]))
            continue;
        // This should catch badly formatted strings at least to avoid buffer overruns
        if (i + 1 >= slen)
            return false;

        // Form hex string
        char tmpBuf[3];
        tmpBuf[0] = uuid128Str[i];
        tmpBuf[1] = uuid128Str[i+1];
        tmpBuf[2] = 0;
        pUUID128[byteIdx++] = strtoul(tmpBuf, nullptr, 16);

        // Inc i beyond hex digits
        i++;
    }

    // Check for reversal
    if (reverseOrder)
    {
        for (uint32_t i = 0; i < UUID128_BYTE_COUNT/2; i++)
        {
            uint8_t tmp = pUUID128[i];
            pUUID128[i] = pUUID128[UUID128_BYTE_COUNT-1-i];
            pUUID128[UUID128_BYTE_COUNT-1-i] = tmp;
        }
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Convert UUID128 uint8_t array to string
/// @param pUUID128 Pointer to the UUID128 array
/// @param reverseOrder Reverse the order of the UUID128 array
/// @return UUID128 string
String Raft::uuid128ToString(const uint8_t* pUUID128, bool reverseOrder)
{
    // Check valid
    if (!pUUID128)
        return "";

    // Convert
    String uuid128Str;
    char tmpBuf[5];
    for (uint32_t i = 0; i < 16; i++)
    {
        uint32_t idx = reverseOrder ? 16-1-i : i;
        snprintf(tmpBuf, sizeof(tmpBuf), "%02x%s", pUUID128[idx], 
                    (i == 3 || i == 5 || i == 7 || i == 9) ? "-" : "");
        uuid128Str += tmpBuf;
    }
    return uuid128Str;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Trim a String including removing trailing null terminators
/// @param str String to trim
void Raft::trimString(String& str)
{
    str.trim();
    while (str.length() > 0 && str[str.length()-1] == 0)
        str.remove(str.length()-1);
}

/// @brief Format a string with a variable number of arguments
/// @param maxLen Maximum length of the formatted string
/// @param fmt Format string
/// @param ... Variable arguments
/// @return Formatted string
String Raft::formatString(uint32_t maxLen, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char buf[maxLen];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return String(buf);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Generate the next pseudo-random number using the Park-Miller algorithm
/// This function implements the Park-Miller minimal standard random number generator algorithm
/// It generates a new pseudo-random number based on the given seed value
/// @param seed current seed value used to generate the next random number
/// @return next pseudo-random number in the sequence
uint32_t Raft::parkMillerNext(uint32_t seed)
{
    uint32_t hi = 16807 * (seed & 0xffff);
    uint32_t lo = 16807 * (seed >> 16);
    lo += (hi & 0x7fff) << 16;
    lo += hi >> 15;
    if (lo > 0x7fffffff)
        lo -= 0x7fffffff;
    return lo;
}