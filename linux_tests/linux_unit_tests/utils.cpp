
#include "utils.h"

// char* utoa(unsigned value, char* result, int base) {
//     // check that the base if valid
//     if (base < 2 || base > 36) { *result = '\0'; return result; }

//     char* ptr = result, *ptr1 = result, tmp_char;
//     unsigned tmp_value;

//     do {
//         tmp_value = value;
//         value /= base;
//         *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[tmp_value % base];
//     } while ( value );

//     // Apply negative sign
//     *ptr-- = '\0';
//     while(ptr1 < ptr) {
//         tmp_char = *ptr;
//         *ptr--= *ptr1;
//         *ptr1++ = tmp_char;
//     }
//     return result;
// }

/**
 * C++ version 0.4 char* style "itoa":
 * Written by Lukás Chmela
 * Released under GPLv3.

    */
char* itoa(int value, char* result, int base) {
    // check that the base if valid
    if (base < 2 || base > 36) { *result = '\0'; return result; }

    char* ptr = result, *ptr1 = result, tmp_char;
    int tmp_value;

    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
    } while ( value );

    // Apply negative sign
    if (tmp_value < 0) *ptr++ = '-';
    *ptr-- = '\0';
    while(ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr--= *ptr1;
        *ptr1++ = tmp_char;
    }
    return result;
}

char* utoa(unsigned int value, char* result, int base) {
    // check that the base if valid
    if (base < 2 || base > 36) { *result = '\0'; return result; }

    char* ptr = result, *ptr1 = result, tmp_char;
    unsigned int tmp_value;

    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
    } while ( value );

    // Apply negative sign
    if (tmp_value < 0) *ptr++ = '-';
    *ptr-- = '\0';
    while(ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr--= *ptr1;
        *ptr1++ = tmp_char;
    }
    return result;
}

char* ltoa(long value, char* result, int base) {
    // check that the base if valid
    if (base < 2 || base > 36) { *result = '\0'; return result; }

    char* ptr = result, *ptr1 = result, tmp_char;
    long tmp_value;

    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
    } while ( value );

    // Apply negative sign
    if (tmp_value < 0) *ptr++ = '-';
    *ptr-- = '\0';
    while(ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr--= *ptr1;
        *ptr1++ = tmp_char;
    }
    return result;
}

char* ultoa(unsigned long value, char* result, int base) {
    // check that the base if valid
    if (base < 2 || base > 36) { *result = '\0'; return result; }

    char* ptr = result, *ptr1 = result, tmp_char;
    unsigned long tmp_value;

    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
    } while ( value );

    // Apply negative sign
    if (tmp_value < 0) *ptr++ = '-';
    *ptr-- = '\0';
    while(ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr--= *ptr1;
        *ptr1++ = tmp_char;
    }
    return result;
}

char* lltoa(long long value, char* result, int base) {
    // check that the base if valid
    if (base < 2 || base > 36) { *result = '\0'; return result; }

    char* ptr = result, *ptr1 = result, tmp_char;
    long long tmp_value;

    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
    } while ( value );

    // Apply negative sign
    if (tmp_value < 0) *ptr++ = '-';
    *ptr-- = '\0';
    while(ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr--= *ptr1;
        *ptr1++ = tmp_char;
    }
    return result;
}

char* ulltoa(unsigned long long value, char* result, int base) {
    // check that the base if valid
    if (base < 2 || base > 36) { *result = '\0'; return result; }

    char* ptr = result, *ptr1 = result, tmp_char;
    unsigned long long tmp_value;

    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
    } while ( value );

    // Apply negative sign
    if (tmp_value < 0) *ptr++ = '-';
    *ptr-- = '\0';
    while(ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr--= *ptr1;
        *ptr1++ = tmp_char;
    }
    return result;
}

char* dtostrf(double value, int width, unsigned int precision, char* result) {
    char* ptr = result;
    char* ptr1 = result;
    char tmp_char;
    int tmp_value;
    double tmp_float;

    // Check for negative number
    if (value < 0.0) {
        *ptr++ = '-';
        value = -value;
    }

    // Calculate magnitude
    tmp_float = value;
    while ((tmp_float >= 10.0) && (width > 1)) {
        tmp_float /= 10.0;
        width--;
    }

    // Calculate leading zeros
    while ((width > 1) && (precision > 0)) {
        tmp_float *= 10.0;
        width--;
        precision--;
    }

    // Round value
    tmp_float += 0.5;

    // Copy reversed digits to result
    tmp_value = (int)tmp_float;
    tmp_float -= tmp_value;
    if (tmp_float > 0.5) {
        tmp_value++;
        if (tmp_value >= 10) {
            tmp_value = 0;
            *ptr++ = '1';
        }
    }
    if (tmp_value == 0) *ptr++ = '0';
    else {
        while (tmp_value > 0) {
            *ptr++ = (char)('0' + (tmp_value % 10));
            tmp_value /= 10;
        }
    }

    // Copy decimal point
    if (precision > 0) *ptr++ = '.';

    // Copy digits from tmp_float
    while (precision > 0) {
        tmp_float *= 10.0;
        tmp_value = (int)tmp_float;
        *ptr++ = (char)('0' + tmp_value);
        tmp_float -= tmp_value;
        precision--;
    }

    // Add tailing zeros
    while ((width > 1) && (ptr > ptr1) && (*(ptr-1) == '0')) {
        ptr--;
        width--;
    }

    // Add tailing space
    while ((width > 1) && (ptr > ptr1)) {
        *ptr++ = ' ';
        width--;
    }

    // Add null termination
    *ptr = '\0';

    // Reverse string
    ptr--;
    while(ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr--= *ptr1;
        *ptr1++ = tmp_char;
    }

    return result;
}

