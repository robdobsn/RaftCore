/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// LEDPixelIF.h
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "stdint.h"

class LEDPixelIF
{
public:
    LEDPixelIF()
    {
    }
    virtual ~LEDPixelIF()
    {
    }

    /// @brief Set RGB value for a pixel
    /// @param ledIdx in the segment
    /// @param r red
    /// @param g green
    /// @param b blue
    /// @param applyBrightness scale values by brightness factor if true
    virtual void setRGB(uint32_t ledIdx, uint32_t r, uint32_t g, uint32_t b, bool applyBrightness=true) = 0;

    /// @brief Set RGB value for a pixel
    /// @param ledIdx in the segment
    /// @param c RGB value
    /// @param applyBrightness scale values by brightness factor if true
    virtual void setRGB(uint32_t ledIdx, uint32_t c, bool applyBrightness=true) = 0;

    /// @brief Set RGB value for a pixel
    /// @param ledIdx in the segment
    /// @param pixRGB pixel to copy
    virtual void setRGB(uint32_t ledIdx, const LEDPixel& pixRGB) = 0;

    /// @brief Set HSV value for a pixel
    /// @param ledIdx in the segment
    /// @param hsv HSV value
    virtual void setHSV(uint32_t ledIdx, LEDPixHSV& hsv) = 0;

    /// @brief Set HSV value for a pixel
    /// @param ledIdx in the segment
    /// @param h hue
    /// @param s saturation
    /// @param v value
    virtual void setHSV(uint32_t ledIdx, uint32_t h, uint32_t s, uint32_t v) = 0;

    /// @brief Clear all pixels in the segment
    virtual void clear() = 0;

    /// @brief Get number of pixels in the segment
    virtual uint32_t getNumPixels() const = 0;

    /// @brief Show pixels
    /// @return true if successful
    virtual bool show() = 0;

    /// @brief Stop pattern generation
    virtual void stop() = 0;
};
