/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Type Records
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "DeviceTypeRecord.h"
#include "BusAddrStatus.h"  // For DeviceOnlineState
#include "DeviceTypeRecordDynamic.h"
#include "DevicePollingInfo.h"
#include "RaftThreading.h"
#include "CatalogueIndex.h"
#include "DeviceTypeGrammar.h"
#include "DeviceTypeRuntimeDecoder.h"
#include <atomic>
#include <memory>
#include <string>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @class DeviceTypeRecords
/// @brief Device type records
class DeviceTypeRecords
{
public:
    DeviceTypeRecords();
    ~DeviceTypeRecords();

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type for address
    /// @param addr device address
    /// @returns device type indexes that match the address
    /// @note the records that identify a device at the address by address - kept for existing callers (e.g. RSAO
    ///       address reservation). Use getIdentCandidates() for identification and isAddrReserved() for
    ///       reservation
    std::vector<DeviceTypeIndexType> getDeviceTypeIdxsForAddr(BusElemAddrType addr) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get the catalogue index (lookups derived from all records), rebuilt if records have been added
    /// @return the index - it doesn't change, so it can be used without a lock for as long as it is held
    std::shared_ptr<const CatalogueIndex> getCatalogueIndex() const;

    /// @brief Generation of the records: increases whenever a record is added (or later replaced or its policy
    ///        changed). A caller that keeps something derived from the records rebuilds it when this changes
    uint32_t getGeneration() const
    {
        return _generation.load(std::memory_order_acquire);
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Start a bulk update (e.g. loading a device type file): the generation increases once, at the end
    ///        (endBulkUpdate), however many records are added or changed - a bus scanner rebuilds its lists on each
    ///        increase. Lookups (getCatalogueIndex etc.) see each change as it is made. Nests
    void beginBulkUpdate()
    {
        _bulkDepth.fetch_add(1, std::memory_order_acq_rel);
    }

    /// @brief End a bulk update: the generation increases once if anything changed
    void endBulkUpdate()
    {
        if ((_bulkDepth.fetch_sub(1, std::memory_order_acq_rel) == 1) && _bulkChanged.exchange(false))
            _generation.fetch_add(1, std::memory_order_acq_rel);
    }

    /// @brief A bulk update for the life of the object
    class BulkUpdate
    {
    public:
        explicit BulkUpdate(DeviceTypeRecords& table) : _table(table)
        {
            _table.beginBulkUpdate();
        }
        ~BulkUpdate()
        {
            _table.endBulkUpdate();
        }
        BulkUpdate(const BulkUpdate&) = delete;
        BulkUpdate& operator=(const BulkUpdate&) = delete;
    private:
        DeviceTypeRecords& _table;
    };

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Identification candidates for an address, in the order to try them
    /// @param addr address (without slot)
    /// @return device type indexes
    std::vector<DeviceTypeIndexType> getIdentCandidates(BusElemAddrType addr) const;

    /// @brief Records known at an address (records that identify a device there by address)
    std::vector<DeviceTypeIndexType> getKnownTypesAtAddr(BusElemAddrType addr) const;

    /// @brief Check whether a record identifies a device at this address (re-addressable devices keep off it)
    bool isAddrReserved(BusElemAddrType addr) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get the record with a hardware type ID (the ID a device reports when probed by an identification
    ///        hook, JSON "deviceTypeId")
    /// @param hwTypeId hardware type ID
    /// @param deviceTypeIndex (out) device type index (DEVICE_TYPE_INDEX_INVALID if not found)
    /// @return the record, or nullptr if there is none
    const DeviceTypeRecord* getRecordByHwTypeId(uint16_t hwTypeId, DeviceTypeIndexType& deviceTypeIndex) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get the device type record for a device type index (no copy, no lock)
    /// @param deviceTypeIndex device type index
    /// @return the record (in flash for a base record), or nullptr if there is none. Records are never changed or
    ///         freed while the firmware runs, so the pointer stays valid
    const DeviceTypeRecord* getRecord(DeviceTypeIndexType deviceTypeIndex) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get the device type record for a device type key (an extended record with the key takes
    ///        precedence over a base record with the same key)
    /// @param deviceTypeKey device type key
    /// @param deviceTypeIndex (out) device type index (DEVICE_TYPE_INDEX_INVALID if not found)
    /// @return the record, or nullptr if there is none
    const DeviceTypeRecord* getRecord(const char* deviceTypeKey, DeviceTypeIndexType& deviceTypeIndex) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type record for a device type index
    /// @param deviceTypeIndex device type index
    /// @param devTypeRec (out) device type record (a copy - its strings point into the record, which stays valid)
    /// @return true if device type found
    bool getDeviceInfo(DeviceTypeIndexType deviceTypeIndex, DeviceTypeRecord& devTypeRec) const;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type record for a device type key
    /// @param deviceTypeKey device type key
    /// @param devTypeRec (out) device type record
    /// @param deviceTypeIndex (out) device type index
    /// @return true if device type found
    bool getDeviceInfo(const String& deviceTypeKey, DeviceTypeRecord& devTypeRec, DeviceTypeIndexType& deviceTypeIndex) const;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device polling info
    /// @param addr address
    /// @param pDevTypeRec device type record
    /// @param pollRequests (out) polling info
    void getPollInfo(BusElemAddrType addrAndSlot, const DeviceTypeRecord* pDevTypeRec, DevicePollingInfo& pollingInfo) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type info JSON by device type index
    /// @param deviceTypeIndex device type index
    /// @param includePlugAndPlayInfo include plug and play info
    /// @return JSON string
    String getDevTypeInfoJsonByTypeIdx(DeviceTypeIndexType deviceTypeIndex, bool includePlugAndPlayInfo) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type info JSON by device type key
    /// @param deviceTypeKey device type key
    /// @param includePlugAndPlayInfo include plug and play info
    /// @param deviceTypeIndex (out) device type index
    /// @return JSON string
    String getDevTypeInfoJsonByTypeName(const String& deviceTypeKey, bool includePlugAndPlayInfo, DeviceTypeIndexType& deviceTypeIndex) const;

    // Device detection record
    class DeviceDetectionRec
    {
    public:
        std::vector<uint8_t> writeData;
        // First value is a mask and second value is the expected value to check against
        // Result is true if any of the pairs match the read values
        std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> checkValues;
        uint16_t pauseAfterSendMs = 0;
        // Bytes to read (the length of the first check value; 0 for a step that only writes or pauses)
        uint16_t readLen = 0;
        // Each 2-byte word read must be followed by a valid Sensirion CRC-8 (":crc8w")
        bool crc8w = false;
    };

    // A record's command strings, compiled (see DeviceTypeGrammar)
    struct CompiledRecord
    {
        DeviceTypeGrammar::Result detection;
        DeviceTypeGrammar::Result init;
        DeviceTypeGrammar::Result poll;
    };

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get a record's compiled command strings (compiled when first asked for, then kept - an index always
    ///        means the same record)
    /// @param deviceTypeIndex device type index
    /// @return compiled record, or nullptr if there is no such record
    std::shared_ptr<const CompiledRecord> getCompiledRecord(DeviceTypeIndexType deviceTypeIndex) const;

    /// @brief Get detection records for a device type index (from the compiled record)
    /// @return false if the detection string has an error (the record must not match)
    bool getDetectionRecs(DeviceTypeIndexType deviceTypeIndex, std::vector<DeviceDetectionRec>& detectionRecs) const;

    /// @brief Get initialisation bus requests for a device type index (from the compiled record)
    void getInitBusRequests(BusElemAddrType addr, DeviceTypeIndexType deviceTypeIndex, std::vector<BusRequestInfo>& initRequests);

    // Init values for one device's init (kept across the init's resumable pauses): the bytes the init steps read before
    // the first step with a value slot or guard, and the values computed from them (devdocs/computed-init-values.md)
    struct InitValueState
    {
        std::vector<uint8_t> readData;
        std::vector<int32_t> values;
        bool computed = false;
    };

    /// @brief Note the bytes an init step read (kept for the init values until they are computed)
    static void noteInitStepRead(InitValueState& state, const std::vector<uint8_t>& readData)
    {
        if (!state.computed)
            state.readData.insert(state.readData.end(), readData.begin(), readData.end());
    }

    /// @brief Check whether an init step uses init values: value slots ({name} placeholders) or a guard ({?name})
    bool initStepUsesInitValues(DeviceTypeIndexType deviceTypeIndex, uint32_t stepIdx) const;

    /// @brief Get an init step's write data with its value slots filled in, and whether its guard lets it run: the
    ///        values are computed (once) by the record's init values function from the bytes read so far; each slot is
    ///        written as one byte (0 to 255), and a guarded step is only done if its value isn't 0
    /// @param deviceTypeIndex device type index
    /// @param stepIdx init step index
    /// @param state init value state (values computed into it on first use)
    /// @param writeData (out) the step's write data
    /// @param doStep (out) false if the step's guard is 0 (the step is skipped: nothing written or read, no pause)
    /// @return false if the values can't be computed (no function, an unknown name, or too few bytes read)
    bool getInitStepWrite(DeviceTypeIndexType deviceTypeIndex, uint32_t stepIdx, InitValueState& state,
                std::vector<uint8_t>& writeData, bool& doStep) const;

    /// @brief Get polling info for a device type index (from the compiled record)
    void getPollInfo(BusElemAddrType addrAndSlot, DeviceTypeIndexType deviceTypeIndex, DevicePollingInfo& pollingInfo) const;

    /// @brief Compile a record's command strings and log any problems
    /// @param pDevTypeRec record
    /// @param logWarnings log warnings as well as errors
    static std::shared_ptr<CompiledRecord> compileRecord(const DeviceTypeRecord* pDevTypeRec, bool logWarnings);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get detection records
    /// @param pDevTypeRec device type record
    /// @param detectionRecs (out) detection records
    /// @return false if any detection value is invalid (detectionRecs is then empty and the record must not match)
    bool getDetectionRecs(const DeviceTypeRecord* pDevTypeRec, std::vector<DeviceDetectionRec>& detectionRecs);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get initialisation bus requests
    /// @param addr address
    /// @param pDevTypeRec device type record
    /// @param initBusRequests (out) initialisation bus requests
    void getInitBusRequests(BusElemAddrType addr, const DeviceTypeRecord* pDevTypeRec, std::vector<BusRequestInfo>& initRequests);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Convert poll response to JSON
    /// @param addr address
    /// @param onlineState device online state
    /// @param deviceTypeIndex device type index
    /// @param devicePollResponseData device poll response data
    static String deviceStatusToJson(BusElemAddrType addr, DeviceOnlineState onlineState, uint16_t deviceTypeIndex,
            const std::vector<uint8_t>& devicePollResponseData);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get scan priority lists
    /// @param priorityLists (out) priority lists
    void getScanPriorityLists(std::vector<std::vector<BusElemAddrType>>& priorityLists);

    // Result of adding an extended record
    enum class AddResult
    {
        Added,              // added with a new index
        AlreadyPresent,     // a record with the same key and the same content is present - its index is returned
        Conflict,           // a record with the same key and different content is present - not added (D17)
        Full,               // the configured number of records or bytes would be exceeded, or out of memory
        Invalid,            // the record has no key
        Replaced,           // a base record with the same key was replaced, keeping its index (only before Ready)
        Protected,          // the key is protected (a system device expects that type) - not added
    };
    static const char* getAddResultStr(AddResult result);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Add an extended device type record
    /// @param devTypeRec device type record
    /// @param deviceTypeIndex (out) device type index (DEVICE_TYPE_INDEX_INVALID unless Added, AlreadyPresent or
    ///        Replaced)
    /// @return result
    /// @note a record with the key of a base record replaces that base record (keeping its index) if the table isn't
    ///       Ready yet; after that it gets a new index, and the key then means the new record
    AddResult addRecord(const DeviceTypeRecordDynamic& devTypeRec, DeviceTypeIndexType& deviceTypeIndex);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Add extended device type records
    /// @param devTypeRec device type record
    /// @param deviceTypeIndex (out) device type index (DEVICE_TYPE_INDEX_INVALID if not added or present)
    /// @return true if added (false if already present - deviceTypeIndex is then its index - or not added)
    /// @note see addRecord() for the reason a record is not added
    bool addExtendedDeviceTypeRecord(const DeviceTypeRecordDynamic& devTypeRec, uint16_t& deviceTypeIndex);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set the limits for extended records (only before the first extended record is added)
    /// @param maxRecords maximum number of extended records (0 for the default)
    /// @param maxBytes maximum bytes used by extended records (0 for the default)
    /// @return true if set (false if records have already been added)
    /// @note defaults: 256 records and 256 KB on boards with PSRAM, 32 records and 20 KB without
    bool setExtendedRecordLimits(uint32_t maxRecords, uint32_t maxBytes);

    /// @brief Mark a table that is only checked, never polled (a type file check's): its runtime decoders are made
    ///        without decode functions, so they take no slots (RAFT_RUNTIME_DECODE_MAX_DECODERS) from the device's
    ///        own table; getDecoderKind still reports them. Call before adding records
    void setCheckOnly()
    {
        _checkOnly = true;
    }

    // Extended record usage
    struct ExtendedRecordStats
    {
        uint32_t count = 0;
        uint32_t maxRecords = 0;
        uint32_t bytesUsed = 0;
        uint32_t maxBytes = 0;
    };
    ExtendedRecordStats getExtendedRecordStats() const;

    /// @brief Number of base records
    static uint32_t getNumBaseRecords();

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set a record's policy (see DeviceTypeRecord::POLICY_...) - affects identification only
    /// @param deviceTypeIndex device type index
    /// @param policy policy
    /// @return Set, NotFound or Protected (a protected type can't be disabled)
    /// @note a change applies to devices identified from then on (devices already identified keep their type
    ///       until they are identified again)
    enum class PolicyResult
    {
        Set,
        NotFound,
        Protected,
    };
    PolicyResult setPolicy(DeviceTypeIndexType deviceTypeIndex, uint8_t policy);
    PolicyResult setPolicy(const char* deviceTypeKey, uint8_t policy);
    static const char* getPolicyResultStr(PolicyResult result);

    /// @brief Get a record's policy
    uint8_t getPolicy(DeviceTypeIndexType deviceTypeIndex) const;

    /// @brief Set a type's scan priority in place of its record's (a board or product choice - DevMan
    ///        "typeSettings"): 0 high, 1 medium, 2 low, or DeviceTypeRecord::SCAN_PRIORITY_NONE
    /// @return false if there is no record with the key, or the priority isn't valid
    bool setScanPriority(const char* deviceTypeKey, uint8_t scanPriority);

    /// @brief Get a type's scan priority (the one set for the type, else its record's)
    uint8_t getScanPriority(DeviceTypeIndexType deviceTypeIndex) const;

    /// @brief Protect a device type key: it can't be disabled, overridden or replaced (a system device expects it)
    void setProtected(const char* deviceTypeKey);
    bool isProtected(const char* deviceTypeKey) const;
    std::vector<String> getProtectedKeys() const;

    /// @brief Where a record comes from
    enum class RecordSource
    {
        None,
        Base,           // compiled in
        Override,       // replaces a base record, at its index
        Extended,       // added at runtime with its own index
    };
    RecordSource getRecordSource(DeviceTypeIndexType deviceTypeIndex) const;
    static const char* getRecordSourceStr(RecordSource source);

    /// @brief Ready: identification has started, so an index may have been given to a device. From then on a
    ///        record can't replace a base record at its index (D2: an index never changes meaning)
    void markReady()
    {
        _ready = true;
    }
    bool isReady() const
    {
        return _ready;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Register a source of records (e.g. the device type file) that must supply its records before devices
    ///        are identified. Buses scan for presence meanwhile, and identify once every registered source is ready
    /// @param pName name (for logging and status)
    /// @param timeoutMs time after which identification goes ahead without this source (logged)
    /// @return source ID for markRecordSourceReady()
    uint32_t registerRecordSource(const char* pName, uint32_t timeoutMs = RECORD_SOURCE_TIMEOUT_MS_DEFAULT);

    /// @brief Mark a record source as having supplied its records
    void markRecordSourceReady(uint32_t sourceId);

    /// @brief Check whether devices can be identified: every registered record source is ready (or timed out)
    bool areRecordSourcesReady() const;

    static constexpr uint32_t RECORD_SOURCE_TIMEOUT_MS_DEFAULT = 10000;

    /// @brief Note that firmware code supplies the data for a device type whose record has pollInfo.ext (such a
    ///        device is not polled: the code puts its samples in as poll results, e.g. Axiom's VL53LxCX SysMod).
    ///        A record from the type file with pollInfo.ext for a type no code supplies is reported, as it would
    ///        never have data. Call from setup() (the type file is loaded in DeviceManager's postSetup)
    /// @param deviceTypeKey device type key
    void registerExtDataSource(const char* deviceTypeKey);

    /// @brief Check whether firmware code supplies the data for a device type (registerExtDataSource)
    bool hasExtDataSource(const char* deviceTypeKey) const;

    /// @brief The device types whose data firmware code supplies
    std::vector<String> getExtDataSources() const;

    /// @brief Check whether the identification hook must not be run at an address (a record there has the policy
    ///        DISABLED_CLAIM)
    bool isAddrHookBlocked(BusElemAddrType addr) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device poll decode function
    /// @param deviceTypeIndex device type index
    /// @return poll decode function
    DeviceTypeRecordDecodeFn getPollDecodeFn(uint16_t deviceTypeIndex) const;

    // Where a record's poll decoder comes from: compiled into the firmware (generated, or inherited by an override
    // with the same response layout), made at runtime from its attribute list (RAFT_RUNTIME_DECODE), or none
    enum class DecoderKind : uint8_t
    {
        None,
        Compiled,
        Runtime,
    };

    /// @brief Get where a record's poll decoder comes from
    DecoderKind getDecoderKind(DeviceTypeIndexType deviceTypeIndex) const;

    /// @brief Name of a decoder kind ("compiled", "runtime" or "none")
    static const char* getDecoderKindStr(DecoderKind kind);

#ifdef RAFT_RUNTIME_DECODE_VERIFY
    /// @brief The init code of a compiled record whose init values function is still the compiled one, or nullptr
    ///        (the decode verifier, DeviceTypeDecodeVerifier.h)
    const char* getBaseInitCode(DeviceTypeIndexType deviceTypeIndex) const;
#endif

private:
    // Extended records are added at runtime. Each is one packed allocation (DeviceTypeRecordPacked) that never
    // changes or moves, so pointers to it stay valid. _extRecords is a fixed array of pointers (allocated when
    // the first record is added, sized by the limits); a record is written to it before _extCount is
    // incremented (release), so reading a record by index needs no lock. Adding and key lookups take the mutex
    mutable RaftMutex _extRecordsMutex;
    const DeviceTypeRecord** _extRecords = nullptr;
    std::atomic<uint32_t> _extCount{0};
    uint32_t _extMaxRecords = 0;
    uint32_t _extMaxBytes = 0;
    uint32_t _extBytesUsed = 0;

    // Positions of the extended records sorted by key (for binary search), maintained under the mutex
    std::vector<uint16_t> _extPosByKey;

    // Generation (incremented when a record is added - once for a bulk update) and the content version
    // (incremented on every change, also within a bulk update), which the catalogue index is built for (replaced,
    // never changed, when rebuilt; held under the mutex)
    std::atomic<uint32_t> _generation{1};
    std::atomic<uint32_t> _contentVersion{1};
    std::atomic<uint32_t> _bulkDepth{0};
    std::atomic<bool> _bulkChanged{false};
    void noteChange()
    {
        _contentVersion.fetch_add(1, std::memory_order_acq_rel);
        if (_bulkDepth.load(std::memory_order_acquire) > 0)
            _bulkChanged.store(true);
        else
            _generation.fetch_add(1, std::memory_order_acq_rel);
    }
    mutable std::shared_ptr<const CatalogueIndex> _catalogueIndex;

    // Policies set at runtime, by device type index (POLICY_UNSET: the record's initial policy), under the mutex
    static constexpr uint8_t POLICY_UNSET = 0xff;
    std::vector<uint8_t> _policies;
    uint8_t policyOf(DeviceTypeIndexType deviceTypeIndex) const;

    // Scan priorities set for types, by device type index (SCAN_PRIORITY_UNSET: the record's), under the mutex
    static constexpr uint8_t SCAN_PRIORITY_UNSET = 0xfe;
    std::vector<uint8_t> _scanPriorities;
    uint8_t scanPriorityOf(DeviceTypeIndexType deviceTypeIndex) const;

    // Protected device type keys, under the mutex
    std::vector<std::string> _protectedKeys;
    bool isProtectedLocked(const char* deviceTypeKey) const;

    // Base records replaced by an extended record (before Ready): an array of numBase pointers, allocated when the
    // first is replaced (nullptr: not replaced). Read without a lock, like _extRecords. A replaced override is kept
    // (in _retiredRecords) until the table is destroyed, so a pointer to it stays valid
    std::atomic<const DeviceTypeRecord* const*> _baseOverrides{nullptr};
    const DeviceTypeRecord** _baseOverridesStore = nullptr;
    std::vector<const DeviceTypeRecord*> _retiredRecords;

    // Runtime decoders made for records with no compiled decoder (RAFT_RUNTIME_DECODE), owned here, under the mutex;
    // in a table only checked, made without decode functions, for the records in _checkOnlyDecoderRecs
    std::vector<DeviceTypeRuntimeDecoder*> _runtimeDecoders;
    std::vector<const DeviceTypeRecord*> _checkOnlyDecoderRecs;
    bool _checkOnly = false;
    void attachRuntimeDecoderLocked(DeviceTypeRecord* pRec);

    // Runtime init values functions from records' init code (RAFT_RUNTIME_DECODE_CODE), owned here, under the mutex
    std::vector<class DeviceTypeRuntimeInit*> _runtimeInits;
    void attachRuntimeInitLocked(DeviceTypeRecord* pRec, const String& initCode);
    std::atomic<bool> _ready{false};
    AddResult replaceBaseRecord(DeviceTypeIndexType baseIdx, const DeviceTypeRecordDynamic& devTypeRec);
    static bool sameResponseLayout(const char* pDevInfoA, const char* pDevInfoB);
    static int32_t findBaseIdxByKey(const char* deviceTypeKey);

    // Record sources that must supply their records before identification (under the mutex). _sourcesReady is
    // the fast path: true when there are none outstanding
    struct RecordSource_
    {
        std::string name;
        uint32_t registeredMs = 0;
        uint32_t timeoutMs = 0;
        bool ready = false;
    };
    mutable std::vector<RecordSource_> _recordSources;

    // Device type keys whose data firmware code supplies (registerExtDataSource), under the mutex
    std::vector<String> _extDataSources;
    mutable std::atomic<bool> _sourcesReady{true};

    // Default limits (D3)
    static constexpr uint32_t EXT_MAX_RECORDS_DEFAULT = 32;
    static constexpr uint32_t EXT_MAX_BYTES_DEFAULT = 20 * 1024;     // holds 32 typical records (538 B each, measured)
    static constexpr uint32_t EXT_MAX_RECORDS_DEFAULT_PSRAM = 256;
    static constexpr uint32_t EXT_MAX_BYTES_DEFAULT_PSRAM = 256 * 1024;

    // Helpers for extended records (assume the mutex is held)
    void applyDefaultExtendedRecordLimits();
    int32_t findExtendedPosByKey(const char* deviceTypeKey) const;

    // Compiled records by device type index (see getCompiledRecord)
    mutable RaftMutex _compiledMutex;
    mutable std::vector<std::shared_ptr<const CompiledRecord>> _compiledRecords;

    // Build bus requests and polling info from compiled steps
    static void detectionRecsFromSteps(const DeviceTypeGrammar::Result& compiled, std::vector<DeviceDetectionRec>& detectionRecs);
    void initRequestsFromSteps(BusElemAddrType addr, const DeviceTypeGrammar::Result& compiled,
                std::vector<BusRequestInfo>& initRequests);
    static void pollInfoFromSteps(BusElemAddrType addr, const DeviceTypeRecord* pDevTypeRec,
                const DeviceTypeGrammar::Result& compiled, DevicePollingInfo& pollingInfo);

    // Helpers
    static bool extractBufferDataFromHexStr(const String& writeStr, std::vector<uint8_t>& writeData);

    // Debug
    static constexpr const char* MODULE_PREFIX = "DevTypeRecs";
};

// Access to single instance
extern DeviceTypeRecords deviceTypeRecords;
