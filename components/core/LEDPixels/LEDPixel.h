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
#include <LEDPixHSV.h>

#define ALWAYS_INLINE __attribute__((always_inline))

class LEDPixel {

public:

    enum ColourOrder
    {
        RGB,
        GRB,
        BGR,
    };

    // Colour value (meaning depends on LED type)
	union {
		struct {
            uint8_t c1;
            uint8_t c2;
            uint8_t c3;
        };
		uint8_t raw[3];
	};

    // Constructor (default)
    inline LEDPixel() ALWAYS_INLINE
    {
        c1 = c2 = c3 = 0;
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

    // Set from RGB
    inline void fromRGB(uint8_t r_in, uint8_t g_in, uint8_t b_in, ColourOrder order, float brightnessFactor=1.0f) ALWAYS_INLINE
    {
        if (brightnessFactor == 1.0f)
        {
            switch(order)
            {
                case RGB:
                    c1 = r_in;
                    c2 = g_in;
                    c3 = b_in;
                    break;
                case GRB:
                    c1 = g_in;
                    c2 = r_in;
                    c3 = b_in;
                    break;
                case BGR:
                    c1 = b_in;
                    c2 = g_in;
                    c3 = r_in;
                    break;
            }
        }
        else
        {
            // Apply brightness factor
            uint8_t r = r_in * brightnessFactor;
            uint8_t g = g_in * brightnessFactor;
            uint8_t b = b_in * brightnessFactor;
            switch(order)
            {
                case RGB:
                    c1 = r;
                    c2 = g;
                    c3 = b;
                    break;
                case GRB:
                    c1 = g;
                    c2 = r;
                    c3 = b;
                    break;
                case BGR:
                    c1 = b;
                    c2 = g;
                    c3 = r;
                    break;
            }
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
        return *this;
    }

    void clear()
    {
        c1 = c2 = c3 = 0;
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
        return RGB;
    }

    static const char* getColourOrderStr(ColourOrder colourOrder)
    {
        switch(colourOrder)
        {
            case RGB: return "RGB";
            case GRB: return "GRB";
            case BGR: return "BGR";
        }
        return "RGB";
    }
};
