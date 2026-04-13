// Minimal stubs for non-ISO Arduino functions not present on Linux
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

char* itoa(int val, char* s, int radix) {
    if (radix == 10) { sprintf(s, "%d", val); return s; }
    if (radix == 16) { sprintf(s, "%x", (unsigned)val); return s; }
    sprintf(s, "%d", val);
    return s;
}

char* utoa(unsigned int val, char* s, int radix) {
    if (radix == 10) { sprintf(s, "%u", val); return s; }
    if (radix == 16) { sprintf(s, "%x", val); return s; }
    sprintf(s, "%u", val);
    return s;
}
