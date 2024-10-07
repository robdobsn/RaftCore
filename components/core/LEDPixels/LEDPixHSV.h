/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// LEDPixHSV.h
//
// Rob Dobson 2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <math.h>
#include "RaftArduino.h"
#include <algorithm>

#define ALWAYS_INLINE __attribute__((always_inline))

#define USE_SIMPLE_HSV_TO_RGB

class LEDPixHSV {

public:

    union {
		struct {
            // Hue (0-360)
            uint16_t h;
            // Saturation (0-100)
            uint8_t s;
            // Value (0-100)
            uint8_t v;
		};
		uint8_t raw[3];
	};

    // Constructor (default)
    inline LEDPixHSV()
    {
        h = s = v = 0;
    }

    // Constructor (from hsv)
    inline LEDPixHSV(uint16_t h_in, uint8_t s_in, uint8_t v_in) ALWAYS_INLINE
    {
        h = h_in;
        s = s_in;
        v = v_in;
    }

    // Constructor (copy)
    inline LEDPixHSV(const LEDPixHSV& other) ALWAYS_INLINE
    {
        h = other.h;
        s = other.s;
        v = other.v;
    }

    // Operator =
    inline LEDPixHSV& operator= (const LEDPixHSV& rhs) ALWAYS_INLINE
    {
        h = rhs.h;
        s = rhs.s;
        v = rhs.v;
        return *this;
    }

    // Set
    inline LEDPixHSV& set(uint16_t h_in, uint8_t s_in, uint8_t v_in) ALWAYS_INLINE
    {
        h = h_in;
        s = s_in;
        v = v_in;
        return *this;
    }

    // To RGB
    uint32_t toRGB() const
    {
        return toRGB(h, s, v);
    }

    static uint32_t toRGB(uint32_t h, uint32_t s, uint32_t v)
    {
#ifdef USE_SIMPLE_HSV_TO_RGB
        h %= 360; // h -> [0,360]
        uint32_t rgb_max = v * 2.55f;
        uint32_t rgb_min = rgb_max * (100 - s) / 100.0f;

        uint32_t i = h / 60;
        uint32_t diff = h % 60;

        // RGB adjustment amount by hue
        uint32_t rgb_adj = (rgb_max - rgb_min) * diff / 60;
        uint32_t r, g, b = 0;

        switch (i) {
        case 0:
            r = rgb_max;
            g = rgb_min + rgb_adj;
            b = rgb_min;
            break;
        case 1:
            r = rgb_max - rgb_adj;
            g = rgb_max;
            b = rgb_min;
            break;
        case 2:
            r = rgb_min;
            g = rgb_max;
            b = rgb_min + rgb_adj;
            break;
        case 3:
            r = rgb_min;
            g = rgb_max - rgb_adj;
            b = rgb_max;
            break;
        case 4:
            r = rgb_min + rgb_adj;
            g = rgb_min;
            b = rgb_max;
            break;
        default:
            r = rgb_max;
            g = rgb_min;
            b = rgb_max - rgb_adj;
            break;
        }
        return (r << 16) | (g << 8) | b;
#else
        // Using the code from https://www.vagrearg.org/content/hsvrgb
        // Check for 0 saturation (return all v)
        if (s == 0)
        {
            return (v << 16) | (v << 8) | v;
        }

        // Get sextant
        uint8_t sextant = h >> 8;
        sextant = sextant > 5 ? 5 : sextant;

        // Pointers defined by sextant
        uint8_t r, g, b = 0;
        uint8_t* pR = &r;
        uint8_t* pG = &g;
        uint8_t* pB = &b;
        if (sextant & 2)
        {
            // Swap pR & pB
            pR = &b;
            pB = &r;
        }
        if (sextant & 4)
        {
            // Swap pG & pB
            uint8_t* pTmp = pG;
            pG = pB;
            pB = pTmp;
        }
        if (!(sextant & 6))
        {
            if (!(sextant & 1))
            {
                // Swap pR & pG
                uint8_t* pTmp = pR;
                pR = pG;
                pG = pTmp;
            }
        }
        else
        {
            if (sextant & 1)
            {
                // Swap pR & pG
                uint8_t* pTmp = pR;
                pR = pG;
                pG = pTmp;
            }
        }

        // Top level
        *pG = v;

        // Bottom level
        uint16_t ww;		// Intermediate result
        ww = v * (255 - s);	// We don't use ~s to prevent size-promotion side effects
        ww += 1;		// Error correction
        ww += ww >> 8;		// Error correction
        *pB = ww >> 8;

        uint8_t h_fraction = h & 0xff;	// 0...255
        uint32_t d = 0;			// Intermediate result

        if(!(sextant & 1)) {
            // *r = ...slope_up...;
            d = v * (uint32_t)((255 << 8) - (uint16_t)(s * (256 - h_fraction)));
            d += d >> 8;	// Error correction
            d += v;		// Error correction
            *pR = d >> 16;
        } else {
            // *r = ...slope_down...;
            d = v * (uint32_t)((255 << 8) - (uint16_t)(s * h_fraction));
            d += d >> 8;	// Error correction
            d += v;		// Error correction
            *pR = d >> 16;
        }
        return (r << 16) | (g << 8) | b;
#endif
    }

    // From RGB
    static LEDPixHSV fromRGB(uint32_t rgb)
    {
        // Extract the RGB components
        uint8_t r = (rgb >> 16) & 0xFF;
        uint8_t g = (rgb >> 8) & 0xFF;
        uint8_t b = rgb & 0xFF;

        // Calculate the min and max values among R, G, B
        uint8_t rgb_min = std::min({r, g, b});
        uint8_t rgb_max = std::max({r, g, b});

        // Calculate value (v)
        uint32_t v = rgb_max * 100 / 255;

        // Calculate saturation (s)
        uint32_t s = (rgb_max == 0) ? 0 : (rgb_max - rgb_min) * 100 / rgb_max;

        // Calculate hue (h)
        uint32_t h = 0;
        if (rgb_max == rgb_min) {
            h = 0;  // Undefined hue (grayscale)
        } else {
            if (rgb_max == r) {
                h = 60 * ((g - b) * 1.0f / (rgb_max - rgb_min)) + 360;
                h = fmod(h, 360);  // Ensure h is in the range [0, 360]
            } else if (rgb_max == g) {
                h = 60 * ((b - r) * 1.0f / (rgb_max - rgb_min)) + 120;
            } else {
                h = 60 * ((r - g) * 1.0f / (rgb_max - rgb_min)) + 240;
            }
        }

        return LEDPixHSV(h, s, v);
    }

    // To string
    String toString() const
    {
        return String(h) + "," + String(s) + "," + String(v);
    }

    /// @brief Get interpolated value between two HSV values based on a factor
    /// @param hsv1 First HSV value
    /// @param hsv2 Second HSV value
    /// @param factor Factor (0.0 to 1.0)
    static LEDPixHSV interpolate(const LEDPixHSV& hsv1, const LEDPixHSV& hsv2, float factor)
    {
        // Check factor
        if (factor <= 0.0f)
            return hsv1;
        if (factor >= 1.0f)
            return hsv2;
        // Interpolate
        uint16_t h = (int)hsv1.h + (((int)hsv2.h - (int)hsv1.h) * factor);
        uint8_t s = (int)hsv1.s + (((int)hsv2.s - (int)hsv1.s) * factor);
        uint8_t v = (int)hsv1.v + (((int)hsv2.v - (int)hsv1.v) * factor);
        return LEDPixHSV(h, s, v);
    }
};
