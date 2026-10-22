/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DeviceTypeDecodeVerifier.h
//
// A debug check of the runtime decoders on a device (RAFT_RUNTIME_DECODE_VERIFY, set in features.cmake): every poll
// result of a device whose type has a compiled decoder is decoded again by a runtime decoder made from the record's
// JSON (DeviceTypeRuntimeDecoder - with the code interpreter for custom decode code), and the two results are
// compared byte for byte. Likewise the compiled init values function of a record with initCode is compared with its
// init code run by the interpreter (DeviceTypeRuntimeInit), on the bytes the device's init steps read. The decodes
// run where the poll results are delivered (the bus task), as a subscriber's decode does; the runtime decoders and
// init functions are made in loop() (DeviceManager's), as making them needs more stack than running them.
//
// Results per type (polls, records, mismatches with the first one's data, decode times, init comparisons) and the
// least stack left in a task that verified: devman/decodeverify (reset=1 clears them).
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <vector>
#include "RaftArduino.h"
#include "RaftThreading.h"
#include "RaftDeviceConsts.h"
#include "DeviceTypeRecord.h"

class DeviceTypeRuntimeDecoder;
class DeviceTypeRuntimeInit;
class DeviceTypeRecords;

class DeviceTypeDecodeVerifier
{
public:
    DeviceTypeDecodeVerifier();
    ~DeviceTypeDecodeVerifier();

    /// @brief Check a poll result: decode it with the record's compiled decoder and a runtime decoder and compare
    /// @param deviceID the device
    /// @param deviceTypeIndex its type
    /// @param pRec its record (one with a compiled decoder; others are counted as not compared)
    /// @param isCompiled whether the record's decoder is the compiled one
    /// @param data the poll result (as delivered to subscribers)
    void checkDecode(RaftDeviceID deviceID, uint16_t deviceTypeIndex, const DeviceTypeRecord* pRec, bool isCompiled,
                const std::vector<uint8_t>& data);

    /// @brief Check a record's compiled init values function against its init code
    /// @param deviceTypeIndex its type
    /// @param pRec its record
    /// @param pInitCode the record's init code (the build's copy)
    /// @param readData the bytes the init steps read
    /// @param compiledOk what the compiled function returned
    /// @param compiledValues the values it computed
    void checkInit(uint16_t deviceTypeIndex, const DeviceTypeRecord* pRec, const char* pInitCode,
                const std::vector<uint8_t>& readData, bool compiledOk, const std::vector<int32_t>& compiledValues);

    /// @brief Make the runtime decoders and init functions to compare (call from a task with room on its stack - making
    ///        them parses JSON and compiles code; a poll result that arrives before its type's decoder is made isn't
    ///        compared, and is counted as not ready)
    /// @param table the device type table (the compiled records' init code)
    void loop(const DeviceTypeRecords& table);

    /// @brief Results as JSON fields (without braces)
    String getJson() const;

    /// @brief Clear the results (the runtime decoders and init functions are kept)
    void reset();

    // Output records per decode (each decoder is given room for this many)
    static constexpr uint32_t MAX_RECS_PER_DECODE = 64;

    // Stack the verification needs where it is called (the two decodes, the interpreter's frames for the deepest
    // record's code, a warning log, and a margin): with less free, a poll result isn't verified and is counted as skipped
    static constexpr uint32_t STACK_NEEDED_BYTES = 3072;

private:
    // Per type
    struct TypeStats
    {
        uint16_t deviceTypeIndex = 0;
        String typeKey;
        DeviceTypeRecordDecodeFn compiledFn = nullptr;      // the decoder the runtime one was made beside
        DeviceTypeRuntimeDecoder* pDecoder = nullptr;
        const DeviceTypeRecord* pPendingRec = nullptr;      // a runtime decoder to make (in loop())
        uint32_t notReady = 0;                              // poll results before the runtime decoder was made
        String problem;                                     // why there is no runtime decoder, or a layout difference
        bool notCompiled = false;                           // the record's decoder isn't the compiled one
        uint32_t polls = 0;
        uint32_t records = 0;
        uint32_t mismatches = 0;
        uint64_t compiledUsTotal = 0;
        uint64_t runtimeUsTotal = 0;
        uint32_t compiledUsMax = 0;
        uint32_t runtimeUsMax = 0;
        uint64_t nodes = 0;                                 // interpreter steps (custom decode code)
        uint64_t compiledWarmUsTotal = 0;                   // the decodes run again straight away (warm caches)
        uint64_t runtimeWarmUsTotal = 0;
        uint64_t warmNodesSkip = 0;
        String firstMismatch;                               // JSON object
        // Init
        DeviceTypeRuntimeInit* pInit = nullptr;
        String initProblem;
        uint32_t initRuns = 0;
        uint32_t initMismatches = 0;
        uint32_t skippedLowStack = 0;
        uint32_t initSkippedLowStack = 0;
        uint32_t initNotReady = 0;
        String initFirstMismatch;
    };
    std::vector<TypeStats> _types;

    // Per device: the decode states (the decoders keep timestamps in them)
    struct DeviceState
    {
        RaftDeviceID deviceID;
        uint16_t deviceTypeIndex = 0;
        RaftBusDeviceDecodeState compiled;
        RaftBusDeviceDecodeState runtime;
    };
    std::vector<DeviceState> _devices;

    // Output buffers
    std::vector<uint8_t> _outCompiled;
    std::vector<uint8_t> _outRuntime;

    // Least stack left in a task after verifying (its high water mark), and least free where the verifier was called
    uint32_t _stackMinFree = UINT32_MAX;
    String _stackMinTask;
    uint32_t _stackAtCallMinFree = UINT32_MAX;
    String _stackAtCallTask;

    bool _initsMade = false;

    mutable RaftMutex _mutex;

    TypeStats& getTypeStats(uint16_t deviceTypeIndex, const DeviceTypeRecord* pRec);
    void noteStack();
    void noteStackAtCall(uint32_t freeBytes);
    static String describeMismatch(const DeviceTypeRecord* pRec, const std::vector<uint8_t>& data, uint32_t nCompiled,
                uint32_t nRuntime, const uint8_t* pCompiled, const uint8_t* pRuntime, uint32_t structSize,
                const RaftBusDeviceDecodeState& stateCompiled, const RaftBusDeviceDecodeState& stateRuntime);
};

#ifdef RAFT_RUNTIME_DECODE_VERIFY
extern DeviceTypeDecodeVerifier deviceTypeDecodeVerifier;
#endif
