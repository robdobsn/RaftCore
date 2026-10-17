#pragma once

#if TEST_IDF_MAJOR >= 5
#error "ESP-IDF 5+ must use the modern PSRAM API"
#endif

#define ESP_SPIRAM_SIZE_INVALID -1

inline int esp_spiram_get_chip_size()
{
    return TEST_PSRAM_BYTES ? 1 : ESP_SPIRAM_SIZE_INVALID;
}
