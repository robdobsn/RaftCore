/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DeviceTypeGrammar.cpp
//
// See DeviceTypeGrammar.h. Where a string is accepted, the steps produced are what the previous parser
// (DeviceTypeRecords::getDetectionRecs/getInitBusRequests/getPollInfo before Stage 4 of the device
// identification plan) produced, byte for byte - linux_unit_tests/test_device_type_grammar checks this against
// a copy of that parser for every string in the known catalogues
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DeviceTypeGrammar.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
    struct Ctx
    {
        DeviceTypeGrammar::Result& result;
        uint32_t stepPos;
        void issue(DeviceTypeGrammar::IssueCode code, uint32_t posInStep)
        {
            result.issues.push_back({code, (uint16_t)(stepPos + posInStep)});
        }
    };

    std::string toLower(const std::string& s)
    {
        std::string out = s;
        for (char& c : out)
            c = (char)tolower((unsigned char)c);
        return out;
    }

    bool isHexDigit(char c)
    {
        return isxdigit((unsigned char)c) != 0;
    }

    uint8_t hexNibble(char c)
    {
        if ((c >= '0') && (c <= '9'))
            return c - '0';
        c = (char)tolower((unsigned char)c);
        return (c >= 'a') && (c <= 'f') ? c - 'a' + 10 : 0;
    }

    // Number after the first 'p' in a string (0 if there is none) - how the previous parser found a pause
    long firstPauseMs(const std::string& lc)
    {
        size_t pos = lc.find('p');
        if (pos == std::string::npos)
            return 0;
        long ms = strtol(lc.c_str() + pos + 1, nullptr, 10);
        return ms < 0 ? 0 : ms;
    }

    // Set a step's pause; false (with an issue) if it is longer than a pause can be
    bool setPause(DeviceTypeGrammar::Step& step, long ms, Ctx& ctx, uint32_t posInStep)
    {
        if (ms > 65535)
        {
            ctx.issue(DeviceTypeGrammar::IssueCode::BadPause, posInStep);
            return false;
        }
        step.pauseMs = (uint16_t)ms;
        return true;
    }

    // Check that a string is "pNN" (case insensitive)
    bool isPauseToken(const std::string& s)
    {
        if ((s.length() < 2) || ((s[0] != 'p') && (s[0] != 'P')))
            return false;
        for (size_t i = 1; i < s.length(); i++)
            if (!isdigit((unsigned char)s[i]))
                return false;
        return true;
    }

    // Hex digits to bytes, as the previous parser: (n+1)/2 bytes, an odd last digit is lost (its byte is 0)
    void hexToBytes(const std::string& digits, std::vector<uint8_t>& out)
    {
        out.assign((digits.length() + 1) / 2, 0);
        for (size_t i = 0; i + 1 < digits.length(); i += 2)
            out[i / 2] = (hexNibble(digits[i]) << 4) | hexNibble(digits[i + 1]);
    }

    // Append hex digits as bytes (as hexToBytes: an odd last digit is lost, its byte is 0)
    void appendHexBytes(const std::string& digits, std::vector<uint8_t>& out)
    {
        std::vector<uint8_t> bytes;
        hexToBytes(digits, bytes);
        out.insert(out.end(), bytes.begin(), bytes.end());
    }

    // A {name} placeholder's name: a letter or _, then letters, digits or _
    bool isSlotName(const std::string& name)
    {
        if (name.empty() || !(isalpha((unsigned char)name[0]) || (name[0] == '_')))
            return false;
        for (char c : name)
            if (!(isalnum((unsigned char)c) || (c == '_')))
                return false;
        return true;
    }

    // Write bytes from the name side; returns false (with an issue) if they aren't hex. In an init string the hex may contain {name} placeholders: each is one byte, written with
    // the value the record's initCode computes (recorded in slots)
    bool parseWrite(const std::string& name, std::vector<uint8_t>& write, std::vector<DeviceTypeGrammar::Step::ValueSlot>& slots,
                DeviceTypeGrammar::Field field, Ctx& ctx)
    {
        size_t start = 0;
        if ((name.length() >= 2) && (name[0] == '0') && ((name[1] == 'x') || (name[1] == 'X')))
            start = 2;
        std::string digits = name.substr(start);
        if ((start == 2) && digits.empty())
        {
            // "0x" with no digits (the previous parser rejected it)
            ctx.issue(DeviceTypeGrammar::IssueCode::NotHex, 2);
            return false;
        }
        std::string hexRun;
        for (size_t i = 0; i < digits.length(); i++)
        {
            if (digits[i] == '{')
            {
                // A placeholder: only in init strings, a name, and at a byte boundary
                size_t end = digits.find('}', i + 1);
                std::string slotName = (end == std::string::npos) ? "" : digits.substr(i + 1, end - i - 1);
                if ((field != DeviceTypeGrammar::Field::Init) || (end == std::string::npos) || !isSlotName(slotName) ||
                            (hexRun.length() % 2 != 0))
                {
                    ctx.issue(DeviceTypeGrammar::IssueCode::BadPlaceholder, start + i);
                    return false;
                }
                appendHexBytes(hexRun, write);
                hexRun.clear();
                DeviceTypeGrammar::Step::ValueSlot slot;
                slot.offset = (uint16_t)write.size();
                slot.name = slotName;
                slots.push_back(slot);
                write.push_back(0);
                i = end;
                continue;
            }
            if (!isHexDigit(digits[i]))
            {
                ctx.issue(DeviceTypeGrammar::IssueCode::NotHex, start + i);
                return false;
            }
            hexRun += digits[i];
        }
        if (hexRun.length() % 2 != 0)
            ctx.issue(DeviceTypeGrammar::IssueCode::OddHex, 0);
        appendHexBytes(hexRun, write);
        return true;
    }

    // A check or read-length value as the previous parser's extractMaskAndDataFromHexStr: "rN" (anywhere),
    // an optional leading pause then 0x... or 0b..., or a pause alone. compareAll: detection (every hex bit
    // compared, rN compares nothing); otherwise only the length is used
    enum class ValueKind
    {
        Read,       // rN
        Hex,
        Binary,
        PauseOnly,
        Invalid,
    };
    ValueKind parseCheckValue(const std::string& lc, DeviceTypeGrammar::Check& check, uint32_t& readLen, Ctx& ctx,
                uint32_t posInStep)
    {
        check.mask.clear();
        check.value.clear();
        readLen = 0;

        // rNNN (the previous parser found the 'r' anywhere)
        size_t rPos = lc.find('r');
        if (rPos != std::string::npos)
        {
            long len = strtol(lc.c_str() + rPos + 1, nullptr, 10);
            if ((len <= 0) || (len > 65535))
            {
                ctx.issue(DeviceTypeGrammar::IssueCode::BadReadLength, posInStep + rPos);
                return ValueKind::Invalid;
            }
            readLen = (uint32_t)len;
            check.mask.assign(readLen, 0);
            check.value.assign(readLen, 0);
            return ValueKind::Read;
        }

        // Optional leading whitespace and pause
        size_t idx = 0;
        while ((idx < lc.length()) && isspace((unsigned char)lc[idx]))
            idx++;
        if ((idx < lc.length()) && (lc[idx] == 'p'))
        {
            idx++;
            while ((idx < lc.length()) && isdigit((unsigned char)lc[idx]))
                idx++;
            while ((idx < lc.length()) && isspace((unsigned char)lc[idx]))
                idx++;
        }

        // Hex
        if (lc.compare(idx, 2, "0x") == 0)
        {
            std::string digits = lc.substr(idx + 2);
            for (size_t i = 0; i < digits.length(); i++)
            {
                if (!isHexDigit(digits[i]))
                {
                    ctx.issue(DeviceTypeGrammar::IssueCode::BadCheck, posInStep + idx + 2 + i);
                    return ValueKind::Invalid;
                }
            }
            if (digits.length() % 2 != 0)
                ctx.issue(DeviceTypeGrammar::IssueCode::OddHex, posInStep + idx);
            hexToBytes(digits, check.value);
            check.mask.assign(check.value.size(), 0xff);
            readLen = check.value.size();
            return ValueKind::Hex;
        }

        // Binary
        if (lc.compare(idx, 2, "0b") == 0)
        {
            std::string bits = lc.substr(idx + 2);
            for (size_t i = 0; i < bits.length(); i++)
            {
                if ((bits[i] != '0') && (bits[i] != '1') && (bits[i] != 'x'))
                {
                    ctx.issue(DeviceTypeGrammar::IssueCode::BadCheck, posInStep + idx + 2 + i);
                    return ValueKind::Invalid;
                }
            }
            if (bits.length() % 8 != 0)
                ctx.issue(DeviceTypeGrammar::IssueCode::BinaryNotWholeBytes, posInStep + idx);
            uint32_t numBytes = (bits.length() + 7) / 8;
            check.mask.assign(numBytes, 0xff);
            check.value.assign(numBytes, 0);
            for (size_t i = 0; i < bits.length(); i++)
            {
                uint8_t bitMask = 0x80 >> (i % 8);
                if (bits[i] == 'x')
                    check.mask[i / 8] &= ~bitMask;
                else if (bits[i] == '1')
                    check.value[i / 8] |= bitMask;
            }
            readLen = numBytes;
            return ValueKind::Binary;
        }

        // Otherwise only a pause (and whitespace) is valid
        for (size_t i = 0; i < lc.length(); i++)
        {
            if (lc[i] == 'p')
            {
                while ((i + 1 < lc.length()) && isdigit((unsigned char)lc[i + 1]))
                    i++;
                continue;
            }
            if (!isspace((unsigned char)lc[i]))
            {
                ctx.issue(DeviceTypeGrammar::IssueCode::BadCheck, posInStep + i);
                return ValueKind::Invalid;
            }
        }
        return ValueKind::PauseOnly;
    }

    // Detection value: alternatives (and optional :crc8w). Returns false if the record can't be used
    bool parseDetectionValue(const std::string& value, uint32_t valuePos, DeviceTypeGrammar::Step& step, Ctx& ctx)
    {
        std::string lc = toLower(value);

        // CRC suffix
        static const char* CRC_SUFFIX = ":crc8w";
        size_t suffixLen = strlen(CRC_SUFFIX);
        if ((lc.length() >= suffixLen) && (lc.compare(lc.length() - suffixLen, suffixLen, CRC_SUFFIX) == 0))
        {
            step.crc8w = true;
            lc = lc.substr(0, lc.length() - suffixLen);
        }

        if (!setPause(step, firstPauseMs(lc), ctx, valuePos))
            return false;
        size_t start = 0;
        while (start < lc.length())
        {
            size_t comma = lc.find(',', start);
            if (comma == std::string::npos)
                comma = lc.length();
            DeviceTypeGrammar::Check check;
            uint32_t readLen = 0;
            ValueKind kind = parseCheckValue(lc.substr(start, comma - start), check, readLen, ctx, valuePos + start);
            if (kind == ValueKind::Invalid)
                return false;
            if ((step.checks.size() > 0) && (check.value.size() != step.checks[0].value.size()))
                ctx.issue(DeviceTypeGrammar::IssueCode::MixedCheckLengths, valuePos + start);
            step.checks.push_back(check);
            start = comma + 1;
        }
        step.readLen = step.checks.size() > 0 ? step.checks[0].value.size() : 0;
        if (step.crc8w && ((step.readLen == 0) || (step.readLen % 3 != 0)))
        {
            ctx.issue(DeviceTypeGrammar::IssueCode::CrcLength, valuePos);
            return false;
        }
        return true;
    }

    // Init value: as the previous parser, a read if it starts with r (or 0b: a byte count), a pause at the
    // first p, anything else ignored
    bool parseInitValue(const std::string& value, uint32_t valuePos, DeviceTypeGrammar::Step& step, Ctx& ctx)
    {
        std::string lc = toLower(value);
        if (!setPause(step, firstPauseMs(lc), ctx, valuePos))
            return false;
        if (lc.length() == 0)
            return true;
        if (lc[0] == 'r')
        {
            long len = strtol(lc.c_str() + 1, nullptr, 10);
            step.readLen = len < 0 ? 0 : (uint16_t)len;
        }
        else if (lc.compare(0, 2, "0b") == 0)
        {
            step.readLen = (lc.length() - 2 + 7) / 8;
            ctx.issue(DeviceTypeGrammar::IssueCode::InitBinaryIsReadCount, valuePos);
            return true;
        }
        else if (lc.compare(0, 2, "0x") == 0)
        {
            ctx.issue(DeviceTypeGrammar::IssueCode::InitValueNotWritten, valuePos);
            return true;
        }

        // Valid forms: rN, pN, rNpM
        size_t i = 0;
        bool valid = true;
        if ((i < lc.length()) && (lc[i] == 'r'))
        {
            i++;
            size_t digitsStart = i;
            while ((i < lc.length()) && isdigit((unsigned char)lc[i]))
                i++;
            valid = i > digitsStart;
        }
        if (valid && (i < lc.length()))
        {
            valid = isPauseToken(lc.substr(i));
            i = lc.length();
        }
        if (!valid)
            ctx.issue(DeviceTypeGrammar::IssueCode::InitValueIgnored, valuePos);
        return true;
    }

    // Poll value: returns false if the step can't be used (the previous parser skipped it)
    bool parsePollValue(const std::string& value, uint32_t valuePos, DeviceTypeGrammar::Step& step, Ctx& ctx)
    {
        // Dynamic read length r{expr} (case sensitive, as before)
        size_t dynStart = value.find("r{");
        if (dynStart != std::string::npos)
        {
            size_t dynEnd = value.find('}', dynStart + 2);
            if (dynEnd == std::string::npos)
            {
                ctx.issue(DeviceTypeGrammar::IssueCode::BadReadExpr, valuePos + dynStart);
                return false;
            }
            auto pExpr = std::make_shared<PollReadLenExpr>();
            if (!pExpr->parse(String(value.substr(dynStart + 2, dynEnd - dynStart - 2).c_str())) || !pExpr->hasMaxValue())
            {
                ctx.issue(DeviceTypeGrammar::IssueCode::BadReadExpr, valuePos + dynStart);
                return false;
            }
            step.readLenExpr = pExpr;
            step.readLen = pExpr->getMaxValue();
            return setPause(step, firstPauseMs(toLower(value)), ctx, valuePos);
        }

        std::string lc = toLower(value);
        DeviceTypeGrammar::Check check;
        uint32_t readLen = 0;
        // Report a bad value as a poll value problem rather than a detection check problem
        size_t issuesBefore = ctx.result.issues.size();
        ValueKind kind = parseCheckValue(lc, check, readLen, ctx, valuePos);
        if (kind == ValueKind::Invalid)
        {
            for (size_t i = issuesBefore; i < ctx.result.issues.size(); i++)
                if (ctx.result.issues[i].code == DeviceTypeGrammar::IssueCode::BadCheck)
                    ctx.result.issues[i].code = DeviceTypeGrammar::IssueCode::BadValue;
            return false;
        }
        step.readLen = readLen;
        return setPause(step, firstPauseMs(lc), ctx, valuePos);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Compile a command string
DeviceTypeGrammar::Result DeviceTypeGrammar::compile(const char* pStr, Field field)
{
    Result result;
    std::string str = pStr ? pStr : "";
    size_t startPos = 0;
    bool detectionInvalid = false;
    while (startPos < str.length())
    {
        // Step (as the previous parser: separated by & or ;)
        size_t endPos = str.find_first_of("&;", startPos);
        if (endPos == std::string::npos)
            endPos = str.length();
        std::string stepStr = str.substr(startPos, endPos - startPos);
        Ctx ctx{result, (uint32_t)startPos};

        // Name (trimmed) and value (after the first =)
        size_t eqPos = stepStr.find('=');
        std::string name = eqPos == std::string::npos ? stepStr : stepStr.substr(0, eqPos);
        size_t nameStart = name.find_first_not_of(" \t\r\n");
        size_t nameEnd = name.find_last_not_of(" \t\r\n");
        name = (nameStart == std::string::npos) ? "" : name.substr(nameStart, nameEnd - nameStart + 1);
        uint32_t namePos = nameStart == std::string::npos ? 0 : nameStart;

        Step step;
        bool stepValid = true;
        if ((eqPos == std::string::npos) && isPauseToken(name))
        {
            // A bare pause (C4)
            ctx.issue(IssueCode::BarePause, namePos);
            stepValid = setPause(step, strtol(name.c_str() + 1, nullptr, 10), ctx, namePos);
        }
        else
        {
            Ctx nameCtx{result, (uint32_t)(startPos + namePos)};
            stepValid = parseWrite(name, step.write, step.slots, field, nameCtx);
            if (stepValid && (eqPos != std::string::npos))
            {
                step.hasValue = true;
                std::string value = stepStr.substr(eqPos + 1);
                uint32_t valuePos = eqPos + 1;
                if (value.find('=') != std::string::npos)
                {
                    ctx.issue(IssueCode::MultipleEquals, valuePos + value.find('='));
                    stepValid = false;
                }
                else if (field == Field::Detection)
                {
                    stepValid = parseDetectionValue(value, valuePos, step, ctx);
                }
                else if (field == Field::Init)
                {
                    if (value.find("r{") != std::string::npos)
                    {
                        ctx.issue(IssueCode::ReadExprNotAllowed, valuePos + value.find("r{"));
                        stepValid = false;
                    }
                    else
                    {
                        stepValid = parseInitValue(value, valuePos, step, ctx);
                    }
                }
                else
                {
                    stepValid = parsePollValue(value, valuePos, step, ctx);
                }
            }
        }
        if (stepValid)
            result.steps.push_back(step);
        else
            detectionInvalid = true;

        startPos = endPos + 1;
    }

    // A detection string with an error is not used at all (dropping a check would make it easier to match)
    if ((field == Field::Detection) && detectionInvalid)
        result.steps.clear();
    return result;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Short name of an issue
const char* DeviceTypeGrammar::getIssueName(IssueCode code)
{
    switch (code)
    {
        case IssueCode::NotHex: return "NotHex";
        case IssueCode::MultipleEquals: return "MultipleEquals";
        case IssueCode::BadCheck: return "BadCheck";
        case IssueCode::BadReadExpr: return "BadReadExpr";
        case IssueCode::ReadExprNotAllowed: return "ReadExprNotAllowed";
        case IssueCode::BadReadLength: return "BadReadLength";
        case IssueCode::BadValue: return "BadValue";
        case IssueCode::CrcLength: return "CrcLength";
        case IssueCode::BadPause: return "BadPause";
        case IssueCode::BadPlaceholder: return "BadPlaceholder";
        case IssueCode::OddHex: return "OddHex";
        case IssueCode::BarePause: return "BarePause";
        case IssueCode::InitValueNotWritten: return "InitValueNotWritten";
        case IssueCode::InitBinaryIsReadCount: return "InitBinaryIsReadCount";
        case IssueCode::InitValueIgnored: return "InitValueIgnored";
        case IssueCode::MixedCheckLengths: return "MixedCheckLengths";
        case IssueCode::BinaryNotWholeBytes: return "BinaryNotWholeBytes";
    }
    return "Unknown";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Strength of a compiled detection string (the number of bits it compares)
int32_t DeviceTypeGrammar::detectionStrength(const Result& compiled)
{
    if (compiled.hasErrors())
        return -1;
    int32_t strength = 0;
    for (const Step& step : compiled.steps)
    {
        int32_t weakest = -1;
        for (const Check& check : step.checks)
        {
            int32_t bits = 0;
            for (uint8_t maskByte : check.mask)
                bits += __builtin_popcount(maskByte);
            if ((weakest < 0) || (bits < weakest))
                weakest = bits;
        }
        if (weakest > 0)
            strength += weakest;
        if (step.crc8w)
            strength += 8 * (step.readLen / 3);
    }
    return strength;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Name of a field
const char* DeviceTypeGrammar::getFieldName(Field field)
{
    switch (field)
    {
        case Field::Detection: return "detectionValues";
        case Field::Init: return "initValues";
        case Field::Poll: return "pollInfo.c";
    }
    return "?";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Sensirion CRC-8 (poly 0x31, init 0xff, no reflection, no final XOR)
uint8_t DeviceTypeGrammar::crc8(const uint8_t* pData, uint32_t len)
{
    uint8_t crc = 0xff;
    for (uint32_t i = 0; i < len; i++)
    {
        crc ^= pData[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
    return crc;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check the CRC-8 of each 2-byte word in [word][crc]... data
bool DeviceTypeGrammar::checkCrc8Words(const uint8_t* pData, uint32_t len)
{
    if ((len == 0) || (len % 3 != 0))
        return false;
    for (uint32_t i = 0; i < len; i += 3)
        if (crc8(pData + i, 2) != pData[i + 2])
            return false;
    return true;
}
