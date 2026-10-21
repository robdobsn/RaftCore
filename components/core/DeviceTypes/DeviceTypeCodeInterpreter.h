/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DeviceTypeCodeInterpreter.h
//
// Runs a device type record's code - custom decode code (resp.c) or init code (initCode) - on the device, for a
// record that has no compiled function (RAFT_RUNTIME_DECODE_CODE). The code is the decode language
// (PseudocodeHandler.py), which the build passes token by token into C++, so its meaning is C++'s: int is 32-bit,
// float is 32-bit, a literal such as 16.0 is a double, buf[i] is an unsigned byte promoted to int, out.<name> has
// its struct field's type. compile() parses the code once into a typed tree, working out each expression's type
// by C++'s rules (literal types, promotions, the usual arithmetic conversions); run() evaluates it with the same
// C++ operations on those types, so it gives what the generated code gives. Unlike compiled code it bounds-checks
// buf[], gives 0 for an integer division by zero, and stops after an instruction budget.
//
// Accepted: declarations (int, float, int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t), assignment and the
// compound assignments, ++ and --, casts to those types, if/else, while, return, next (decode code), C operators
// (no ?: or comma operator), decimal, hex and float literals, buf[expr], out.<attribute> (decode code: written, or
// read if a scalar; an array attribute only written, out.<name>[i] = value), init.<name> (init code).
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <vector>
#include "RaftArduino.h"
#include "DeviceTypeRuntimeDecoder.h"

class DeviceTypeCodeProgram
{
public:
    using NumType = DeviceTypeRuntimeDecoder::NumType;

    // What the code may write
    struct OutField
    {
        String name;
        NumType type = NumType::Invalid;
        uint16_t offset = 0;
        uint16_t count = 1;         // >1 for an array attribute
    };

    enum class Kind : uint8_t
    {
        Decode,     // out.<attribute> and next
        Init,       // init.<name>
    };

    /// @brief Compile code
    /// @param pCode the code
    /// @param kind decode or init code
    /// @param outFields the struct fields decode code may write (decode code)
    /// @param problem (out) why the code can't be compiled
    /// @return the program, or nullptr. The caller owns it
    static DeviceTypeCodeProgram* compile(const char* pCode, Kind kind, const std::vector<OutField>& outFields, String& problem);

    // The result of running the code
    enum class Status : uint8_t
    {
        Done,           // ran to the end
        Break,          // next with the output full, outside any while (the decode stops)
        Return,         // return <value>
        OutOfBudget,    // ran too long
    };

    // Called at next: moves to the next output record and returns false if the output is full
    typedef bool (*NextFn)(void* pContext);

    // Run context
    struct RunContext
    {
        const uint8_t* pBuf = nullptr;      // buf
        uint32_t bufLen = 0;                // bytes readable from pBuf
        uint8_t* pOut = nullptr;            // the current output struct (decode code; changed by next)
        int32_t* pInitValues = nullptr;     // init values (init code)
        uint32_t numInitValues = 0;
        NextFn nextFn = nullptr;
        void* pNextContext = nullptr;
        uint32_t budget = 0;                // instructions left (decremented)
        int64_t returnValue = 0;            // the value of return, as converted to the function's result
        uint32_t maxEvalDepth = 0;          // deepest nesting of expression evaluation reached (stack use)
        uint32_t maxExecDepth = 0;          // deepest nesting of statements reached (stack use)
    };

    /// @brief Run the code
    Status run(RunContext& ctx) const;

    /// @brief Whether the code has a next statement (decode code without one gets one at the end)
    bool hasNext() const;

    /// @brief Whether run() uses bytecode: code within the bytecode's subset (32-bit integer values, variables, buf[],
    ///        out.<field> stores, the integer operators, if, while, next, return) is also compiled to bytecode, run by
    ///        a loop with no recursion and a fixed stack; other code is run from its tree
    bool usesBytecode() const
    {
        return _bytecodeOk && s_bytecodeEnabled;
    }

    /// @brief Use bytecode where it can be (default true) - false runs every program from its tree (tests, timing)
    static void setBytecodeEnabled(bool enabled)
    {
        s_bytecodeEnabled = enabled;
    }

    /// @brief Names of the init values, in order of first use (init code)
    const std::vector<String>& getInitValueNames() const
    {
        return _initNames;
    }

    // Value types of expressions (after promotion)
    enum class VT : uint8_t
    {
        I32, U32, I64, U64, F32, F64,
    };

    // A value: only the member for its type is used (16 bytes - the interpreter's frames hold several, and its
    // stack use is a frame per level of nesting)
    struct Val
    {
        VT t = VT::I32;
        union
        {
            int64_t i = 0;  // I32, I64 (and U32 held as its value)
            uint64_t u;     // U64
            float f;        // F32
            double d;       // F64
        };
    };

    // Tree node
    struct Node
    {
        uint8_t op = 0;
        VT type = VT::I32;          // expression type
        NumType storeType = NumType::Int32; // variable / field / cast type
        int32_t a = -1;             // children (node indices) or a slot / field / index
        int32_t b = -1;
        int32_t c = -1;
        Val lit;                    // literal
    };

private:
    friend class DeviceTypeCodeCompiler;
    std::vector<Node> _nodes;
    std::vector<int32_t> _lists;        // statement lists (blocks)
    std::vector<OutField> _outFields;
    std::vector<String> _initNames;
    std::vector<NumType> _slotTypes;    // variable slots
    int32_t _root = -1;
    Kind _kind = Kind::Decode;

    struct Exec;
    Val eval(int32_t nodeIdx, Exec& ex) const;
    Status exec(int32_t nodeIdx, Exec& ex) const;

    // Bytecode: an instruction (an operation, a type or kind, and two operands)
    struct Ins
    {
        uint8_t op = 0;
        uint8_t t = 0;
        int16_t a = 0;
        int32_t b = 0;
    };
    std::vector<Ins> _code;
    bool _bytecodeOk = false;
    static bool s_bytecodeEnabled;
    friend class DeviceTypeBytecodeBuilder;
    friend bool buildBytecode(DeviceTypeCodeProgram& prog);
    Status runBytecode(RunContext& ctx) const;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A record's init code run as its init values function (DeviceTypeRecordInitFn, as the build generates from
// initCode): false if fewer bytes were read than the init steps before the first value slot or guard read, otherwise
// the code assigns init.<name> values. Each takes one of RAFT_RUNTIME_INIT_MAX slots, each with its own function
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef RAFT_RUNTIME_INIT_MAX
#define RAFT_RUNTIME_INIT_MAX 16
#endif

class DeviceTypeRuntimeInit
{
public:
    ~DeviceTypeRuntimeInit();

    /// @brief Create from a record's init code and init string
    /// @param pInitCode the init code
    /// @param pInitValues the record's initValues (the reads before the first slot or guard are required)
    /// @param problem (out) why not
    /// @param takeSlot false for one used only through run() (getInitFn() is then nullptr), which needs no slot
    /// @return the init values function's owner, or nullptr. The caller owns it
    static DeviceTypeRuntimeInit* create(const char* pInitCode, const char* pInitValues, String& problem, bool takeSlot = true);

    /// @brief The names the code computes, in order of first use (the function's value order)
    const std::vector<String>& getNames() const;

    /// @brief The names, comma separated (DeviceTypeRecord::initValueNames)
    const char* getNamesStr() const
    {
        return _namesStr.c_str();
    }

    DeviceTypeRecordInitFn getInitFn() const
    {
        return _initFn;
    }

    /// @brief Run (the DeviceTypeRecordInitFn contract)
    bool run(const uint8_t* pReadBuf, uint32_t readBufLen, int32_t* pValues, uint32_t numValues) const;

    static uint32_t getNumSlotsInUse();

private:
    DeviceTypeRuntimeInit() = default;
    DeviceTypeCodeProgram* _pProgram = nullptr;
    uint32_t _requiredReadBytes = 0;
    String _namesStr;
    int32_t _slot = -1;
    DeviceTypeRecordInitFn _initFn = nullptr;
};

