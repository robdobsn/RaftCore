/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CatalogueIndex.h
//
// Lookups derived from all device type records - base and extended - built at runtime. This replaces the
// tables the generator used to derive at build time, so records added at runtime are treated the same as base
// records. Built by DeviceTypeRecords when its generation changes; an index is never changed once built, so
// a reader holding one needs no lock.
//
// - Identification candidates for each address (records that aren't probe-only), in identification order:
//   descending identPriority, then extended records (in the order added) before base records (in catalogue
//   order).
// - Reserved addresses: addresses that a record identifies by address (used to keep re-addressable devices
//   off them).
// - Scan priority lists, from each record's scanPriority (as the generator built them).
// - Hardware type ID -> device type index (extended records first).
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <vector>
#include <algorithm>
#include "RaftUtils.h"
#include "DeviceTypeRecord.h"
#include "RaftDeviceConsts.h"

class CatalogueIndex
{
public:
    // Addresses covered (7-bit I2C addresses up to 0x77) and the range scanned by the last scan list
    static constexpr uint32_t MAX_ADDR = 0x77;
    static constexpr uint32_t MIN_SCAN_ADDR = 0x04;
    static constexpr uint32_t NUM_SCAN_PRIORITY_LEVELS = 3;

    /// @brief Build the index
    /// @param generation generation of the records it is built from
    /// @param numBase number of base records (indexes 0 .. numBase-1)
    /// @param numExtended number of extended records (indexes numBase ..)
    /// @param getRecord function returning the record for an index
    template <typename GetRecord>
    void build(uint32_t generation, uint32_t numBase, uint32_t numExtended, GetRecord getRecord)
    {
        _generation = generation;

        // Records in candidate order before identPriority is applied: extended (as added), then base
        std::vector<DeviceTypeIndexType> order;
        order.reserve(numBase + numExtended);
        for (uint32_t i = 0; i < numExtended; i++)
            order.push_back(numBase + i);
        for (uint32_t i = 0; i < numBase; i++)
            order.push_back(i);

        // Pass 1: the (address, record) pairs in candidate order, scan priority sets and hardware type IDs
        struct AddrRec
        {
            uint8_t addr;
            DeviceTypeIndexType idx;
        };
        std::vector<AddrRec> addrRecs;
        addrRecs.reserve(numBase + numExtended + 32);
        bool inScanSet[NUM_SCAN_PRIORITY_LEVELS][MAX_ADDR + 1] = {};
        uint16_t countByAddr[MAX_ADDR + 1] = {};
        bool anyIdentPriority = false;
        _hwTypeIds.clear();
        std::vector<int> addrList;
        for (DeviceTypeIndexType idx : order)
        {
            const DeviceTypeRecord* pRec = getRecord(idx);
            if (!pRec)
                continue;
            if (pRec->hasHwTypeId())
                _hwTypeIds.push_back({pRec->hwTypeId, idx});
            if (pRec->isProbeOnly() || !pRec->addresses || !pRec->addresses[0])
                continue;
            anyIdentPriority = anyIdentPriority || (pRec->identPriority != 0);
            parseAddresses(pRec->addresses, addrList);
            for (int addr : addrList)
            {
                addrRecs.push_back({(uint8_t)addr, idx});
                countByAddr[addr]++;
            }
            if (pRec->scanPriority != DeviceTypeRecord::SCAN_PRIORITY_NONE)
            {
                uint32_t level = pRec->scanPriority < NUM_SCAN_PRIORITY_LEVELS ? pRec->scanPriority : NUM_SCAN_PRIORITY_LEVELS - 1;
                for (uint32_t i = 0; i < addrList.size(); i++)
                {
                    // High priority: the first (primary) address in list 0, the others in list 1
                    uint32_t addrLevel = (level == 0) && (i > 0) ? 1 : level;
                    inScanSet[addrLevel][addrList[i]] = true;
                }
            }
        }

        // Pass 2: candidates in CSR form (a counting sort, which keeps the record order for each address)
        _candStart.assign(MAX_ADDR + 2, 0);
        for (uint32_t addr = 0; addr <= MAX_ADDR; addr++)
            _candStart[addr + 1] = _candStart[addr] + countByAddr[addr];
        _candidates.assign(addrRecs.size(), 0);
        std::vector<uint16_t> fillPos(_candStart.begin(), _candStart.end() - 1);
        for (const AddrRec& addrRec : addrRecs)
            _candidates[fillPos[addrRec.addr]++] = addrRec.idx;

        // Identification order: descending identPriority (a stable sort keeps the record order within a priority)
        if (anyIdentPriority)
        {
            for (uint32_t addr = 0; addr <= MAX_ADDR; addr++)
            {
                std::stable_sort(_candidates.begin() + _candStart[addr], _candidates.begin() + _candStart[addr + 1],
                        [&getRecord](DeviceTypeIndexType a, DeviceTypeIndexType b) {
                            return getRecord(a)->identPriority > getRecord(b)->identPriority;
                        });
            }
        }

        // Scan lists: each level's addresses not in a higher level, in address order. The last level holds every
        // scanned address not in a higher level
        _scanLists.assign(NUM_SCAN_PRIORITY_LEVELS, std::vector<uint8_t>());
        for (uint32_t level = 0; level < NUM_SCAN_PRIORITY_LEVELS; level++)
        {
            bool isLast = level == NUM_SCAN_PRIORITY_LEVELS - 1;
            for (uint32_t addr = isLast ? MIN_SCAN_ADDR : 0; addr <= MAX_ADDR; addr++)
            {
                if (!isLast && !inScanSet[level][addr])
                    continue;
                bool inHigher = false;
                for (uint32_t higher = 0; higher < level; higher++)
                    inHigher = inHigher || inScanSet[higher][addr];
                if (!inHigher)
                    _scanLists[level].push_back(addr);
            }
            _scanLists[level].shrink_to_fit();
        }

        // Hardware type IDs sorted by ID (stable, so extended records come first for the same ID)
        std::stable_sort(_hwTypeIds.begin(), _hwTypeIds.end(), [](const HwTypeIdEntry& a, const HwTypeIdEntry& b) {
            return a.hwTypeId < b.hwTypeId;
        });
        _hwTypeIds.shrink_to_fit();
    }

    /// @brief Generation of the records the index was built from
    uint32_t getGeneration() const
    {
        return _generation;
    }

    /// @brief Identification candidates for an address, in identification order
    /// @param addr address (without slot)
    /// @param count (out) number of candidates
    /// @return pointer to the candidates (valid while the index exists), or nullptr if there are none
    const DeviceTypeIndexType* getIdentCandidates(uint32_t addr, uint32_t& count) const
    {
        count = 0;
        if (addr > MAX_ADDR)
            return nullptr;
        count = _candStart[addr + 1] - _candStart[addr];
        return count > 0 ? &_candidates[_candStart[addr]] : nullptr;
    }

    /// @brief Check whether a record identifies a device at this address (re-addressable devices keep off it)
    bool isAddrReserved(uint32_t addr) const
    {
        uint32_t count = 0;
        getIdentCandidates(addr, count);
        return count > 0;
    }

    /// @brief Scan priority lists (list 0 is scanned most often)
    const std::vector<std::vector<uint8_t>>& getScanLists() const
    {
        return _scanLists;
    }

    /// @brief Find the record with a hardware type ID
    /// @param hwTypeId hardware type ID
    /// @param deviceTypeIndex (out) device type index
    /// @return true if found
    bool findByHwTypeId(uint16_t hwTypeId, DeviceTypeIndexType& deviceTypeIndex) const
    {
        auto it = std::lower_bound(_hwTypeIds.begin(), _hwTypeIds.end(), hwTypeId,
                [](const HwTypeIdEntry& entry, uint16_t id) { return entry.hwTypeId < id; });
        if ((it == _hwTypeIds.end()) || (it->hwTypeId != hwTypeId))
            return false;
        deviceTypeIndex = it->deviceTypeIndex;
        return true;
    }

    /// @brief Approximate bytes used by the index
    uint32_t getBytesUsed() const
    {
        uint32_t bytes = sizeof(*this) + _candStart.capacity() * sizeof(uint16_t) +
                _candidates.capacity() * sizeof(DeviceTypeIndexType) + _hwTypeIds.capacity() * sizeof(HwTypeIdEntry);
        for (const auto& list : _scanLists)
            bytes += sizeof(list) + list.capacity();
        return bytes;
    }

    /// @brief Parse a record's address string ("0x10", "0x10-0x17", "0x48,0x49") into addresses up to MAX_ADDR
    static void parseAddresses(const char* pAddresses, std::vector<int>& addrList)
    {
        std::vector<int> parsed;
        Raft::parseIntList(pAddresses, parsed, ",", "-", MAX_ADDR + 1);
        addrList.clear();
        for (int addr : parsed)
        {
            if ((addr >= 0) && (addr <= (int)MAX_ADDR))
                addrList.push_back(addr);
        }
    }

private:
    uint32_t _generation = 0;

    // Candidates in CSR form: the candidates for address a are _candidates[_candStart[a] .. _candStart[a+1])
    std::vector<uint16_t> _candStart;
    std::vector<DeviceTypeIndexType> _candidates;

    // Scan priority lists
    std::vector<std::vector<uint8_t>> _scanLists;

    // Hardware type ID -> device type index, sorted by ID
    struct HwTypeIdEntry
    {
        uint16_t hwTypeId;
        DeviceTypeIndexType deviceTypeIndex;
    };
    std::vector<HwTypeIdEntry> _hwTypeIds;
};
