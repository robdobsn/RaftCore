/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DeviceActionEncoder
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DeviceActionEncoder.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "RaftJson.h"
#include "RaftUtils.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Append the bytes of a hex string (whole bytes only, optional 0x)
static bool appendHex(const String& hexStr, std::vector<uint8_t>& out)
{
    const char* p = hexStr.c_str();
    if ((p[0] == '0') && ((p[1] == 'x') || (p[1] == 'X')))
        p += 2;
    size_t len = strlen(p);
    if (len % 2 != 0)
        return false;
    for (size_t i = 0; i < len; i += 2)
    {
        if (!isxdigit((unsigned char)p[i]) || !isxdigit((unsigned char)p[i + 1]))
            return false;
        char byteStr[3] = { p[i], p[i + 1], 0 };
        out.push_back((uint8_t)strtoul(byteStr, nullptr, 16));
    }
    return true;
}

// Parse a number (the whole string)
static bool parseNumber(const String& str, double& value)
{
    const char* p = str.c_str();
    while (isspace((unsigned char)*p))
        p++;
    if (*p == 0)
        return false;
    char* pEnd = nullptr;
    value = strtod(p, &pEnd);
    while (pEnd && isspace((unsigned char)*pEnd))
        pEnd++;
    return pEnd && (*pEnd == 0) && isfinite(value);
}

// Sensirion CRC-8 (polynomial 0x31, initial value 0xFF) of a 2-byte word
static uint8_t sensirionCrc8(uint8_t msb, uint8_t lsb)
{
    uint8_t crc = 0xff;
    uint8_t bytes[2] = { msb, lsb };
    for (uint8_t b : bytes)
    {
        crc ^= b;
        for (int bit = 0; bit < 8; bit++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
    return crc;
}

// Integer value as a JavaScript DataView stores it: truncated toward zero, modulo 2^bits (two's complement)
static uint64_t toIntBits(double v)
{
    if (!isfinite(v))
        return 0;
    double t = trunc(v);
    // Within int64 range use a direct conversion; beyond, reduce modulo 2^64
    if ((t >= -9.2e18) && (t <= 9.2e18))
        return (uint64_t)(int64_t)t;
    double m = fmod(t, 18446744073709551616.0);
    if (m < 0)
        m += 18446744073709551616.0;
    return (uint64_t)m;
}

static void appendInt(std::vector<uint8_t>& out, uint64_t bits, int numBytes, bool littleEndian)
{
    for (int i = 0; i < numBytes; i++)
    {
        int shift = littleEndian ? (8 * i) : (8 * (numBytes - 1 - i));
        out.push_back((uint8_t)((bits >> shift) & 0xff));
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Struct pack
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool DeviceActionEncoder::structPack(const char* format, const std::vector<double>& values, std::vector<uint8_t>& out)
{
    if (!format)
        return false;
    bool littleEndian = false;
    size_t valueIdx = 0;
    const char* p = format;
    while (*p)
    {
        char c = *p;
        if ((c == '<') || (c == '>'))
        {
            littleEndian = (c == '<');
            p++;
            continue;
        }
        if (isspace((unsigned char)c))
        {
            p++;
            continue;
        }
        // Prefix count (e.g. 3H)
        uint32_t repeat = 1;
        if (isdigit((unsigned char)c))
        {
            repeat = (uint32_t)strtoul(p, (char**)&p, 10);
            if ((repeat == 0) || !*p || isspace((unsigned char)*p) || (*p == '<') || (*p == '>') || isdigit((unsigned char)*p))
                return false;
        }
        char code = *p++;
        // [N] suffix (multiplies any prefix count)
        if (*p == '[')
        {
            const char* pClose = strchr(p, ']');
            if (!pClose)
                return false;
            uint32_t bracket = (uint32_t)strtoul(p + 1, nullptr, 10);
            if (bracket == 0)
                return false;
            repeat *= bracket;
            p = pClose + 1;
        }
        for (uint32_t r = 0; r < repeat; r++)
        {
            if (code == 'x')
            {
                out.push_back(0);
                continue;
            }
            int numBytes = 0;
            bool isFloat = false;
            switch (code)
            {
                case 'c': case 'b': case 'B': case '?': numBytes = 1; break;
                case 'h': case 'H': numBytes = 2; break;
                case 'i': case 'l': case 'I': case 'L': numBytes = 4; break;
                case 'q': case 'Q': numBytes = 8; break;
                case 'f': numBytes = 4; isFloat = true; break;
                case 'd': numBytes = 8; isFloat = true; break;
                default: return false;
            }
            if (valueIdx >= values.size())
                return false;
            double v = values[valueIdx++];
            if (isFloat && (numBytes == 4))
            {
                float f = (float)v;
                uint32_t bits = 0;
                memcpy(&bits, &f, sizeof(bits));
                appendInt(out, bits, 4, littleEndian);
            }
            else if (isFloat)
            {
                uint64_t bits = 0;
                memcpy(&bits, &v, sizeof(bits));
                appendInt(out, bits, 8, littleEndian);
            }
            else
            {
                appendInt(out, toIntBits(v), numBytes, littleEndian);
            }
        }
    }
    return true;
}

bool DeviceActionEncoder::isIntegerType(const char* format)
{
    // As raftjs isIntegerActionType: an optional byte order then a single integer code
    if (!format)
        return false;
    const char* p = format;
    if (*p && strchr("<>!=@", *p))
        p++;
    return (p[0] != 0) && (p[1] == 0) && strchr("bBhHiIlLqQ", p[0]);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Encode
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DeviceActionEncoder::Result DeviceActionEncoder::encode(const char* devInfoJson, const char* actionName,
            const std::vector<String>& values, std::vector<std::vector<uint8_t>>& writes)
{
    writes.clear();
    if (!devInfoJson || !actionName)
        return Result::NoActions;
    RaftJson devInfo(devInfoJson, false);
    int numActions = 0;
    if (devInfo.getType("actions", numActions) != RaftJsonIF::RAFT_JSON_ARRAY || numActions == 0)
        return Result::NoActions;
    for (int i = 0; i < numActions; i++)
    {
        String prefix = "actions[" + String(i) + "]";
        if (devInfo.getString((prefix + "/n").c_str(), "") != actionName)
            continue;
        RaftJson actionJson(devInfo.getString(prefix.c_str(), "{}").c_str());
        return encodeAction(actionJson, values, writes);
    }
    return Result::NoSuchAction;
}

DeviceActionEncoder::Result DeviceActionEncoder::encodeAction(const RaftJsonIF& action, const std::vector<String>& values,
            std::vector<std::vector<uint8_t>>& writes)
{
    writes.clear();
    int mapLen = 0;
    bool hasMap = action.getType("map", mapLen) == RaftJsonIF::RAFT_JSON_OBJECT;

    // Map action: the value is a key; its hex is written as is (split on '&')
    if (hasMap && (values.size() == 1))
    {
        String entryPath = "map/" + values[0];
        int entryLen = 0;
        RaftJsonIF::RaftJsonType entryType = action.getType(entryPath.c_str(), entryLen);
        String hexStr;
        if (entryType == RaftJsonIF::RAFT_JSON_STRING)
            hexStr = action.getString(entryPath.c_str(), "");
        else if (entryType == RaftJsonIF::RAFT_JSON_OBJECT)
            hexStr = action.getString((entryPath + "/w").c_str(), "");
        else
            return Result::NoMapEntry;
        int start = 0;
        while (start <= (int)hexStr.length())
        {
            int amp = hexStr.indexOf('&', start);
            if (amp < 0)
                amp = hexStr.length();
            String part = hexStr.substring(start, amp);
            start = amp + 1;
            if (part.length() == 0)
                continue;
            std::vector<uint8_t> write;
            if (!appendHex(part, write))
                return Result::BadHex;
            writes.push_back(write);
        }
        return Result::OK;
    }

    // Numbers
    std::vector<double> nums;
    for (const String& v : values)
    {
        double d = 0;
        if (!parseNumber(v, d))
            return Result::BadValue;
        nums.push_back(d);
    }
    String typeStr = action.getString("t", "");
    std::vector<uint8_t> packed;
    if (typeStr.length() > 0)
    {
        if (nums.size() == 1)
        {
            double v = nums[0];
            int len = 0;
            if (action.getType("sub", len) == RaftJsonIF::RAFT_JSON_NUMBER)
                v -= action.getDouble("sub", 0);
            if (action.getType("mul", len) == RaftJsonIF::RAFT_JSON_NUMBER)
                v *= action.getDouble("mul", 1);
            // Rounded as JavaScript's Math.round (halves up, e.g. -1.5 -> -1), as raftjs does
            if (isIntegerType(typeStr.c_str()))
                v = floor(v + 0.5);
            nums[0] = v;
        }
        if (!structPack(typeStr.c_str(), nums, packed))
            return nums.size() > 0 ? Result::BadType : Result::BadValue;
    }

    // Check bytes
    String crcStr = action.getString("crc", "");
    if (crcStr.length() > 0)
    {
        if ((crcStr != "crc8w") || (packed.size() % 2 != 0))
            return Result::BadCrc;
        std::vector<uint8_t> withCrc;
        for (size_t i = 0; i < packed.size(); i += 2)
        {
            withCrc.push_back(packed[i]);
            withCrc.push_back(packed[i + 1]);
            withCrc.push_back(sensirionCrc8(packed[i], packed[i + 1]));
        }
        packed.swap(withCrc);
    }

    std::vector<uint8_t> write;
    if (!appendHex(action.getString("w", ""), write))
        return Result::BadHex;
    write.insert(write.end(), packed.begin(), packed.end());
    if (!appendHex(action.getString("wz", ""), write))
        return Result::BadHex;
    writes.push_back(write);
    return Result::OK;
}

const char* DeviceActionEncoder::getResultStr(Result result)
{
    switch (result)
    {
        case Result::OK: return "ok";
        case Result::NoActions: return "failNoActions";
        case Result::NoSuchAction: return "failNoSuchAction";
        case Result::NoMapEntry: return "failNoMapEntry";
        case Result::BadValue: return "failBadValue";
        case Result::BadType: return "failBadType";
        case Result::BadHex: return "failBadHex";
        case Result::BadCrc: return "failBadCrc";
    }
    return "fail";
}
