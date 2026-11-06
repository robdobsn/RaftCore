/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// LEDStripRMTPlan.h
//
// The RMT channel an LED strip asks for: how much memory and whether it uses DMA, as a list of channel configs to
// try in order (the first one the driver can allocate is used).
//
// A frame that fits in the channel's memory is written there before it starts, so nothing can corrupt it. A frame
// that doesn't fit is refilled by the RMT interrupt while it goes out, half the memory at a time; if that interrupt
// is late the hardware wraps and sends stale symbols, which shows earlier pixels' colours further down the strip.
// So, unless memBlockSymbols is configured:
// - the whole frame in RMT memory if it fits in this strip's share of the TX memory (the TX blocks shared equally
//   between the strips);
// - otherwise DMA, where the chip has it: the frame is encoded into a buffer in internal RAM, whole if it fits in
//   DMA_MAX_SYMBOLS, otherwise refilled half a buffer at a time, which tolerates far longer interrupt delays;
// - otherwise (and if those can't be allocated, e.g. memory taken by another channel) the largest share of RMT
//   memory, down to one block.
//
// Raft-free so it can be host-tested (linux_unit_tests/test_led_strip_rmt_plan).
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <vector>

class LEDStripRMTPlan
{
public:
    // Use of DMA for the strip's RMT channel
    enum DmaMode : uint8_t
    {
        DMA_AUTO = 0,       // when the frame doesn't fit in the strip's share of RMT memory
        DMA_ON = 1,         // always (if the chip has it)
        DMA_OFF = 2,        // never
    };

    // The chip's RMT TX resources
    struct ChipCaps
    {
        uint32_t blockSymbols = 48;     // symbols in one channel's memory block
        uint32_t txChannels = 4;        // channels that can transmit (one channel can use the following ones' blocks)
        bool dmaSupported = false;      // a TX channel can use DMA
    };

    // A channel config to try
    struct Attempt
    {
        uint32_t memSymbols = 0;        // mem_block_symbols: RMT memory, or with DMA the DMA buffer
        bool withDma = false;
        bool operator==(const Attempt& other) const
        {
            return (memSymbols == other.memSymbols) && (withDma == other.withDma);
        }
    };

    // Symbols after the pixel data (the encoder's reset code)
    static constexpr uint32_t RESET_SYMBOLS = 1;

    // Largest DMA buffer: the driver splits it into two descriptors of at most 4095 bytes (4 bytes per symbol)
    static constexpr uint32_t DMA_MAX_SYMBOLS = 2032;

    // Each DMA descriptor's buffer is rounded down to the DMA alignment, so a buffer is asked for with this margin
    // to hold a frame whole
    static constexpr uint32_t DMA_MARGIN_SYMBOLS = 32;

    /// @brief Symbols in a frame
    /// @param numPixels pixels in the strip
    /// @param bytesPerPixel bytes per pixel (3 for RGB, 5 for RGBWW)
    static uint32_t frameSymbols(uint32_t numPixels, uint32_t bytesPerPixel)
    {
        return numPixels * bytesPerPixel * 8 + RESET_SYMBOLS;
    }

    /// @brief DMA buffer for a frame (the whole frame if it fits in DMA_MAX_SYMBOLS)
    static uint32_t dmaSymbols(uint32_t frameSyms)
    {
        uint32_t syms = roundUp(frameSyms, 16) + DMA_MARGIN_SYMBOLS;
        return syms > DMA_MAX_SYMBOLS ? DMA_MAX_SYMBOLS : syms;
    }

    /// @brief Channel configs to try, in order
    /// @param frameSyms symbols in a frame (frameSymbols())
    /// @param configMemSymbols memBlockSymbols from the strip's config, 0 for automatic
    /// @param dmaMode use of DMA
    /// @param numStrips strips on the chip (each gets an equal share of TX memory when sized automatically)
    /// @param caps the chip's RMT resources
    static std::vector<Attempt> attempts(uint32_t frameSyms, uint32_t configMemSymbols, DmaMode dmaMode,
                uint32_t numStrips, const ChipCaps& caps)
    {
        std::vector<Attempt> list;
        bool dmaAllowed = caps.dmaSupported && (dmaMode != DMA_OFF);

        // Configured size: as given (the driver needs an even number, at least one block)
        if (configMemSymbols > 0)
        {
            uint32_t syms = roundUp(configMemSymbols, 2);
            if (syms < caps.blockSymbols)
                syms = caps.blockSymbols;
            list.push_back({syms, caps.dmaSupported && (dmaMode == DMA_ON)});
            return list;
        }

        // This strip's share of the TX blocks, and the blocks the frame needs
        uint32_t shareBlocks = caps.txChannels / (numStrips > 0 ? numStrips : 1);
        if (shareBlocks == 0)
            shareBlocks = 1;
        uint32_t frameBlocks = (frameSyms + caps.blockSymbols - 1) / caps.blockSymbols;

        // DMA first if asked for
        if (dmaAllowed && (dmaMode == DMA_ON))
            list.push_back({dmaSymbols(frameSyms), true});

        // The whole frame in RMT memory
        if (frameBlocks <= shareBlocks)
            list.push_back({frameBlocks * caps.blockSymbols, false});

        // DMA (if the frame doesn't fit, or the memory for it couldn't be allocated)
        if (dmaAllowed && (dmaMode == DMA_AUTO))
            list.push_back({dmaSymbols(frameSyms), true});

        // As much RMT memory as can be had, down to one block
        uint32_t blocks = (frameBlocks <= shareBlocks) ? frameBlocks - 1 : shareBlocks;
        for (; blocks >= 1; blocks--)
            list.push_back({blocks * caps.blockSymbols, false});
        return list;
    }

    /// @brief Check if a frame is refilled while it goes out (so a late RMT or DMA interrupt can corrupt it)
    static bool needsRefill(uint32_t frameSyms, const Attempt& attempt)
    {
        uint32_t capacity = attempt.withDma ?
                    (attempt.memSymbols > DMA_MARGIN_SYMBOLS ? attempt.memSymbols - DMA_MARGIN_SYMBOLS : 0) :
                    attempt.memSymbols;
        return frameSyms > capacity;
    }

    /// @brief Parse a DMA mode ("auto", "on"/"1"/"true", "off"/"0"/"false"; anything else is auto)
    static DmaMode parseDmaMode(const char* pStr)
    {
        if (!pStr)
            return DMA_AUTO;
        if (strEq(pStr, "on") || strEq(pStr, "1") || strEq(pStr, "true"))
            return DMA_ON;
        if (strEq(pStr, "off") || strEq(pStr, "0") || strEq(pStr, "false"))
            return DMA_OFF;
        return DMA_AUTO;
    }

    static const char* getDmaModeStr(DmaMode dmaMode)
    {
        switch (dmaMode)
        {
            case DMA_ON: return "on";
            case DMA_OFF: return "off";
            default: return "auto";
        }
    }

private:
    static uint32_t roundUp(uint32_t val, uint32_t multiple)
    {
        return ((val + multiple - 1) / multiple) * multiple;
    }

    // Case-insensitive string compare
    static bool strEq(const char* pA, const char* pB)
    {
        for (; *pA && *pB; pA++, pB++)
        {
            char a = (*pA >= 'A' && *pA <= 'Z') ? (*pA + 'a' - 'A') : *pA;
            char b = (*pB >= 'A' && *pB <= 'Z') ? (*pB + 'a' - 'A') : *pB;
            if (a != b)
                return false;
        }
        return *pA == *pB;
    }
};
