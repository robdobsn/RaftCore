#pragma once

#if TEST_IDF_MAJOR < 5
#error "ESP-IDF 4 must use the legacy PSRAM API"
#endif

inline size_t esp_psram_get_size()
{
    return TEST_PSRAM_BYTES;
}
