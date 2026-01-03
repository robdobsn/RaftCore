/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include "esp_idf_version.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)

#include "driver/rmt_encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Type of led strip encoder configuration
 */
typedef struct {
    uint32_t resolution; // Encoder resolution in Hz
    uint16_t T0H_ticks; // Duration of high time for 0 bit (WS2812: T0H), in RMT clock ticks
    uint16_t T0L_ticks; // Duration of low time for 0 bit (WS2812: T0L), in RMT clock ticks
    uint16_t T1H_ticks; // Duration of high time for 1 bit (WS2812: T1H), in RMT clock ticks
    uint16_t T1L_ticks; // Duration of low time for 1 bit (WS2812: T1L), in RMT clock ticks
    uint16_t reset_ticks; // Duration of reset code, in RMT clock ticks
    bool msbFirst;
} led_strip_encoder_config_t;

/**
 * @brief Create RMT encoder for encoding LED strip pixels into RMT symbols
 *
 * @param[in] config Encoder configuration
 * @param[out] ret_encoder Returned encoder handle
 * @return
 *      - ESP_ERR_INVALID_ARG for any invalid arguments
 *      - ESP_ERR_NO_MEM out of memory when creating led strip encoder
 *      - ESP_OK if creating encoder successfully
 */
esp_err_t rmt_new_led_strip_encoder(const led_strip_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder);

#ifdef __cplusplus
}
#endif

#endif // ESP_IDF_VERSION