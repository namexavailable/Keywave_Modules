/*
 * Copyright 2026 Keywave Technology Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * KW307_cmd.h - UART Command Layer (internal)
 * FW: 2.11.0
 *
 * Each function encodes the payload (Big-Endian) and sends one UART Write frame.
 * No parameter validation - callers (KW307_api.c) are responsible.
 *
 * Internal header - include KW307_api.h instead.
 */

#ifndef KW307_CMD_H
#define KW307_CMD_H

#include <stdint.h>
#include <stdbool.h>
#include "KW307_uart.h"
#include "KW307_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @cond INTERNAL */

/* ================================================================
   System CMDs (0x01~0x03 + 0x09)
   ================================================================ */

/* CMD 0x01 — blocking read of firmware version. Any out-pointer may be NULL.
 * Returns KW307_OK on success or a KW307_ERR_* value on failure. */
int kw307_cmd_read_fw_version  (int timeout_ms,
                                 uint8_t* major, uint8_t* minor, uint8_t* patch);

int kw307_cmd_factory_reset    (KW307Uart_t* u);                        /* CMD 0x02 */
int kw307_cmd_save_settings    (KW307Uart_t* u);                        /* CMD 0x03 */
int kw307_cmd_clear_state      (KW307Uart_t* u);                        /* CMD 0x09 */

/* ================================================================
   Customer CMDs

   Power / output / gain / doppler have commands of their own; the other
   encoders below write parameters through CMD 0x90.
   ================================================================ */
int kw307_cmd_power_mode       (KW307Uart_t* u, uint8_t mode);          /* CMD 0x10 */
int kw307_cmd_gain             (KW307Uart_t* u, uint8_t index);         /* CMD 0x12 */

/* CMD 0x11 — output-mode write, customer subset (module FW 2.7.6 or newer):
 *   0x00 default frame / 0xFF off. Other mode values are enabled by optional
 *   feature modules.
 * Returns KW307_OK / KW307_ERR_PARAM / KW307_ERR_PORT.
 */
int kw307_cmd_output_mode      (KW307Uart_t* u, uint8_t mode);          /* CMD 0x11 */
int kw307_cmd_detect_range     (KW307Uart_t* u, uint16_t min_mm, uint16_t max_mm);
int kw307_cmd_detect_fov       (KW307Uart_t* u, int16_t deg10, uint8_t shrink_deg10);
int kw307_cmd_signal_threshold (KW307Uart_t* u, int16_t moving, int16_t stationary);
int kw307_cmd_doppler_filter   (KW307Uart_t* u,                                        /* CMD 0x17 (6B) */
                                 uint8_t fhpf_khpf, uint8_t fhpf_nd, uint8_t flpf,
                                 uint8_t shpf_khpf, uint8_t shpf_nd, uint8_t slpf);
int kw307_cmd_moving_tracker_cnt     (KW307Uart_t* u, uint8_t cnt_limit, uint8_t display_th);
int kw307_cmd_stationary_tracker_cnt (KW307Uart_t* u, uint8_t cnt_limit, uint8_t display_th);

/* ----------------------------------------------------------------
   Parameter write (CMD 0x90)
   ---------------------------------------------------------------- */

/** Write a single raw algorithm parameter (advanced).
 *    d0 = LOW byte, d1 = HIGH byte. len = 1 (1-byte value, d1 ignored — pass 0)
 *    or len = 2 (16-bit value, big-endian).
 *  Raw parameter numbers are firmware-specific and not part of the stable API —
 *  prefer the typed setters in KW307_api.h when one exists. */
int kw307_set_parameter(uint8_t pid, uint8_t d1, uint8_t d0, uint8_t len);

/* ----------------------------------------------------------------
   Raw / advanced CMD access (no typed wrapper).
   Generic escape hatches — kept in this internal header so they stay out
   of the customer-facing API reference, but remain callable: including
   KW307_api.h pulls this header in. Read-style commands: pair with
   kw307_read_blocking() in KW307_io.h.
   ---------------------------------------------------------------- */

/** Send any command as a Write frame. data=NULL, len=0 for no payload. */
int kw307_send_cmd(uint8_t cmd, const uint8_t* data, uint16_t len);

/** Send any command as a Read-request frame. */
int kw307_read_cmd(uint8_t cmd, const uint8_t* data, uint16_t len);

/* ----------------------------------------------------------------
   Shared by the three API groups (KW307_api*.c)
   ---------------------------------------------------------------- */

/* The SDK's UART context (KW307_io.c). */
extern KW307Uart_t* kw307_default_uart(void);

/* Firmware line, as kw307_check_fw_version() found it (KW307_io.c). The calls
 * firmware 2.7.6 lacks return KW307_ERR_NOT_SUPPORTED while this is true. */
void kw307_i_fw_set_old(bool old);
bool kw307_i_fw_is_old(void);

/* Write verify is on and rejections are being seen (KW307_io.c). */
int  kw307_i_write_verify_on(void);


/* One parameter read back, its id checked against the request. */
int kw307_i_param_read_u8(uint8_t pid, uint8_t* out, int timeout_ms);
int kw307_i_param_read_i16(uint8_t pid, int16_t* out, int timeout_ms);

/* CMD 0x17 read: moving band [0..2], stationary band [3..5]. */
int kw307_i_dop_read(uint8_t dop[6], int timeout_ms);

/* Smoothing: 0~1000 in the API, Q15 on the wire. */
#define KW307_I_PERMILLE_MAX  1000
int     kw307_i_permille_to_q15(int16_t v, uint16_t* out);
int16_t kw307_i_q15_to_permille(int16_t q);

/* Big-endian int16 out of a wire buffer. The radar sends every multi-byte
 * field this way. */
int16_t kw307_i_i16be(const uint8_t* p);

/** @endcond */

#ifdef __cplusplus
}
#endif

#endif /* KW307_CMD_H */
