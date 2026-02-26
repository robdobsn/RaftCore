/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Data aggregator interface (pure virtual)
//
// Rob Dobson 2025
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <vector>

class PollDataAggregatorIF
{
public:

    /// @brief Destructor
    virtual ~PollDataAggregatorIF()
    {
    }

    /// @brief Clear the circular buffer
    virtual void clear() = 0;

    /// @brief Put a vector of uint8_t data to one slot in the circular buffer
    /// @param timeNowUs time in us (passed in to aid testing)
    /// @param data Data to add
    virtual bool put(uint64_t timeNowUs, const std::vector<uint8_t>& data) = 0;

    /// @brief Get the last vector of uint8_t data from the circular buffer
    /// @param data (output) Data to get
    /// @return true if data available
    virtual bool get(std::vector<uint8_t>& data) = 0;

    /// @brief Get the last vector of uint8_t data from the circular buffer
    /// @param data (output) Data to get
    /// @param responseSize (output) Size of each response
    /// @param maxResponsesToReturn Maximum number of responses to return (pass 0 for all available)
    /// @return number of responses returned
    virtual uint32_t get(std::vector<uint8_t>& data, uint32_t& responseSize, uint32_t maxResponsesToReturn) = 0;

    /// @brief Get the number of results stored
    virtual uint32_t count() const = 0;

    /// @brief Get the latest data
    /// @param data (output) Data to get
    /// @return true if data is new
    virtual bool getLatestValue(uint64_t& dataTimeUs, std::vector<uint8_t>& data) = 0;

    /// @brief Resize the aggregator to store a different number of results
    /// @param numResultsToStore New number of results to store
    /// @return true if resized successfully
    /// @note This clears any existing buffered data
    virtual bool resize(uint32_t numResultsToStore) = 0;
};
