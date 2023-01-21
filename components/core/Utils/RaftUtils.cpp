/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftUtils
//
// Rob Dobson 2012-2022
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <Logger.h>
#include <RaftUtils.h>
#include <limits.h>

#ifndef INADDR_NONE
#define INADDR_NONE         ((uint32_t)0xffffffffUL)
#endif

// Debug
// #define DEBUG_EXTRACT_NAME_VALUES
#ifdef DEBUG_EXTRACT_NAME_VALUES
static const char* MODULE_PREFIX = "Utils";
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Timeouts
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Raft::isTimeout(uint64_t curTime, uint64_t lastTime, uint64_t maxDuration)
{
    if (curTime >= lastTime)
    {
        return (curTime > lastTime + maxDuration);
    }
    return (ULONG_LONG_MAX - (lastTime - curTime) > maxDuration);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Time before time-out occurs - handling counter wrapping
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Elapsed time handling counter wrapping
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint64_t Raft::timeElapsed(uint64_t curTime, uint64_t lastTime)
{
    if (curTime >= lastTime)
        return curTime - lastTime;
    return (ULONG_LONG_MAX - lastTime) + 1 + curTime;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set results for JSON communications
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Raft::setJsonBoolResult(const char* pReq, String& resp, bool rslt, const char* otherJson)
{
    String additionalJson = "";
    if ((otherJson) && (otherJson[0] != '\0'))
        additionalJson = otherJson + String(",");
    String retStr;
    resp = "{\"req\":\"" + String(pReq) + "\"," + additionalJson + "\"rslt\":";
    if (rslt)
        resp += "\"ok\"}";
    else
        resp += "\"fail\"}";
}

// Set a result error
void Raft::setJsonErrorResult(const char* pReq, String& resp, const char* errorMsg, const char* otherJson)
{
    String additionalJson = "";
    if ((otherJson) && (otherJson[0] != 0))
        additionalJson = otherJson + String(",");
    String retStr;
    String errorMsgStr;
    if (errorMsg)
        errorMsgStr = errorMsg;
    resp = "{\"req\":\"" + String(pReq) + "\"," + additionalJson + "\"rslt\":\"fail\",\"error\":\"" + errorMsgStr + "\"}";
}

// Set result
void Raft::setJsonResult(const char* pReq, String& resp, bool rslt, const char* errorMsg, const char* otherJson)
{
    if (rslt)
        setJsonBoolResult(pReq, resp, rslt, otherJson);
    else
        setJsonErrorResult(pReq, resp, errorMsg, otherJson);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Escape JSON string
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String Raft::escapeJSON(const String& inStr)
{
    String outStr;
    // Reserve a bit more than the inStr length
    outStr.reserve((inStr.length() * 3) / 2);
    // Replace chars with escapes as needed
    for (unsigned int i = 0; i < inStr.length(); i++) 
    {
        int c = inStr.charAt(i);
        if (c == '"' || c == '\\' || ('\x00' <= c && c <= '\x1f')) 
        {
            outStr += "\\u";
            String cx = String(c, 16);
            for (unsigned int j = 0; j < 4-cx.length(); j++)
                outStr += "0";
            outStr += cx;
        } 
        else
        {
            outStr += (char)c;
        }
    }
    return outStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Convert HTTP query format to JSON
// JSON only contains name/value pairs and not {}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String Raft::getJSONFromHTTPQueryStr(const char* inStr, bool mustStartWithQuestionMark)
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
    return outStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get Nth field from delimited string
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get a string from a fixed-length buffer
// false if the string was truncated
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Raft::strFromBuffer(const uint8_t* pBuf, uint32_t bufLen, String& outStr, bool asciiOnly)
{
    // Handling on stack or heap?
    static const uint32_t STR_FROM_BUFFER_STACK_MAXLEN = 250;
    static const uint32_t STR_FROM_BUFFER_MAXLEN = 5000;
    uint32_t lenToCopy = (bufLen < STR_FROM_BUFFER_MAXLEN ? bufLen : STR_FROM_BUFFER_MAXLEN);

    // Check if we can use stack
    if (lenToCopy <= STR_FROM_BUFFER_STACK_MAXLEN)
    {
        char tmpStr[lenToCopy+1];
        char* pOut = tmpStr;
        for (uint32_t i = 0; i < lenToCopy; i++)
        {
            if ((pBuf[i] == 0) || (asciiOnly && (pBuf[i] > 127)))
                break;
            *pOut++ = pBuf[i];
        }
        *pOut = 0;
        outStr = tmpStr;
        return lenToCopy == bufLen;
    }

    // Use heap
    char* tmpStr = new char[lenToCopy+1];
    if (!tmpStr)
    {
        outStr = "";
        return false;
    }
    char* pOut = tmpStr;
    for (uint32_t i = 0; i < lenToCopy; i++)
    {
        if ((pBuf[i] == 0) || (asciiOnly && (pBuf[i] > 127)))
            break;
        *pOut++ = pBuf[i];
    }
    *pOut = 0;
    outStr = tmpStr;
    delete [] tmpStr;
    return lenToCopy == bufLen;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get bytes from hex-encoded string
// Assumes no space between hex digits (e.g. 55aa55aa, etc)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t Raft::getBytesFromHexStr(const char* inStr, uint8_t* outBuf, size_t maxOutBufLen)
{
    // Mapping ASCII to hex
    static const uint8_t chToNyb[] =
    {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, // 01234567
        0x08, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 89:;<=>?
        0x00, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x00, // @ABCDEFG
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // HIJKLMNO
    };

    // Clear initially
    uint32_t inStrLen = strnlen(inStr, maxOutBufLen+1);
    uint32_t numBytes = maxOutBufLen < inStrLen / 2 ? maxOutBufLen : inStrLen / 2;
    uint32_t posIdx = 0;
    for (uint32_t byteIdx = 0; byteIdx < numBytes; byteIdx++)
    {
        uint32_t nyb0Idx = (inStr[posIdx++] & 0x1F) ^ 0x10;
        uint32_t nyb1Idx = (inStr[posIdx++] & 0x1F) ^ 0x10;
        outBuf[byteIdx] = (chToNyb[nyb0Idx] << 4) + chToNyb[nyb1Idx];
    };
    return numBytes;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generate a hex string from bytes
// Generates no space between hex digits (e.g. 55aa55aa, etc)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Raft::getHexStrFromBytes(const uint8_t* pBuf, uint32_t bufLen, String& outStr)
{
    // Setup outStr
    outStr = "";

    // Check valid
    if (!pBuf)
        return;

    // Size outStr
    outStr.reserve(bufLen * 2);

    // Generate hex
    for (uint32_t i = 0; i < bufLen; i++)
    {
        char tmpStr[10];
        snprintf(tmpStr, sizeof(tmpStr), "%02x", pBuf[i]);
        outStr += tmpStr;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generate a hex string from bytes
// Generates no space between hex digits (e.g. 55aa55aa, etc)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Convert IP string to IP address bytes
// This code from Unix sources
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

// Format MAC address
String Raft::formatMACAddr(const uint8_t* pMacAddr, const char* separator)
{
    String outStr;
    outStr.reserve(18);
    for (uint8_t i = 0; i < 6; i++)
    {
        char tmpStr[10];
        snprintf(tmpStr, sizeof(tmpStr), "%02x", pMacAddr[i]);
        outStr += tmpStr;
        if (i != 5)
            outStr += separator;
    }
    return outStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Buffer utilities
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Get a uint8_t value from the uint8_t pointer passed in
// Increment the pointer (by 1)
// Also checks endStop pointer value if provided
uint16_t Raft::getUint8AndInc(const uint8_t*& pVal, const uint8_t* pEndStop)
{
    if (!pVal || (pEndStop && (pVal >= pEndStop)))
        return 0;
    uint8_t val = *pVal;
    pVal += 1;
    return val;
}

// Get an int8_t value from the uint8_t pointer passed in
// Increment the pointer (by 1)
// Also checks endStop pointer value if provided
int16_t Raft::getInt8AndInc(const uint8_t*& pVal, const uint8_t* pEndStop)
{
    if (!pVal || (pEndStop && (pVal >= pEndStop)))
        return 0;
    int8_t val = *((int8_t*)pVal);
    pVal += 1;
    return val;
}

// Get a uint16_t little endian value from the uint8_t pointer passed in
// Increment the pointer (by 2)
// Also checks endStop pointer value if provided
uint16_t Raft::getLEUint16AndInc(const uint8_t*& pVal, const uint8_t* pEndStop)
{
    if (!pVal || (pEndStop && (pVal + 1 >= pEndStop)))
        return 0;
    uint16_t val = ((((uint16_t)(*(pVal+1))) * 256) + *pVal);
    pVal += 2;
    return val;
}

// Get a int16_t little endian value from the uint8_t pointer passed in
// Increment the pointer (by 2)
// Also checks endStop pointer value if provided
int16_t Raft::getLEInt16AndInc(const uint8_t*& pVal, const uint8_t* pEndStop)
{
    if (!pVal || (pEndStop && (pVal + 1 >= pEndStop)))
        return 0;
    uint32_t val = ((((uint32_t)(*(pVal+1))) * 256) + *pVal);
    pVal += 2;
    if (val <= 32767)
        return val;
    return val-65536;
}

// Get a uint16_t big endian value from the uint8_t pointer passed in
// Increment the pointer (by 2)
// Also checks endStop pointer value if provided
uint16_t Raft::getBEUint16AndInc(const uint8_t*& pVal, const uint8_t* pEndStop)
{
    if (!pVal || (pEndStop && (pVal + 1 >= pEndStop)))
        return 0;
    uint16_t val = ((((uint16_t)(*(pVal))) * 256) + *(pVal+1));
    pVal += 2;
    return val;
}

// Get a int16_t big endian value from the uint8_t pointer passed in
// Increment the pointer (by 2)
// Also checks endStop pointer value if provided
int16_t Raft::getBEInt16AndInc(const uint8_t*& pVal, const uint8_t* pEndStop)
{
    if (!pVal || (pEndStop && (pVal + 1 >= pEndStop)))
        return 0;
    uint32_t val = ((((uint32_t)(*(pVal))) * 256) + *(pVal+1));
    pVal += 2;
    if (val <= 32767)
        return val;
    return val-65536;
}

// Get a uint32_t big endian value from the uint8_t pointer passed in
// Increment the pointer (by 4)
// Also checks endStop pointer value if provided
uint32_t Raft::getBEUint32AndInc(const uint8_t*& pVal, const uint8_t* pEndStop)
{
    if (!pVal || (pEndStop && (pVal + 3 >= pEndStop)))
        return 0;
    uint32_t val = ((((uint32_t)(*pVal)) << 24) + (((uint32_t)(*(pVal+1))) << 16) +
                    + (((uint32_t)(*(pVal+2))) << 8) + *(pVal+3));
    pVal += 4;
    return val;
}

// Get a float32_t big endian value from the uint8_t pointer passed in
// Increment the pointer (by 4)
// Also checks endStop pointer value if provided
float Raft::getBEfloat32AndInc(const uint8_t*& pVal, const uint8_t* pEndStop)
{
    if (!pVal || (pEndStop && (pVal + 3 >= pEndStop)))
        return 0;
    
    float val = 0;
    uint8_t* pFloat = (uint8_t*)(&val)+3;
    for (int i = 0; i < 4; i++)
        *pFloat-- = *pVal++;
    return val;
}

// Set values into a buffer
void Raft::setBEInt8(uint8_t* pBuf, uint32_t offset, int8_t val)
{
    if (!pBuf)
        return;
    *(pBuf+offset) = val;
}

// Set values into a buffer
void Raft::setBEUint8(uint8_t* pBuf, uint32_t offset, uint8_t val)
{
    if (!pBuf)
        return;
    *(pBuf+offset) = val;
}

// Set values into a buffer
void Raft::setBEInt16(uint8_t* pBuf, uint32_t offset, int16_t val)
{
    if (!pBuf)
        return;
    *(pBuf+offset) = (val >> 8) & 0x0ff;
    *(pBuf+offset+1) = val & 0xff;
}

// Set values into a buffer
void Raft::setBEUint16(uint8_t* pBuf, uint32_t offset, uint16_t val)
{
    if (!pBuf)
        return;
    *(pBuf+offset) = (val >> 8) & 0x0ff;
    *(pBuf+offset+1) = val & 0xff;
}

// Set values into a buffer
void Raft::setBEUint32(uint8_t* pBuf, uint32_t offset, uint32_t val)
{
    if (!pBuf)
        return;
    *(pBuf+offset) = (val >> 24) & 0x0ff;
    *(pBuf+offset+1) = (val >> 16) & 0x0ff;
    *(pBuf+offset+2) = (val >> 8) & 0x0ff;
    *(pBuf+offset+3) = val & 0xff;
}

// Set values into a buffer
// Since ESP32 is little-endian this means reversing the order
void Raft::setBEFloat32(uint8_t* pBuf, uint32_t offset, float val)
{
    if (!pBuf)
        return;
    uint8_t* pFloat = (uint8_t*)(&val)+3;
    uint8_t* pBufOff = pBuf + offset;
    for (int i = 0; i < 4; i++)
        *pBufOff++ = *pFloat--;
}

// Get a string from a fixed-length buffer
// false if the string was truncated
bool Raft::strFromBuffer(const uint8_t* pBuf, uint32_t bufLen, String& outStr, bool asciiOnly);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clamp
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Clamp - like std::clamp but for uint32_t
uint32_t Raft::clamp(uint32_t val, uint32_t lo, uint32_t hi)
{
    if (val < lo)
        val = lo;
    if (val > hi)
        val = hi;
    return val;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Hex string handling
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Get RGB from hex string
Raft::RGBValue Raft::getRGBFromHex(const String& colourStr)
{
    uint32_t colourRGB = strtoul(colourStr.c_str(), nullptr, 16);
    return RGBValue((colourRGB >> 16) & 0xff, (colourRGB >> 8) & 0xff, colourRGB & 0xff);
}

// Get decimal value of hex char
uint32_t Raft::getHexFromChar(int ch)
{
    ch = toupper(ch);
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    else if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return 0;
}

// Extract bytes from hex encoded string
uint32_t Raft::getBytesFromHexStr(const char* inStr, uint8_t* outBuf, size_t maxOutBufLen);

// Generate a hex string from bytes
void Raft::getHexStrFromBytes(const uint8_t* pBuf, uint32_t bufLen, String& outStr);

// Generate a hex string from uint32_t
void Raft::getHexStrFromUint32(const uint32_t* pBuf, uint32_t bufLen, String& outStr, 
            const char* separator);

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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Find 
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Find match in buffer (like strstr for unterminated strings)
// Returns position in buffer of val or -1 if not found
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

