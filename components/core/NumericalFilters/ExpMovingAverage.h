/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Exponential Moving Average
//
// Rob Dobson 2020-2023
//
// Based on https://tttapa.github.io/Pages/Mathematics/Systems-and-Control-Theory/Digital-filters/Simple%20Moving%20Average/C++Implementation.html
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <cstdint>
#include <type_traits>

/// Exponential Moving Average implementation for unsigned integers.
///
/// This code is from https://tttapa.github.io/Pages/Mathematics/Systems-and-Control-Theory/Digital-filters/Exponential%20Moving%20Average/C++Implementation.html#arduino-example

template <uint16_t K, class input_t = uint32_t>
class ExpMovingAverage
{
  public:
    ExpMovingAverage()
    {
        clear();
    }
    /// Update the filter with the given input and return the filtered output.
    input_t sample(input_t input)
    {
        state += input;
        output = (state + half) >> K;
        state -= output;
        return output;
    }

    input_t getAverage() 
    {
        return output;
    }

    static_assert(std::is_unsigned<input_t>::value,
                  "The `input_t` type should be an unsigned integer, "
                  "otherwise, the division using bit shifts is invalid.");

    /// Fixed point representation of one half, used for rounding.
    constexpr static input_t half = 1 << (K - 1);

  private:
    void clear()
    {
        state = 0;
        output = 0;
    }
    input_t state = 0;
    input_t output = 0;
};

