/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DeviceActionEncoder
// Encodes a device type record action (devInfoJson "actions") into the bytes to write to the device - the same
// encoding raftjs uses (RaftDeviceInfo encodeActionWrite / RaftStruct structPack), so firmware-side callers (the
// devman/action endpoint, DeviceManager::sendDeviceAction, scripting) and raftjs agree. Shared test vectors:
// scripts/tests/action_encode_vectors.json.
//
// Encoding:
//  - an action with a map: the value is a map key; the entry's hex (a string, or an object's "w") is written as is,
//    split on '&' into several writes
//  - otherwise numbers: one value -> (value - sub) * mul, rounded for an integer type; several values are packed
//    as given. Packed with t (Python struct codes, big-endian unless '<'), optionally a Sensirion CRC-8 after each
//    2-byte word (crc "crc8w"), between the hex of w and wz
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <vector>
#include "RaftArduino.h"
#include "RaftJsonIF.h"

class DeviceActionEncoder
{
public:
    enum class Result
    {
        OK,
        NoActions,          // the record has no actions
        NoSuchAction,       // no action of that name
        NoMapEntry,         // a map action, and the value isn't one of its keys
        BadValue,           // a value isn't a number (non-map action), or the wrong number of values
        BadType,            // t can't be packed (unknown code, or a string/other unsupported code)
        BadHex,             // w, wz or a map entry isn't whole hex bytes
        BadCrc,             // crc8w with a value that isn't whole 2-byte words, or an unknown crc
    };

    /// @brief Encode an action of a device type record
    /// @param devInfoJson the record's devInfoJson
    /// @param actionName action name (n)
    /// @param values the values: a map key for a map action, otherwise numbers (as text)
    /// @param writes (out) the writes, in order (several for a map entry with '&')
    /// @return result
    static Result encode(const char* devInfoJson, const char* actionName, const std::vector<String>& values,
                std::vector<std::vector<uint8_t>>& writes);

    /// @brief Encode one action object (an element of "actions")
    static Result encodeAction(const RaftJsonIF& actionJson, const std::vector<String>& values,
                std::vector<std::vector<uint8_t>>& writes);

    /// @brief Pack numbers as a Python struct format (as raftjs structPack: big-endian unless '<', counts as
    ///        prefix digits or [N]; codes x c b B ? h H i l I L q Q f d). Integer codes take the value truncated
    ///        toward zero, modulo the size (as a JavaScript DataView)
    /// @return false if the format can't be packed or there are too few values
    static bool structPack(const char* format, const std::vector<double>& values, std::vector<uint8_t>& out);

    /// @brief Whether a type is a single integer code (rounded when encoding one value)
    static bool isIntegerType(const char* format);

    /// @brief Name of a result (for error responses)
    static const char* getResultStr(Result result);
};
