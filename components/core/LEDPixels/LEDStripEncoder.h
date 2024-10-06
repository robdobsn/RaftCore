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
    uint16_t bit0_0_ticks; // Duration of 0 bit in 0 state, in RMT clock ticks
    uint16_t bit0_1_ticks; // Duration of 0 bit in 1 state, in RMT clock ticks
    uint16_t bit1_0_ticks; // Duration of 1 bit in 0 state, in RMT clock ticks
    uint16_t bit1_1_ticks; // Duration of 1 bit in 1 state, in RMT clock ticks
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