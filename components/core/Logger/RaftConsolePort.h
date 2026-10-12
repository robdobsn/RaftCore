/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftConsolePort
//
// Lets one owner take the console's port for itself - for example a binary
// protocol on the USB-Serial-JTAG port that the console normally uses - and
// give it back later.  While the port is claimed nothing else writes text to
// it: Raft's own log output (LoggerCore), ESP-IDF's log output (the
// esp_log vprintf hook), stdout/stderr and the ROM printf channel are all
// silenced, and restored on release.  Log text can optionally be handed to a
// sink instead of being dropped.
//
// Readers of the port (SerialConsole) are told separately by the owner, as
// they are SysMods (see SerialConsole's "releasePort" / "reclaimPort" commands).
//
// claim() and release() are meant to be called from one task (the main loop);
// isClaimed() and divert() may be called from any task.
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>

/// @brief Receives text that would have gone to the console while the port is
/// claimed.  Called from whichever task logged, so it must be short and must
/// not block or log.
typedef void (*RaftConsoleTextSink)(const char* text, uint32_t len);

namespace RaftConsolePort
{
    /// @brief Take the console's port
    /// @param owner name of the claimant (only it can release)
    /// @param sink optional receiver for diverted log text (nullptr drops it)
    /// @return true if claimed (or already claimed by the same owner)
    bool claim(const char* owner, RaftConsoleTextSink sink = nullptr);

    /// @brief Give the port back and restore console output
    /// @return true if released (false if not claimed by this owner)
    bool release(const char* owner);

    /// @brief Whether the port is claimed
    bool isClaimed();

    /// @brief Change the sink while claimed (nullptr drops diverted text)
    void setSink(RaftConsoleTextSink sink);

    /// @brief For console writers (LoggerCore): if the port is claimed, hand the
    /// text to the sink (if any) and return true - the caller must then not
    /// write it to the console
    bool divert(const char* text, uint32_t len);
}
