/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BusScanSchedule.h
//
// The order in which a bus scanner probes addresses (Stage 9 of the device identification plan).
//
// Priority lists: addresses a device type record (or an identification hook, or a multiplexer) can claim, in
// priority order. Each list is scanned on the main bus and every multiplexer slot in a cycle of slot visits: the
// main bus has weight MAIN_BUS_WEIGHT (2) and each slot a weight set by the scanner (1, or more for a slot with
// devices or a recent change), the visits spread through the cycle; every slot is in every cycle. The first list is scanned a whole slot position at a time;
// lower lists in chunks of a few addresses, so that the first list comes round again after at most one chunk of
// a lower list. The lists take turns by weight - the first every turn, the second every LIST_WEIGHTS[1] turns and
// so on - and each list keeps its own position.
//
// Background: addresses nothing can claim (a device there can't be identified) are probed slowly, spread evenly
// over a sweep period, on the main bus and every slot.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <vector>

class BusScanSchedule
{
public:
    static constexpr uint32_t LIST_WEIGHTS[] = { 1, 3, 9 };
    static constexpr uint32_t MAIN_BUS_WEIGHT = 2;

    BusScanSchedule()
    {
        rebuildSlotSeq();
    }

    /// @brief Set the priority lists (the position in a list is kept where the list is unchanged in size)
    void setLists(const std::vector<std::vector<uint8_t>>& lists)
    {
        bool sameShape = (lists.size() == _lists.size());
        for (uint32_t i = 0; sameShape && (i < lists.size()); i++)
            sameShape = lists[i].size() == _lists[i].size();
        _lists = lists;
        if (!sameShape)
        {
            _cursors.assign(_lists.size(), Cursor());
            _cur = 0;
            _chunkLeft = 0;
        }
    }

    /// @brief Set the number of multiplexer slots (each weighted 1 until setSlotWeights)
    void setNumSlots(uint32_t numSlots)
    {
        if (numSlots == _numSlots)
            return;
        _numSlots = numSlots;
        _slotWeights.assign(numSlots, 1);
        rebuildSlotSeq();
        for (Cursor& cursor : _cursors)
            cursor.slotSeqIdx = 0;
        _bgPos = 0;
    }

    /// @brief Set how often each multiplexer slot is scanned relative to the others (1 or more - e.g. more for a
    ///        slot with devices or a recent change). The main bus has weight MAIN_BUS_WEIGHT. Every slot is in every
    ///        cycle, so the cycle - the time for a list to cover every slot - is at most the sum of the weights
    /// @param weights one per slot (ignored if the count differs from setNumSlots)
    void setSlotWeights(const std::vector<uint8_t>& weights)
    {
        if ((weights.size() != _numSlots) || (weights == _slotWeights))
            return;
        _slotWeights = weights;
        rebuildSlotSeq();
        for (Cursor& cursor : _cursors)
            if (cursor.slotSeqIdx >= _slotSeq.size())
                cursor.slotSeqIdx = 0;
    }

    /// @brief Set the background addresses and the time for a full background sweep (main bus and every slot)
    void setBackground(const std::vector<uint8_t>& addrs, uint32_t sweepPeriodMs)
    {
        if (addrs.size() != _bgAddrs.size())
            _bgIdx = 0;
        _bgAddrs = addrs;
        _bgSweepPeriodMs = sweepPeriodMs;
    }

    /// @brief Restart from the start of every list
    void restart()
    {
        _cursors.assign(_lists.size(), Cursor());
        _cur = 0;
        _chunkLeft = 0;
    }

    /// @brief Get the next address to probe from the priority lists
    /// @param addr (out) address
    /// @param slotPos (out) 0 for the main bus, 1 + n for the nth multiplexer slot
    /// @param sweepCompleted (out) set true when a list has been scanned at every slot position
    /// @return false if the lists are empty
    bool next(uint32_t& addr, uint32_t& slotPos, bool& sweepCompleted)
    {
        if ((_chunkLeft == 0) || (_cur >= _lists.size()) || _lists[_cur].empty())
        {
            if (!selectNextList())
                return false;
        }
        Cursor& cursor = _cursors[_cur];
        addr = _lists[_cur][cursor.addrIdx];
        slotPos = _slotSeq[cursor.slotSeqIdx];
        cursor.addrIdx++;
        if (_chunkLeft > 0)
            _chunkLeft--;
        if (cursor.addrIdx >= _lists[_cur].size())
        {
            // A slot position done - lists switch turns at a slot boundary
            cursor.addrIdx = 0;
            cursor.slotSeqIdx++;
            _chunkLeft = 0;
            if (cursor.slotSeqIdx >= _slotSeq.size())
            {
                cursor.slotSeqIdx = 0;
                sweepCompleted = true;
            }
        }
        return true;
    }

    /// @brief Get the next background address to probe, if one is due
    /// @param timeNowMs time now
    /// @param addr (out) address
    /// @param bgSlotPos (out) 0 for the main bus, 1 + n for the nth multiplexer slot
    /// @return true if a probe is due
    bool nextBackground(uint32_t timeNowMs, uint32_t& addr, uint32_t& bgSlotPos)
    {
        if (_bgAddrs.empty() || (_bgSweepPeriodMs == 0))
            return false;
        uint32_t probesPerSweep = _bgAddrs.size() * (1 + _numSlots);
        uint32_t intervalMs = _bgSweepPeriodMs / probesPerSweep;
        if (intervalMs == 0)
            intervalMs = 1;
        if (_bgStarted && ((int32_t)(timeNowMs - _bgLastMs) < (int32_t)intervalMs))
            return false;
        _bgStarted = true;
        _bgLastMs = timeNowMs;
        addr = _bgAddrs[_bgIdx];
        bgSlotPos = _bgPos;
        _bgIdx++;
        if (_bgIdx >= _bgAddrs.size())
        {
            _bgIdx = 0;
            _bgPos = (_bgPos + 1) % (1 + _numSlots);
        }
        return true;
    }

    /// @brief Number of slot visits in a cycle (the sum of the weights)
    uint32_t numSlotPositions() const
    {
        return _slotSeq.size();
    }

    /// @brief The slot visits of a cycle, in order (0 the main bus, 1 + n the nth slot)
    const std::vector<uint8_t>& getSlotSeq() const
    {
        return _slotSeq;
    }

private:
    struct Cursor
    {
        uint32_t addrIdx = 0;
        uint32_t slotSeqIdx = 0;     // position in _slotSeq
        uint32_t credit = 0;        // addresses earned (in units of 1/weight) and not yet scanned
    };
    std::vector<std::vector<uint8_t>> _lists;
    std::vector<Cursor> _cursors;
    uint32_t _cur = 0;
    uint32_t _chunkLeft = 0;
    uint32_t _numSlots = 0;
    std::vector<uint8_t> _slotWeights;
    std::vector<uint8_t> _slotSeq;

    // The slot visits of a cycle by smooth weighted round robin, so a slot's visits are spread through the cycle
    void rebuildSlotSeq()
    {
        uint32_t numPos = 1 + _numSlots;
        std::vector<int32_t> weight(numPos), current(numPos, 0);
        int32_t total = 0;
        for (uint32_t i = 0; i < numPos; i++)
        {
            weight[i] = (i == 0) ? MAIN_BUS_WEIGHT : (_slotWeights[i - 1] > 0 ? _slotWeights[i - 1] : 1);
            total += weight[i];
        }
        _slotSeq.clear();
        for (int32_t step = 0; step < total; step++)
        {
            uint32_t best = 0;
            for (uint32_t i = 0; i < numPos; i++)
            {
                current[i] += weight[i];
                if (current[i] > current[best])
                    best = i;
            }
            current[best] -= total;
            _slotSeq.push_back((uint8_t)best);
        }
    }

    std::vector<uint8_t> _bgAddrs;
    uint32_t _bgSweepPeriodMs = 0;
    uint32_t _bgIdx = 0;
    uint32_t _bgPos = 0;
    uint32_t _bgLastMs = 0;
    bool _bgStarted = false;

    // Choose the list whose turn it is, and how many addresses it scans this turn. Each turn of the first list (the
    // first that has addresses) is followed by a chunk of each lower list in turn, of (its size / its weight)
    // addresses - fractions carried over - so each lower list gets the same share of probes as if it were scanned
    // whole every weight turns, but the first list comes round again sooner
    bool selectNextList()
    {
        uint32_t numLists = _lists.size();
        uint32_t first = 0;
        while ((first < numLists) && _lists[first].empty())
            first++;
        if (first >= numLists)
            return false;
        uint32_t startFrom = ((_cur == first) || (_cur < first)) ? first + 1 : _cur + 1;
        if (_cur == first)
        {
            // A turn of the first list is done: each lower list earns its share
            for (uint32_t i = first + 1; i < numLists; i++)
                _cursors[i].credit += _lists[i].size();
        }
        for (uint32_t i = startFrom; i < numLists; i++)
        {
            uint32_t weightIdx = i - first;
            uint32_t weight = weightIdx < sizeof(LIST_WEIGHTS) / sizeof(LIST_WEIGHTS[0]) ? LIST_WEIGHTS[weightIdx] :
                        (weightIdx + 1) * (weightIdx + 1);
            uint32_t chunk = _cursors[i].credit / weight;
            if (_lists[i].empty() || (chunk == 0))
                continue;
            _cursors[i].credit -= chunk * weight;
            _cur = i;
            _chunkLeft = chunk;
            return true;
        }
        _cur = first;
        _chunkLeft = _lists[first].size() - _cursors[first].addrIdx;
        return true;
    }
};
