/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DeviceTypeRuntimeDecoder.h
//
// Decodes poll results for a device type record that has no compiled decoder (a record from a device type file, or
// one registered by code), from its devInfoJson "resp" attribute list - the same decode the build generates for a
// compiled record (DecodeGenerator.py), into the same struct layout, with the same field descriptions. So
// DataLogger, RaftROS, named values and anything else that uses a record's pollResultDecodeFn, pollFieldDescs and
// pollStructSize work for such a record too.
//
// Built in with RAFT_RUNTIME_DECODE (RaftCore CMake: set(RAFT_RUNTIME_DECODE OFF) in features.cmake leaves it out).
// A record with custom decode code ("c") is decoded by the code interpreter (DeviceTypeCodeInterpreter.h), with
// RAFT_RUNTIME_DECODE_CODE as well, in the generated custom decode's loop (next, array zeroing, sign extension).
//
// The decoder is called through a plain DeviceTypeRecordDecodeFn: each decoder takes one of a fixed number of
// slots, and each slot has its own small function (RAFT_RUNTIME_DECODE_MAX_DECODERS, default 64).
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <vector>
#include <atomic>
#include "RaftArduino.h"
#include "DeviceTypeRecord.h"

class DeviceTypeCodeProgram;

#ifndef RAFT_RUNTIME_DECODE_MAX_DECODERS
#define RAFT_RUNTIME_DECODE_MAX_DECODERS 64
#endif

class DeviceTypeRuntimeDecoder
{
public:
    ~DeviceTypeRuntimeDecoder();

    /// @brief Create a decoder for a record's response attributes
    /// @param pDevInfoJson the record's devInfoJson
    /// @param problem (out) why there is no decoder, if there isn't one
    /// @param takeSlot false for a decoder used only through decode() (getDecodeFn() is then nullptr), which needs no slot
    /// @return the decoder, or nullptr if the record has nothing to decode, needs custom decode code, has an attribute
    ///         the build couldn't decode, or no decoder slot is free. The caller owns it
    static DeviceTypeRuntimeDecoder* create(const char* pDevInfoJson, String& problem, bool takeSlot = true);

    /// @brief The decode function for this decoder (a slot's function, valid while the decoder exists)
    DeviceTypeRecordDecodeFn getDecodeFn() const
    {
        return _decodeFn;
    }

    /// @brief Field descriptions of the decoded struct (as the build generates for a compiled record)
    const AttrFieldDesc* getFieldDescs() const
    {
        return _fieldDescs.data();
    }
    uint16_t getFieldCount() const
    {
        return (uint16_t)_fieldDescs.size();
    }
    uint16_t getStructSize() const
    {
        return _structSize;
    }

    /// @brief Decode poll results (the DeviceTypeRecordDecodeFn contract)
    uint32_t decode(const uint8_t* pBufIn, uint32_t bufLen, void* pStructOut, uint32_t structOutSize,
                uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) const;

    /// @brief Number of decoder slots in use (for tests and diagnostics)
    static uint32_t getNumSlotsInUse();

    /// @brief Custom decode code: the deepest nesting of expressions and of statements its runs have reached (each
    ///        level is an interpreter stack frame - the stack a decode needs)
    uint32_t getMaxEvalDepth() const
    {
        return _maxEvalDepth.load(std::memory_order_relaxed);
    }
    uint32_t getMaxExecDepth() const
    {
        return _maxExecDepth.load(std::memory_order_relaxed);
    }

    /// @brief Custom decode code run as bytecode (DeviceTypeCodeProgram::usesBytecode)
    bool usesBytecode() const;

    /// @brief Custom decode code: interpreter steps (expression and statement nodes) run in all, for timing
    uint64_t getNodesRun() const
    {
        return _nodesRun.load(std::memory_order_relaxed);
    }

    // Numeric types (of a value in the buffer, a working value or a struct field)
    enum class NumType : uint8_t
    {
        Int8, Uint8, Int16, Uint16, Int32, Uint32, Int64, Uint64, Float, Double, Bool, Invalid
    };

    // A literal as the generated C++ writes it: an integer (with whether it fits in an int) or a floating value
    struct Literal
    {
        bool present = false;
        bool isInteger = true;
        long long intValue = 0;
        double floatValue = 0;
    };

    // A lookup table row (r as ranges, v the value)
    struct LutRange
    {
        long long start = 0;
        long long end = 0;
    };
    struct LutRow
    {
        std::vector<LutRange> ranges;
        double value = 0;
    };

    // One attribute
    struct Field
    {
        NumType bufType = NumType::Invalid;     // type in the poll data ("t")
        bool bigEndian = false;
        bool readUnsigned = false;              // a signed type with "m" is read unsigned
        NumType workType = NumType::Invalid;    // working value type (the generator's intermediate type)
        NumType outType = NumType::Invalid;     // struct field type ("o", else from "t")
        uint16_t outOffset = 0;
        uint16_t count = 1;                     // elements (array attributes)
        int32_t at = -1;                        // absolute position, or -1 for sequential
        std::vector<uint16_t> atList;           // non-contiguous byte positions
        bool hasX = false;
        unsigned long long xMask = 0;
        bool hasM = false;
        unsigned long long mMask = 0;
        bool isSignedBuf = false;
        bool hasSb = false;
        uint32_t sbPos = 0;
        bool hasSs = false;
        long long ss = 0;
        int32_t shift = 0;
        Literal divisor;
        Literal addend;
        bool hasLut = false;
        std::vector<LutRow> lutRows;
        bool lutHasDefault = false;
        double lutDefault = 0;
        // Custom decode: a float field ("o" "float") of a signed buffer type is sign-extended at next (the bits of
        // the buffer type: 8, 16 or 32, or 0 for none)
        uint8_t signExtBits = 0;
    };

private:
    DeviceTypeRuntimeDecoder() = default;
    bool parse(const char* pDevInfoJson, String& problem);

    // Decode one record's attributes into pOut; false if the buffer ends (the record isn't counted)
    bool decodeAttrs(const uint8_t* pBuf, const uint8_t* pBufEnd, uint8_t* pOut) const;

    // Custom decode code (RAFT_RUNTIME_DECODE_CODE)
    uint32_t decodeCustom(const uint8_t* pBufIn, uint32_t bufLen, uint8_t* pOut, uint32_t maxRecs,
                RaftBusDeviceDecodeState& decodeState) const;
    void zeroArrays(uint8_t* pOut) const;
    void signExtend(uint8_t* pOut) const;
    static bool customNext(void* pContext);
    static bool hasNextKeyword(const String& code);
    bool addFieldLayout(Field& f, const String& name, const String& format, uint32_t& offset, uint32_t& maxAlign,
                String& problem);
    DeviceTypeCodeProgram* _pProgram = nullptr;
    uint32_t _sampleIncUs = 0;
    mutable std::atomic<uint32_t> _maxEvalDepth{0};
    mutable std::atomic<uint32_t> _maxExecDepth{0};
    mutable std::atomic<uint64_t> _nodesRun{0};
    void noteDepths(uint32_t evalDepth, uint32_t execDepth) const
    {
        if (evalDepth > _maxEvalDepth.load(std::memory_order_relaxed))
            _maxEvalDepth.store(evalDepth, std::memory_order_relaxed);
        if (execDepth > _maxExecDepth.load(std::memory_order_relaxed))
            _maxExecDepth.store(execDepth, std::memory_order_relaxed);
    }

    std::vector<Field> _fields;
    std::vector<AttrFieldDesc> _fieldDescs;
    std::vector<String> _names;
    std::vector<String> _formats;
    uint16_t _structSize = 0;
    uint16_t _pollRecSize = 0;
    int32_t _slot = -1;
    DeviceTypeRecordDecodeFn _decodeFn = nullptr;
};
