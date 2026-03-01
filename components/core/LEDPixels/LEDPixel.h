/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// LEDPixel.h
//
// Rob Dobson 2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <string.h>
#include "LEDPixHSV.h"

#define ALWAYS_INLINE __attribute__((always_inline))

class LEDPixel {

public:

    // Maximum number of channels per pixel (e.g. RGBWW = 5)
    static const uint32_t MAX_CHANNELS = 5;

    enum ColourOrder
    {
        RGB,
        GRB,
        BGR,
        RGBWW,  // WS2805: R, G, B, Cold White, Warm White
        GRBWW,  // GRB variant with dual white channels
    };

    // Colour value (meaning depends on LED type)
	union {
		struct {
            uint8_t c1;
            uint8_t c2;
            uint8_t c3;
            uint8_t c4;
            uint8_t c5;
        };
		uint8_t raw[MAX_CHANNELS];
	};

    // Constructor (default)
    inline LEDPixel() ALWAYS_INLINE
    {
        c1 = c2 = c3 = c4 = c5 = 0;
    }

    // Constructor (copy)
    inline LEDPixel(const LEDPixel& other) ALWAYS_INLINE
    {
        *this = other;
    }

    // Get raw value as uint32_t
    uint32_t getRaw() const
    {
        return (c1 << 16) | (c2 << 8) | c3;
    }

    // Set from RGB (white channels are zeroed for multi-channel colour orders)
    inline void fromRGB(uint8_t r_in, uint8_t g_in, uint8_t b_in, ColourOrder order, float brightnessFactor=1.0f) ALWAYS_INLINE
    {
        uint8_t r = (brightnessFactor == 1.0f) ? r_in : (uint8_t)(r_in * brightnessFactor);
        uint8_t g = (brightnessFactor == 1.0f) ? g_in : (uint8_t)(g_in * brightnessFactor);
        uint8_t b = (brightnessFactor == 1.0f) ? b_in : (uint8_t)(b_in * brightnessFactor);
        switch(order)
        {
            case RGB:
                c1 = r; c2 = g; c3 = b;
                break;
            case GRB:
                c1 = g; c2 = r; c3 = b;
                break;
            case BGR:
                c1 = b; c2 = g; c3 = r;
                break;
            case RGBWW:
                c1 = r; c2 = g; c3 = b; c4 = 0; c5 = 0;
                break;
            case GRBWW:
                c1 = g; c2 = r; c3 = b; c4 = 0; c5 = 0;
                break;
        }
    }

    // Set from RGBWW (5-channel: red, green, blue, cold white, warm white)
    inline void fromRGBWW(uint8_t r_in, uint8_t g_in, uint8_t b_in, uint8_t cw_in, uint8_t ww_in,
                          ColourOrder order, float brightnessFactor=1.0f) ALWAYS_INLINE
    {
        uint8_t r = (brightnessFactor == 1.0f) ? r_in : (uint8_t)(r_in * brightnessFactor);
        uint8_t g = (brightnessFactor == 1.0f) ? g_in : (uint8_t)(g_in * brightnessFactor);
        uint8_t b = (brightnessFactor == 1.0f) ? b_in : (uint8_t)(b_in * brightnessFactor);
        uint8_t cw = (brightnessFactor == 1.0f) ? cw_in : (uint8_t)(cw_in * brightnessFactor);
        uint8_t ww = (brightnessFactor == 1.0f) ? ww_in : (uint8_t)(ww_in * brightnessFactor);
        switch(order)
        {
            case RGBWW:
                c1 = r; c2 = g; c3 = b; c4 = cw; c5 = ww;
                break;
            case GRBWW:
                c1 = g; c2 = r; c3 = b; c4 = cw; c5 = ww;
                break;
            // For 3-channel orders, white channels are discarded
            case RGB:
                c1 = r; c2 = g; c3 = b;
                break;
            case GRB:
                c1 = g; c2 = r; c3 = b;
                break;
            case BGR:
                c1 = b; c2 = g; c3 = r;
                break;
        }
    }

    // Constructor (from 24-bit value)
    inline void fromRGB(uint32_t rgb24bit, ColourOrder order, float brightnessFactor) ALWAYS_INLINE
    {
        uint8_t r = (rgb24bit >> 16) & 0xFF;
        uint8_t g = (rgb24bit >> 8) & 0xFF;
        uint8_t b = (rgb24bit >> 0) & 0xFF;
        fromRGB(r, g, b, order, brightnessFactor);
    }

    // Array access
	inline uint8_t& operator[] (uint32_t x)
    {
        return raw[x];
    }
    inline const uint8_t& operator[] (uint32_t x) const
    {
        return raw[x];
    }

    // Assignment operator
    inline LEDPixel& operator= (const LEDPixel& rhs) ALWAYS_INLINE
    {
        c1 = rhs.c1;
        c2 = rhs.c2;
        c3 = rhs.c3;
        c4 = rhs.c4;
        c5 = rhs.c5;
        return *this;
    }

    void clear()
    {
        c1 = c2 = c3 = c4 = c5 = 0;
    }

    static ColourOrder getColourOrderCode(const char* pColourOrder)
    {
        if (pColourOrder == nullptr)
            return RGB;
        if (strcasecmp(pColourOrder, "RGB") == 0)
            return RGB;
        if (strcasecmp(pColourOrder, "GRB") == 0)
            return GRB;
        if (strcasecmp(pColourOrder, "BGR") == 0)
            return BGR;
        if (strcasecmp(pColourOrder, "RGBWW") == 0)
            return RGBWW;
        if (strcasecmp(pColourOrder, "GRBWW") == 0)
            return GRBWW;
        return RGB;
    }

    static const char* getColourOrderStr(ColourOrder colourOrder)
    {
        switch(colourOrder)
        {
            case RGB: return "RGB";
            case GRB: return "GRB";
            case BGR: return "BGR";
            case RGBWW: return "RGBWW";
            case GRBWW: return "GRBWW";
        }
        return "RGB";
    }

    /// @brief Get number of bytes per pixel for a given colour order
    /// @param colourOrder Colour order
    /// @return Number of bytes per pixel (3 for RGB/GRB/BGR, 5 for RGBWW/GRBWW)
    static uint32_t getBytesPerPixel(ColourOrder colourOrder)
    {
        switch(colourOrder)
        {
            case RGBWW:
            case GRBWW:
                return 5;
            default:
                return 3;
        }
    }
};
