/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DeviceTypeRuntimeDecoder.cpp
//
// See DeviceTypeRuntimeDecoder.h. Each step mirrors the C++ that DecodeGenerator.py (gen_attr_extraction_code)
// generates for a standard decode, working in the same C++ types, so a runtime-decoded record gives exactly the
// values its compiled decoder would (linux_unit_tests/test_runtime_decoder checks every RaftCore record):
//   - the value is read with the buffer type's reader, unsigned for a signed type with a mask "m";
//   - it is held in the generator's intermediate type (a signed type one size larger, or the float type);
//   - "x" XOR, "m" mask (sign-extending for a signed type), "sb"/"ss" sign handling and "s" shift are applied in it,
//     with constants of the C literal types the generator writes;
//   - it is stored to the struct field ("o", else the buffer type), then divided by "d" and offset by "a" in the
//     field's type - with an integer or a floating literal as the record writes it; or, with a lookup table "lut",
//     divided, offset and looked up in a double and the result stored.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DeviceTypeRuntimeDecoder.h"

#ifdef RAFT_RUNTIME_DECODE

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <ctype.h>
#include <cmath>
#include <string.h>
#include <stdlib.h>
#include <utility>
#include "RaftJson.h"
#include "RaftUtils.h"
#include "DevicePollingInfo.h"

// Timestamp in the decoded struct (DecodeGenerator.py defaults: uint32_t timeMs, 1000 us resolution)
static const uint32_t DECODE_STRUCT_TIMESTAMP_RESOLUTION_US = 1000;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Decoder slots: each slot has its own function, so a plain function pointer reaches its decoder
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
    std::atomic<const DeviceTypeRuntimeDecoder*> g_decoderSlots[RAFT_RUNTIME_DECODE_MAX_DECODERS];

    template <size_t SLOT>
    uint32_t slotDecode(const uint8_t* pBufIn, uint32_t bufLen, void* pStructOut, uint32_t structOutSize,
                uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState)
    {
        const DeviceTypeRuntimeDecoder* pDecoder = g_decoderSlots[SLOT].load(std::memory_order_acquire);
        return pDecoder ? pDecoder->decode(pBufIn, bufLen, pStructOut, structOutSize, maxRecCount, decodeState) : 0;
    }

    template <size_t... SLOTS>
    constexpr std::array<DeviceTypeRecordDecodeFn, sizeof...(SLOTS)> makeSlotFns(std::index_sequence<SLOTS...>)
    {
        return {{&slotDecode<SLOTS>...}};
    }
    const std::array<DeviceTypeRecordDecodeFn, RAFT_RUNTIME_DECODE_MAX_DECODERS> g_slotFns =
                makeSlotFns(std::make_index_sequence<RAFT_RUNTIME_DECODE_MAX_DECODERS>{});
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Types
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
    using NumType = DeviceTypeRuntimeDecoder::NumType;
    using Literal = DeviceTypeRuntimeDecoder::Literal;

    uint32_t numTypeSize(NumType t)
    {
        switch (t)
        {
            case NumType::Int8: case NumType::Uint8: return 1;
            case NumType::Int16: case NumType::Uint16: return 2;
            case NumType::Int32: case NumType::Uint32: case NumType::Float: return 4;
            case NumType::Int64: case NumType::Uint64: case NumType::Double: return 8;
            case NumType::Bool: return sizeof(bool);
            default: return 0;
        }
    }

    uint32_t numTypeAlign(NumType t)
    {
        switch (t)
        {
            case NumType::Int8: return alignof(int8_t);
            case NumType::Uint8: return alignof(uint8_t);
            case NumType::Int16: return alignof(int16_t);
            case NumType::Uint16: return alignof(uint16_t);
            case NumType::Int32: return alignof(int32_t);
            case NumType::Uint32: return alignof(uint32_t);
            case NumType::Int64: return alignof(int64_t);
            case NumType::Uint64: return alignof(uint64_t);
            case NumType::Float: return alignof(float);
            case NumType::Double: return alignof(double);
            case NumType::Bool: return alignof(bool);
            default: return 1;
        }
    }

    bool isIntegerType(NumType t)
    {
        return (t != NumType::Float) && (t != NumType::Double) && (t != NumType::Invalid);
    }

    // Buffer type from a "t" base type (DecodeGenerator.pystruct_map): the type and whether it is big endian
    bool bufTypeFromCode(const String& base, NumType& type, bool& bigEndian)
    {
        static const struct { const char* code; NumType type; bool bigEndian; } TYPES[] = {
            {"b", NumType::Int8, false}, {"B", NumType::Uint8, false},
            {">h", NumType::Int16, true}, {"<h", NumType::Int16, false},
            {">H", NumType::Uint16, true}, {"<H", NumType::Uint16, false},
            {">i", NumType::Int32, true}, {"<i", NumType::Int32, false},
            {">I", NumType::Uint32, true}, {"<I", NumType::Uint32, false},
            {">l", NumType::Int32, true}, {"<l", NumType::Int32, false},
            {">L", NumType::Uint32, true}, {"<L", NumType::Uint32, false},
            {">q", NumType::Int64, true}, {"<q", NumType::Int64, false},
            {">Q", NumType::Uint64, true}, {"<Q", NumType::Uint64, false},
            {">f", NumType::Float, true}, {"<f", NumType::Float, false},
            {">d", NumType::Double, true}, {"<d", NumType::Double, false},
        };
        for (const auto& entry : TYPES)
        {
            if (base == entry.code)
            {
                type = entry.type;
                bigEndian = entry.bigEndian;
                return true;
            }
        }
        return false;
    }

    // Struct field type from "o" (DecodeGenerator.c_like_types)
    bool outTypeFromName(const String& name, NumType& type)
    {
        static const struct { const char* name; NumType type; } TYPES[] = {
            {"int8", NumType::Int8}, {"int8_t", NumType::Int8}, {"uint8", NumType::Uint8}, {"uint8_t", NumType::Uint8},
            {"int16", NumType::Int16}, {"int16_t", NumType::Int16}, {"uint16", NumType::Uint16}, {"uint16_t", NumType::Uint16},
            {"int32", NumType::Int32}, {"int32_t", NumType::Int32}, {"uint32", NumType::Uint32}, {"uint32_t", NumType::Uint32},
            {"int64", NumType::Int64}, {"int64_t", NumType::Int64}, {"uint64", NumType::Uint64}, {"uint64_t", NumType::Uint64},
            {"float", NumType::Float}, {"double", NumType::Double}, {"bool", NumType::Bool},
        };
        for (const auto& entry : TYPES)
        {
            if (name == entry.name)
            {
                type = entry.type;
                return true;
            }
        }
        return false;
    }

    // Intermediate type (DecodeGenerator.larger_signed_c_type)
    NumType workTypeFor(NumType bufType)
    {
        switch (bufType)
        {
            case NumType::Uint8: case NumType::Int8: return NumType::Int16;
            case NumType::Uint16: case NumType::Int16: return NumType::Int32;
            case NumType::Uint32: case NumType::Int32: case NumType::Uint64: return NumType::Int64;
            default: return bufType;
        }
    }

    // Field description type (DecodeGenerator._attr_type_enum_map: from "o", else from the C type - int64 and
    // uint64 map to Int32 and Uint32, double to Float, as the build does)
    AttrType attrTypeFor(NumType outType)
    {
        switch (outType)
        {
            case NumType::Float: case NumType::Double: return AttrType::Float;
            case NumType::Int32: case NumType::Int64: return AttrType::Int32;
            case NumType::Uint32: case NumType::Uint64: return AttrType::Uint32;
            case NumType::Int16: return AttrType::Int16;
            case NumType::Uint16: return AttrType::Uint16;
            case NumType::Int8: return AttrType::Int8;
            case NumType::Bool: return AttrType::Bool;
            default: return AttrType::Uint8;
        }
    }

    // A number in the JSON as the generator writes it into C++ (an integer stays an integer literal)
    bool parseLiteral(const RaftJsonIF& json, const char* path, Literal& lit)
    {
        int arrayLen = 0;
        if (json.getType(path, arrayLen) != RaftJsonIF::RAFT_JSON_NUMBER)
            return false;
        String text = json.getString(path, "");
        lit.present = true;
        lit.isInteger = (text.indexOf('.') < 0) && (text.indexOf('e') < 0) && (text.indexOf('E') < 0);
        if (lit.isInteger)
            lit.intValue = strtoll(text.c_str(), nullptr, 10);
        lit.floatValue = json.getDouble(path, 0);
        return true;
    }

    // An integer field ("x", "m", "sb", "ss", "at"), as a number or a string the generator reads with int(v, 0)
    bool parseInt(const RaftJsonIF& json, const char* path, long long& value)
    {
        int arrayLen = 0;
        RaftJsonIF::RaftJsonType type = json.getType(path, arrayLen);
        if (type == RaftJsonIF::RAFT_JSON_NUMBER)
        {
            value = json.getLong(path, 0);
            return true;
        }
        if (type != RaftJsonIF::RAFT_JSON_STRING)
            return false;
        String text = json.getString(path, "");
        text.trim();
        if (text.length() == 0)
            return false;
        char* pEnd = nullptr;
        value = strtoll(text.c_str(), &pEnd, 0);
        return pEnd && (*pEnd == 0);
    }

    // A lookup table integer (DecodeGenerator._parse_lut_int: 0x for hex, otherwise decimal)
    bool parseLutInt(String text, long long& value)
    {
        text.trim();
        if (text.length() == 0)
            return false;
        char* pEnd = nullptr;
        bool isHex = text.startsWith("0x") || text.startsWith("0X");
        value = strtoll(text.c_str(), &pEnd, isHex ? 16 : 10);
        return pEnd && (*pEnd == 0);
    }

    // A working value: the generator's intermediate value, held as an int64_t (wrapped to the intermediate type's
    // width after each operation, as assigning to the narrower C++ variable does) or, for a float type, a double
    // holding the float's value. Bitwise operations, tests and modular arithmetic on the 64-bit value give the same
    // bits in the intermediate width as the generated code does with any C literal type
    struct WorkValue
    {
        bool isFloat = false;
        int64_t i = 0;
        double d = 0;       // a double intermediate
        float fl = 0;       // a float intermediate (kept as a float, so a NaN's bits are kept as the generated code keeps them)
    };

    int64_t wrapTo(NumType workType, uint64_t v)
    {
        switch (workType)
        {
            case NumType::Int16: return (int16_t)(uint16_t)v;
            case NumType::Int32: return (int32_t)(uint32_t)v;
            default: return (int64_t)v;
        }
    }

    // Read a buffer value with the generator's reader (the reader's return value, converted to the intermediate)
    WorkValue readValue(NumType type, NumType workType, bool bigEndian, const uint8_t*& pPos, const uint8_t* pBufEnd)
    {
        WorkValue v;
        switch (type)
        {
            case NumType::Int8: v.i = Raft::getInt8AndInc(pPos, pBufEnd); break;
            case NumType::Uint8: v.i = Raft::getUInt8AndInc(pPos, pBufEnd); break;
            case NumType::Int16: v.i = bigEndian ? Raft::getBEInt16AndInc(pPos, pBufEnd) : Raft::getLEInt16AndInc(pPos, pBufEnd); break;
            case NumType::Uint16: v.i = bigEndian ? Raft::getBEUInt16AndInc(pPos, pBufEnd) : Raft::getLEUInt16AndInc(pPos, pBufEnd); break;
            case NumType::Int32: v.i = bigEndian ? Raft::getBEInt32AndInc(pPos, pBufEnd) : Raft::getLEInt32AndInc(pPos, pBufEnd); break;
            case NumType::Uint32: v.i = bigEndian ? Raft::getBEUInt32AndInc(pPos, pBufEnd) : Raft::getLEUInt32AndInc(pPos, pBufEnd); break;
            case NumType::Int64: v.i = bigEndian ? Raft::getBEInt64AndInc(pPos, pBufEnd) : Raft::getLEInt64AndInc(pPos, pBufEnd); break;
            case NumType::Uint64:
                v.i = (int64_t)(bigEndian ? Raft::getBEUInt64AndInc(pPos, pBufEnd) : Raft::getLEUInt64AndInc(pPos, pBufEnd));
                break;
            case NumType::Float:
                v.isFloat = true;
                v.fl = bigEndian ? Raft::getBEfloat32AndInc(pPos, pBufEnd) : Raft::getLEfloat32AndInc(pPos, pBufEnd);
                v.d = v.fl;
                break;
            case NumType::Double:
                v.isFloat = true;
                v.d = bigEndian ? Raft::getBEdouble64AndInc(pPos, pBufEnd) : Raft::getLEdouble64AndInc(pPos, pBufEnd);
                break;
            default: break;
        }
        if (!v.isFloat)
            v.i = wrapTo(workType, (uint64_t)v.i);
        return v;
    }

    // The type a signed buffer type is read as when it has a mask (DecodeGenerator: base_type.upper())
    NumType unsignedOf(NumType type)
    {
        switch (type)
        {
            case NumType::Int8: return NumType::Uint8;
            case NumType::Int16: return NumType::Uint16;
            case NumType::Int32: return NumType::Uint32;
            case NumType::Int64: return NumType::Uint64;
            default: return type;
        }
    }

    // The operations on the intermediate value (x, m, sb/ss, s), as the generated code
    void applyOps(const DeviceTypeRuntimeDecoder::Field& f, WorkValue& v)
    {
        if (v.isFloat)
        {
            // Only a shift compiles on a float intermediate in the generated code; the result goes back to the float type
            if (f.shift != 0)
            {
                bool isFloatWork = (f.workType == NumType::Float);
                uint32_t asUint = isFloatWork ? (uint32_t)v.fl : (uint32_t)v.d;
                if (f.shift > 0)
                {
                    uint32_t shifted = asUint >> (f.shift & 31);
                    v.fl = (float)shifted;
                    v.d = isFloatWork ? (double)v.fl : (double)shifted;
                }
                else
                {
                    int32_t shifted = (int32_t)(asUint << ((-f.shift) & 31));
                    v.fl = (float)shifted;
                    v.d = isFloatWork ? (double)v.fl : (double)shifted;
                }
            }
            return;
        }
        uint64_t u = (uint64_t)v.i;
        if (f.hasX)
            u = (uint64_t)wrapTo(f.workType, u ^ f.xMask);
        if (f.hasM)
        {
            if (f.isSignedBuf)
            {
                uint64_t signBitMask = (f.mMask + 1) >> 1;
                if (u & signBitMask)
                    u = (uint64_t)wrapTo(f.workType, u | (~f.mMask & ~signBitMask));
            }
            else
            {
                u = (uint64_t)wrapTo(f.workType, u & f.mMask);
            }
        }
        if (f.hasSb)
        {
            uint64_t signBitMask = 1ULL << f.sbPos;
            if (u & signBitMask)
                u = (uint64_t)wrapTo(f.workType, f.hasSs ? ((uint64_t)f.ss - u) : (u - (signBitMask << 1)));
        }
        if (f.shift > 0)
            u = (uint64_t)wrapTo(f.workType, (uint32_t)u >> (f.shift & 31));
        else if (f.shift < 0)
            u = (uint64_t)wrapTo(f.workType, (uint64_t)(int64_t)(int32_t)((uint32_t)u << ((-f.shift) & 31)));
        v.i = (int64_t)u;
    }

    // Apply a lookup table (DecodeGenerator.gen_lut_code) to a double
    void applyLut(const DeviceTypeRuntimeDecoder::Field& f, double& v)
    {
        if (v != v)
            return;
        long long rounded = (long long)(v + 0.5);
        if ((double)rounded > v + 0.5)
            rounded--;
        for (const auto& row : f.lutRows)
        {
            for (const auto& range : row.ranges)
            {
                if ((rounded >= range.start) && (rounded <= range.end))
                {
                    v = row.value;
                    return;
                }
            }
        }
        if (f.lutHasDefault)
            v = f.lutDefault;
    }

    // Divide or offset a value by a literal as the generated "out /= d;" or "out += a;": with an integer literal of
    // the C type it would have (int, or long long if it doesn't fit), or a double
    template <typename T>
    void applyLiteral(T& out, const Literal& lit, bool divide)
    {
        if (!lit.present || (lit.floatValue == 0))
            return;
        if (!lit.isInteger)
        {
            if (divide)
                out /= lit.floatValue;
            else
                out += lit.floatValue;
        }
        else if ((lit.intValue >= INT_MIN) && (lit.intValue <= INT_MAX))
        {
            if (divide)
                out /= (int)lit.intValue;
            else
                out += (int)lit.intValue;
        }
        else
        {
            if (divide)
                out /= (long long)lit.intValue;
            else
                out += (long long)lit.intValue;
        }
    }

    // Store the intermediate value to a struct field of type OUT, then divide and offset in that type (or, with a
    // lookup table, work in a double and store the result). Converting the 64-bit (or double) working value gives
    // what converting the narrower intermediate does, as it holds the same value
    template <typename OUT>
    void storeAs(const DeviceTypeRuntimeDecoder::Field& f, const WorkValue& v, uint8_t* pDest)
    {
        OUT out;
        if (f.hasLut)
        {
            double lut = v.isFloat ? ((f.workType == NumType::Float) ? (double)v.fl : v.d) : (double)v.i;
            applyLiteral(lut, f.divisor, true);
            applyLiteral(lut, f.addend, false);
            applyLut(f, lut);
            out = (OUT)lut;
        }
        else
        {
            if (v.isFloat)
                out = (f.workType == NumType::Float) ? (OUT)v.fl : (OUT)v.d;
            else
                out = (OUT)v.i;
            applyLiteral(out, f.divisor, true);
            applyLiteral(out, f.addend, false);
        }
        memcpy(pDest, &out, sizeof(OUT));
    }

    void store(const DeviceTypeRuntimeDecoder::Field& f, const WorkValue& v, uint8_t* pDest)
    {
        switch (f.outType)
        {
            case NumType::Int8: storeAs<int8_t>(f, v, pDest); break;
            case NumType::Uint8: storeAs<uint8_t>(f, v, pDest); break;
            case NumType::Int16: storeAs<int16_t>(f, v, pDest); break;
            case NumType::Uint16: storeAs<uint16_t>(f, v, pDest); break;
            case NumType::Int32: storeAs<int32_t>(f, v, pDest); break;
            case NumType::Uint32: storeAs<uint32_t>(f, v, pDest); break;
            case NumType::Int64: storeAs<int64_t>(f, v, pDest); break;
            case NumType::Uint64: storeAs<uint64_t>(f, v, pDest); break;
            case NumType::Float: storeAs<float>(f, v, pDest); break;
            case NumType::Double: storeAs<double>(f, v, pDest); break;
            case NumType::Bool: storeAs<bool>(f, v, pDest); break;
            default: break;
        }
    }

    // Decode one element: read, operate on the intermediate value, store
    void decodeElem(const DeviceTypeRuntimeDecoder::Field& f, const uint8_t*& pPos, const uint8_t* pReadEnd, uint8_t* pDest)
    {
        NumType readType = f.readUnsigned ? unsignedOf(f.bufType) : f.bufType;
        WorkValue v = readValue(readType, f.workType, f.bigEndian, pPos, pReadEnd);
        applyOps(f, v);
        store(f, v, pDest);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create and destroy
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DeviceTypeRuntimeDecoder* DeviceTypeRuntimeDecoder::create(const char* pDevInfoJson, String& problem)
{
    problem = "";
    DeviceTypeRuntimeDecoder* pDecoder = new (std::nothrow) DeviceTypeRuntimeDecoder();
    if (!pDecoder)
    {
        problem = "out of memory";
        return nullptr;
    }
    if (!pDecoder->parse(pDevInfoJson, problem))
    {
        delete pDecoder;
        return nullptr;
    }

    // Take a slot
    for (uint32_t slot = 0; slot < RAFT_RUNTIME_DECODE_MAX_DECODERS; slot++)
    {
        const DeviceTypeRuntimeDecoder* pFree = nullptr;
        if (g_decoderSlots[slot].compare_exchange_strong(pFree, pDecoder, std::memory_order_acq_rel))
        {
            pDecoder->_slot = (int32_t)slot;
            pDecoder->_decodeFn = g_slotFns[slot];
            return pDecoder;
        }
    }
    problem = "no runtime decoder slot free (RAFT_RUNTIME_DECODE_MAX_DECODERS " + String(RAFT_RUNTIME_DECODE_MAX_DECODERS) + ")";
    delete pDecoder;
    return nullptr;
}

DeviceTypeRuntimeDecoder::~DeviceTypeRuntimeDecoder()
{
    if ((_slot >= 0) && (_slot < RAFT_RUNTIME_DECODE_MAX_DECODERS))
        g_decoderSlots[_slot].store(nullptr, std::memory_order_release);
}

uint32_t DeviceTypeRuntimeDecoder::getNumSlotsInUse()
{
    uint32_t count = 0;
    for (uint32_t slot = 0; slot < RAFT_RUNTIME_DECODE_MAX_DECODERS; slot++)
        if (g_decoderSlots[slot].load(std::memory_order_acquire))
            count++;
    return count;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Parse the response attributes into fields and the struct layout
bool DeviceTypeRuntimeDecoder::parse(const char* pDevInfoJson, String& problem)
{
    RaftJson devInfo(pDevInfoJson ? pDevInfoJson : "{}", false);
    int arrayLen = 0;
    if (devInfo.getType("resp", arrayLen) != RaftJsonIF::RAFT_JSON_OBJECT)
    {
        problem = "no response format (resp)";
        return false;
    }
    if (devInfo.contains("resp/c"))
    {
        problem = "custom decode code (resp.c) needs the code interpreter";
        return false;
    }
    int numAttrs = 0;
    if ((devInfo.getType("resp/a", numAttrs) != RaftJsonIF::RAFT_JSON_ARRAY) || (numAttrs == 0))
    {
        problem = "no attributes (resp.a)";
        return false;
    }
    if (!devInfo.contains("resp/b"))
    {
        problem = "no response size (resp.b)";
        return false;
    }
    long respBytes = devInfo.getLong("resp/b", 0);
    if ((respBytes < 0) || (respBytes + DevicePollingInfo::POLL_RESULT_TIMESTAMP_SIZE > UINT16_MAX))
    {
        problem = "invalid response size (resp.b)";
        return false;
    }
    _pollRecSize = (uint16_t)(respBytes + DevicePollingInfo::POLL_RESULT_TIMESTAMP_SIZE);

    // Struct layout: the timestamp, then each attribute's field in order, aligned as the compiler aligns it
    uint32_t offset = sizeof(uint32_t);
    uint32_t maxAlign = alignof(uint32_t);
    for (int attrIdx = 0; attrIdx < numAttrs; attrIdx++)
    {
        String prefix = "resp/a[" + String(attrIdx) + "]/";
        String name = devInfo.getString((prefix + "n").c_str(), "");
        if (name.length() == 0)
            continue;
        for (const String& existing : _names)
        {
            if (existing == name)
            {
                problem = "attribute " + name + " appears twice";
                return false;
            }
        }
        Field f;

        // Type: optional endian, optional count, code, optional [N]
        String typeStr = devInfo.getString((prefix + "t").c_str(), "");
        typeStr.trim();
        String endian = "";
        uint32_t idx = 0;
        if ((typeStr.length() > 0) && ((typeStr[0] == '<') || (typeStr[0] == '>')))
            endian = String(typeStr[idx++]);
        uint32_t prefixCount = 0;
        bool hasPrefixCount = false;
        while ((idx < typeStr.length()) && isdigit((unsigned char)typeStr[idx]))
        {
            prefixCount = prefixCount * 10 + (typeStr[idx++] - '0');
            hasPrefixCount = true;
        }
        if (idx >= typeStr.length())
        {
            problem = "attribute " + name + " has no valid type (t)";
            return false;
        }
        char code = typeStr[idx++];
        uint32_t bracketCount = 1;
        if ((idx < typeStr.length()) && (typeStr[idx] == '['))
        {
            int close = typeStr.indexOf(']', idx);
            if ((close < 0) || (close != (int)typeStr.length() - 1))
            {
                problem = "attribute " + name + " has an invalid type " + typeStr;
                return false;
            }
            bracketCount = (uint32_t)strtoul(typeStr.substring(idx + 1, close).c_str(), nullptr, 10);
            idx = close + 1;
        }
        if (idx != typeStr.length())
        {
            problem = "attribute " + name + " has an invalid type " + typeStr;
            return false;
        }
        if (!bufTypeFromCode(endian + String(code), f.bufType, f.bigEndian))
        {
            problem = "attribute " + name + " has a type the build can't decode: " + typeStr;
            return false;
        }
        uint32_t count = (hasPrefixCount ? prefixCount : 1) * (bracketCount > 0 ? bracketCount : 1);
        f.count = (uint16_t)(count > 0 ? count : 1);
        f.isSignedBuf = (f.bufType == NumType::Int8) || (f.bufType == NumType::Int16) ||
                    (f.bufType == NumType::Int32) || (f.bufType == NumType::Int64);
        f.workType = workTypeFor(f.bufType);

        // Struct field type ("o", else the buffer type)
        String outName = devInfo.getString((prefix + "o").c_str(), "");
        if (!outTypeFromName(outName, f.outType))
            f.outType = f.bufType;

        // Position
        int atLen = 0;
        RaftJsonIF::RaftJsonType atType = devInfo.getType((prefix + "at").c_str(), atLen);
        if (atType == RaftJsonIF::RAFT_JSON_ARRAY)
        {
            if (f.count > 1)
            {
                problem = "attribute " + name + " is an array with a list of byte positions (at)";
                return false;
            }
            for (int i = 0; i < atLen; i++)
            {
                long long pos = 0;
                if (!parseInt(devInfo, (prefix + "at[" + String(i) + "]").c_str(), pos) || (pos < 0) || (pos > UINT16_MAX))
                {
                    problem = "attribute " + name + " has an invalid position (at)";
                    return false;
                }
                f.atList.push_back((uint16_t)pos);
            }
        }
        else if (atType != RaftJsonIF::RAFT_JSON_UNDEFINED)
        {
            long long pos = 0;
            if (!parseInt(devInfo, (prefix + "at").c_str(), pos) || (pos < 0) || (pos > UINT16_MAX))
            {
                problem = "attribute " + name + " has an invalid position (at)";
                return false;
            }
            f.at = (int32_t)pos;
        }

        // Operations
        bool isFloatBuf = !isIntegerType(f.bufType);
        long long intVal = 0;
        if (devInfo.contains((prefix + "x").c_str()))
        {
            if (isFloatBuf || !parseInt(devInfo, (prefix + "x").c_str(), intVal))
            {
                problem = "attribute " + name + " has an XOR mask (x) the build can't apply";
                return false;
            }
            f.hasX = true;
            f.xMask = (unsigned long long)intVal;
        }
        if (devInfo.contains((prefix + "m").c_str()))
        {
            if (isFloatBuf || !parseInt(devInfo, (prefix + "m").c_str(), intVal))
            {
                problem = "attribute " + name + " has a mask (m) the build can't apply";
                return false;
            }
            f.hasM = true;
            f.mMask = (unsigned long long)intVal;
            f.readUnsigned = f.isSignedBuf;
        }
        if (devInfo.contains((prefix + "sb").c_str()))
        {
            if (isFloatBuf || !parseInt(devInfo, (prefix + "sb").c_str(), intVal) || (intVal < 0) || (intVal > 62))
            {
                problem = "attribute " + name + " has a sign bit (sb) the build can't apply";
                return false;
            }
            f.hasSb = true;
            f.sbPos = (uint32_t)intVal;
            if (devInfo.contains((prefix + "ss").c_str()))
            {
                if (!parseInt(devInfo, (prefix + "ss").c_str(), intVal))
                {
                    problem = "attribute " + name + " has an invalid sign subtract (ss)";
                    return false;
                }
                f.hasSs = true;
                f.ss = intVal;
            }
        }
        f.shift = (int32_t)devInfo.getLong((prefix + "s").c_str(), 0);
        parseLiteral(devInfo, (prefix + "d").c_str(), f.divisor);
        parseLiteral(devInfo, (prefix + "a").c_str(), f.addend);

        // Lookup table (rows with a value; "r" "" is the default - the last one; an unparseable range is ignored)
        int lutLen = 0;
        if (devInfo.getType((prefix + "lut").c_str(), lutLen) == RaftJsonIF::RAFT_JSON_ARRAY)
        {
            for (int rowIdx = 0; rowIdx < lutLen; rowIdx++)
            {
                String rowPath = prefix + "lut[" + String(rowIdx) + "]/";
                int rowLen = 0;
                if ((devInfo.getType((prefix + "lut[" + String(rowIdx) + "]").c_str(), rowLen) != RaftJsonIF::RAFT_JSON_OBJECT) ||
                            !devInfo.contains((rowPath + "v").c_str()))
                    continue;
                double value = devInfo.getDouble((rowPath + "v").c_str(), 0);
                String rangeStr = devInfo.getString((rowPath + "r").c_str(), "");
                if (rangeStr.length() == 0)
                {
                    f.lutHasDefault = true;
                    f.lutDefault = value;
                    continue;
                }
                LutRow row;
                row.value = value;
                int start = 0;
                while (start <= (int)rangeStr.length())
                {
                    int comma = rangeStr.indexOf(',', start);
                    if (comma < 0)
                        comma = rangeStr.length();
                    String part = rangeStr.substring(start, comma);
                    start = comma + 1;
                    int dash = part.indexOf('-');
                    LutRange range;
                    if (dash >= 0)
                    {
                        String endStr = part.substring(dash + 1);
                        int secondDash = endStr.indexOf('-');
                        if (secondDash >= 0)
                            endStr = endStr.substring(0, secondDash);
                        if (parseLutInt(part.substring(0, dash), range.start) && parseLutInt(endStr, range.end))
                            row.ranges.push_back(range);
                    }
                    else if (parseLutInt(part, range.start))
                    {
                        range.end = range.start;
                        row.ranges.push_back(range);
                    }
                }
                if (!row.ranges.empty())
                    f.lutRows.push_back(row);
            }
            f.hasLut = !f.lutRows.empty() || f.lutHasDefault;
        }

        // Field position in the struct
        uint32_t align = numTypeAlign(f.outType);
        offset = (offset + align - 1) / align * align;
        f.outOffset = (uint16_t)offset;
        offset += numTypeSize(f.outType) * f.count;
        if (align > maxAlign)
            maxAlign = align;
        if (offset > UINT16_MAX)
        {
            problem = "decoded struct too large";
            return false;
        }

        _fields.push_back(f);
        _names.push_back(name);
        _formats.push_back(devInfo.getString((prefix + "f").c_str(), ""));
    }
    if (_fields.empty())
    {
        problem = "no named attributes";
        return false;
    }
    _structSize = (uint16_t)((offset + maxAlign - 1) / maxAlign * maxAlign);

    // Field descriptions (the names and formats are kept in the vectors above - reserve so they don't move)
    for (uint32_t i = 0; i < _fields.size(); i++)
    {
        AttrFieldDesc desc;
        desc.name = _names[i].c_str();
        desc.offset = _fields[i].outOffset;
        desc.type = attrTypeFor(_fields[i].outType);
        desc.fmtStr = _formats[i].c_str();
        desc.divisor = 1.0f;
        desc.addend = 0.0f;
        desc.count = _fields[i].count;
        _fieldDescs.push_back(desc);
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Decode poll results
uint32_t DeviceTypeRuntimeDecoder::decode(const uint8_t* pBufIn, uint32_t bufLen, void* pStructOut, uint32_t structOutSize,
            uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) const
{
    if (!pBufIn || !pStructOut || (_structSize == 0))
        return 0;
    uint32_t maxRecs = std::min<uint32_t>(maxRecCount, structOutSize / _structSize);
    const uint8_t* pBufEnd = pBufIn + bufLen;
    uint32_t numRecs = 0;
    while (numRecs < maxRecs)
    {
        const uint8_t* pBuf = pBufIn + (uint32_t)_pollRecSize * numRecs;
        if (pBuf + _pollRecSize > pBufEnd)
            break;
        uint8_t* pOut = (uint8_t*)pStructOut + (uint32_t)_structSize * numRecs;

        // Timestamp
        uint64_t timestampUs = Raft::getBEUInt16AndInc(pBuf, pBufEnd) * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;
        if (timestampUs < decodeState.lastReportTimestampUs)
            decodeState.reportTimestampOffsetUs += DevicePollingInfo::POLL_RESULT_WRAP_VALUE * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;
        decodeState.lastReportTimestampUs = timestampUs;
        timestampUs += decodeState.reportTimestampOffsetUs;
        uint32_t timeVal = (uint32_t)(timestampUs / DECODE_STRUCT_TIMESTAMP_RESOLUTION_US);
        memcpy(pOut, &timeVal, sizeof(timeVal));

        // Attributes
        if (!decodeAttrs(pBuf, pBufEnd, pOut))
            break;
        numRecs++;
    }
    return numRecs;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Decode one record's attributes; false if the buffer ends first (as the generated code breaks out)
bool DeviceTypeRuntimeDecoder::decodeAttrs(const uint8_t* pBuf, const uint8_t* pBufEnd, uint8_t* pOut) const
{
    const uint8_t* pAttrStart = pBuf;
    for (const Field& f : _fields)
    {
        uint32_t elemSize = numTypeSize(f.bufType);
        if (!f.atList.empty())
        {
            // Bytes from the listed positions, assembled into one value (positions beyond the buffer give 0)
            uint8_t bytes[8] = {};
            for (uint32_t i = 0; (i < f.atList.size()) && (i < elemSize); i++)
                if (pAttrStart + f.atList[i] < pBufEnd)
                    bytes[i] = pAttrStart[f.atList[i]];
            const uint8_t* pPos = bytes;
            decodeElem(f, pPos, bytes + elemSize, pOut + f.outOffset);
            continue;
        }
        const uint8_t*& pPos = pBuf;
        const uint8_t* pAbsPos = (f.at >= 0) ? pAttrStart + f.at : nullptr;
        const uint8_t*& pRead = (f.at >= 0) ? pAbsPos : pPos;
        if (pRead + (uint32_t)f.count * elemSize > pBufEnd)
            return false;
        uint32_t outElemSize = numTypeSize(f.outType);
        for (uint32_t elemIdx = 0; elemIdx < f.count; elemIdx++)
            decodeElem(f, pRead, pBufEnd, pOut + f.outOffset + elemIdx * outElemSize);
    }
    return true;
}

#else // RAFT_RUNTIME_DECODE

// Without runtime decoding: no decoders are made
DeviceTypeRuntimeDecoder* DeviceTypeRuntimeDecoder::create(const char* pDevInfoJson, String& problem)
{
    problem = "runtime decoding not built in (RAFT_RUNTIME_DECODE)";
    return nullptr;
}
DeviceTypeRuntimeDecoder::~DeviceTypeRuntimeDecoder()
{
}
uint32_t DeviceTypeRuntimeDecoder::decode(const uint8_t*, uint32_t, void*, uint32_t, uint16_t, RaftBusDeviceDecodeState&) const
{
    return 0;
}
uint32_t DeviceTypeRuntimeDecoder::getNumSlotsInUse()
{
    return 0;
}

#endif // RAFT_RUNTIME_DECODE
