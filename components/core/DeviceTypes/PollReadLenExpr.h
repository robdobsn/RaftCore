/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Poll Read Length Expression
//
// Evaluates compact expressions to compute I2C read lengths at poll time
// Expression syntax: $N[B].wE:maskHH*N:maxN  (see variable-length-poll-reads.md)
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <vector>
#include "RaftArduino.h"

class PollReadLenExpr
{
public:
    /// @brief Parse expression string like "$0.w12:mask0FFF*2:max96"
    /// @param expr expression string (content between r{ and })
    /// @return true if parsed successfully
    bool parse(const String& expr)
    {
        _ops.clear();
        _maxValue = UINT32_MAX;

        const char* p = expr.c_str();
        const char* end = p + expr.length();

        // Parse source reference: $N
        if (p >= end || *p != '$')
            return false;
        p++;
        _sourceIdx = strtoul(p, const_cast<char**>(&p), 10);

        // Optional byte offset: [B]
        _byteOffset = 0;
        if (p < end && *p == '[')
        {
            p++;
            _byteOffset = strtoul(p, const_cast<char**>(&p), 10);
            if (p < end && *p == ']')
                p++;
            else
                return false;
        }

        // Optional bit extraction: .wE (little-endian) or .WE (big-endian)
        _extractBits = 8;
        _bigEndian = false;
        if (p < end && *p == '.')
        {
            p++;
            if (p < end && (*p == 'w' || *p == 'W'))
            {
                _bigEndian = (*p == 'W');
                p++;
                _extractBits = strtoul(p, const_cast<char**>(&p), 10);
            }
            else
            {
                return false;
            }
        }

        // Parse operator chain
        while (p < end)
        {
            Op op;
            if (*p == ':')
            {
                p++;
                if (strncmp(p, "mask", 4) == 0)
                {
                    p += 4;
                    op.type = Op::AND;
                    op.operand = strtoul(p, const_cast<char**>(&p), 16);
                }
                else if (strncmp(p, "max", 3) == 0)
                {
                    p += 3;
                    op.type = Op::MAX;
                    op.operand = strtoul(p, const_cast<char**>(&p), 10);
                    _maxValue = op.operand;
                }
                else if (strncmp(p, "min", 3) == 0)
                {
                    p += 3;
                    op.type = Op::MIN;
                    op.operand = strtoul(p, const_cast<char**>(&p), 10);
                }
                else if (strncmp(p, "align", 5) == 0)
                {
                    p += 5;
                    op.type = Op::ALIGN;
                    op.operand = strtoul(p, const_cast<char**>(&p), 10);
                }
                else
                {
                    return false;
                }
            }
            else if (*p == '*')
            {
                p++;
                op.type = Op::MUL;
                op.operand = strtoul(p, const_cast<char**>(&p), 10);
            }
            else if (*p == '/')
            {
                p++;
                op.type = Op::DIV;
                op.operand = strtoul(p, const_cast<char**>(&p), 10);
            }
            else if (*p == '+')
            {
                p++;
                op.type = Op::ADD;
                op.operand = strtoul(p, const_cast<char**>(&p), 10);
            }
            else if (*p == '-')
            {
                p++;
                op.type = Op::SUB;
                op.operand = strtoul(p, const_cast<char**>(&p), 10);
            }
            else if (*p == '&')
            {
                p++;
                op.type = Op::AND;
                op.operand = strtoul(p, const_cast<char**>(&p), 16);
            }
            else if (*p == '|')
            {
                p++;
                op.type = Op::OR;
                op.operand = strtoul(p, const_cast<char**>(&p), 16);
            }
            else if (*p == '>' && (p + 1) < end && *(p + 1) == '>')
            {
                p += 2;
                op.type = Op::SHR;
                op.operand = strtoul(p, const_cast<char**>(&p), 10);
            }
            else if (*p == '<' && (p + 1) < end && *(p + 1) == '<')
            {
                p += 2;
                op.type = Op::SHL;
                op.operand = strtoul(p, const_cast<char**>(&p), 10);
            }
            else
            {
                return false;
            }
            _ops.push_back(op);
        }

        return true;
    }

    /// @brief Evaluate with access to prior read results
    /// @param priorResults vector of result buffers from earlier poll operations
    /// @return computed read length in bytes
    uint32_t evaluate(const std::vector<std::vector<uint8_t>>& priorResults) const
    {
        // Check source index is valid
        if (_sourceIdx >= priorResults.size())
            return (_maxValue != UINT32_MAX) ? _maxValue : 0;

        const auto& srcBuf = priorResults[_sourceIdx];

        // Extract value from source buffer
        uint32_t value = extractValue(srcBuf);

        // Apply operator chain
        for (const auto& op : _ops)
        {
            switch (op.type)
            {
                case Op::AND:   value &= op.operand; break;
                case Op::OR:    value |= op.operand; break;
                case Op::SHR:   value >>= op.operand; break;
                case Op::SHL:   value <<= op.operand; break;
                case Op::MUL:   value *= op.operand; break;
                case Op::DIV:   value = (op.operand != 0) ? value / op.operand : 0; break;
                case Op::ADD:   value += op.operand; break;
                case Op::SUB:   value = (value >= op.operand) ? value - op.operand : 0; break;
                case Op::MAX:   value = (value <= op.operand) ? value : op.operand; break;
                case Op::MIN:   value = (value >= op.operand) ? value : op.operand; break;
                case Op::ALIGN: value = (op.operand != 0) ? (value / op.operand) * op.operand : value; break;
            }
        }

        return value;
    }

    /// @brief Return the maximum possible value (from :max clamp, or UINT32_MAX if none)
    uint32_t getMaxValue() const
    {
        return _maxValue;
    }

    /// @brief Check if the expression has a valid max value
    bool hasMaxValue() const
    {
        return _maxValue != UINT32_MAX;
    }

private:
    /// @brief Extract a value from the source buffer
    /// @param buf source buffer
    /// @return extracted value
    uint32_t extractValue(const std::vector<uint8_t>& buf) const
    {
        uint32_t numBytes = (_extractBits + 7) / 8;

        // Check bounds
        if (_byteOffset + numBytes > buf.size())
            return 0;

        // Extract bytes
        uint32_t value = 0;
        if (_bigEndian)
        {
            for (uint32_t i = 0; i < numBytes; i++)
                value = (value << 8) | buf[_byteOffset + i];
        }
        else
        {
            for (uint32_t i = numBytes; i > 0; i--)
                value = (value << 8) | buf[_byteOffset + i - 1];
        }

        // Mask to extract bit width
        if (_extractBits < 32)
            value &= (1u << _extractBits) - 1;

        return value;
    }

    // Source reference
    uint32_t _sourceIdx = 0;       // $N index
    uint32_t _byteOffset = 0;     // [B] byte offset
    uint32_t _extractBits = 8;    // .wE bit width
    bool _bigEndian = false;      // .W vs .w

    // Operator chain
    struct Op {
        enum Type { AND, OR, SHR, SHL, MUL, DIV, ADD, SUB, MAX, MIN, ALIGN };
        Type type;
        uint32_t operand;
    };
    std::vector<Op> _ops;

    // Maximum value (from :max clamp)
    uint32_t _maxValue = UINT32_MAX;
};
