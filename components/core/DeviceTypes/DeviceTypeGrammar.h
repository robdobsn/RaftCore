/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DeviceTypeGrammar.h
//
// One parser for the three command strings of a device type record - detectionValues, initValues and
// pollInfo.c - which share a syntax but give the value side of a step different meanings. A string is
// compiled once into steps; problems are reported with their position.
//
// A string is a list of steps separated by "&" or ";". Each step is "write[=value]":
//   write   hex bytes to write, with or without 0x (empty: nothing written). A step that is only "pNN" (no "=")
//           is a pause
//   value   detection: comma-separated alternatives to compare with what is read, each "0x<hex>" (every bit
//           compared), "0b<bits>" (x = don't care) or "rN" (read N bytes, compare nothing); optionally
//           preceded by a pause "pNN", and optionally followed by ":crc8w" (each 2-byte word read is followed
//           by its Sensirion CRC-8, which must be valid). A value that is only "pNN" pauses after the write
//           init:   "rN" read N bytes, "pNN" pause after the write (either or both)
//           poll:   "rN" read N bytes, "r{expr}" read a length computed from earlier results, "0x<hex>" or
//                   "0b<bits>" read that many bytes, "pNN" pause after the write
//
// Problems are errors or warnings. In this release (D19) a warning keeps what the previous parser did, so
// every record behaves as before; an error is something the previous parser turned into garbage or skipped:
// a detection string with an error never matches, and an init or poll step with an error is dropped.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <memory>
#include <string>
#include <vector>
#include "PollReadLenExpr.h"

class DeviceTypeGrammar
{
public:
    // Which string is being compiled (the value side of a step means something different in each)
    enum class Field : uint8_t
    {
        Detection,
        Init,
        Poll,
    };

    // Problems found while compiling
    enum class IssueCode : uint8_t
    {
        // Errors
        NotHex,                 // write bytes that aren't hex
        MultipleEquals,         // more than one "=" in a step
        BadCheck,               // a detection value that isn't 0x, 0b, rN or a pause
        BadReadExpr,            // an r{...} that doesn't parse or has no :max
        ReadExprNotAllowed,     // r{...} outside a poll string
        BadReadLength,          // rN with N missing, 0 or more than 65535
        BadValue,               // a poll value that isn't rN, r{...}, 0x, 0b or a pause
        CrcLength,              // :crc8w on a check whose length isn't a multiple of 3
        BadPause,               // a pause longer than 65535 ms
        // Warnings (the previous behaviour is kept)
        OddHex,                 // odd number of hex digits (the last digit is lost; the byte is padded with 0)
        BarePause,              // "pNN" without "=" - a pause (the previous parser wrote it as data, C4)
        InitValueNotWritten,    // an init value of 0x... - not written, only the bytes before "=" are
        InitBinaryIsReadCount,  // an init value of 0b... - read that many bytes, not written
        InitValueIgnored,       // an init value that isn't rN or pNN - ignored
        MixedCheckLengths,      // detection alternatives of different lengths (only the first length is read)
        BinaryNotWholeBytes,    // a 0b value whose bit count isn't a multiple of 8 (trailing bits compared as 0)
    };

    struct Issue
    {
        IssueCode code;
        uint16_t pos;           // character position in the string
        bool isError() const
        {
            return DeviceTypeGrammar::isError(code);
        }
    };

    // A detection check: the bytes read match if (read & mask) == value, for every byte
    struct Check
    {
        std::vector<uint8_t> mask;
        std::vector<uint8_t> value;
    };

    // One step: write, then read (and check, for detection), then pause
    struct Step
    {
        std::vector<uint8_t> write;
        uint16_t readLen = 0;
        uint16_t pauseMs = 0;
        bool hasValue = false;              // the step has a value side ("=")
        std::vector<Check> checks;          // detection: alternatives (any one matching is a match)
        bool crc8w = false;                 // detection: each 2-byte word read is followed by a valid CRC-8
        std::shared_ptr<PollReadLenExpr> readLenExpr;   // poll: read length computed from earlier results
    };

    struct Result
    {
        std::vector<Step> steps;
        std::vector<Issue> issues;
        bool hasErrors() const
        {
            for (const Issue& issue : issues)
                if (issue.isError())
                    return true;
            return false;
        }
    };

    /// @brief Compile a command string
    /// @param pStr string (nullptr is the same as empty)
    /// @param field which string it is
    /// @return steps and issues. Steps with errors are left out (a detection string with an error must not be
    ///         used at all)
    static Result compile(const char* pStr, Field field);

    /// @brief Check whether an issue is an error
    static bool isError(IssueCode code)
    {
        return code < IssueCode::OddHex;
    }

    /// @brief Short name of an issue (as used in the shared test vectors)
    static const char* getIssueName(IssueCode code);

    /// @brief Name of a field
    static const char* getFieldName(Field field);

    /// @brief Check the Sensirion CRC-8 (poly 0x31, init 0xff) of each 2-byte word in data laid out as
    ///        [word][crc][word][crc]...
    /// @return true if every CRC is valid (and the length is a multiple of 3)
    static bool checkCrc8Words(const uint8_t* pData, uint32_t len);

    /// @brief Sensirion CRC-8 of bytes
    static uint8_t crc8(const uint8_t* pData, uint32_t len);
};
