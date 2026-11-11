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
 * KW307_cmd.c - UART Command Layer (internal)
 * FW: 2.11.0
 *
 * Encodes each CMD payload (Big-Endian) and sends via kw307_uart_send_write().
 */

#include "KW307_cmd.h"
#include "KW307_io.h"     /* kw307_read_blocking, KW307Err_t */
#include "KW307_port.h"   /* kw307_port_is_open */
#include <stddef.h>       /* NULL */

/** @cond INTERNAL */

/* ================================================================
   Internal encode helpers
   ================================================================ */

/* Every write in this file goes out through kw307_write_checked() rather than
 * kw307_uart_send_write(). With write-verify off (the default) the two are the
 * same call; with it on, each write waits for its ACK — see the mode's notes in
 * KW307_io.h. Routing it here means a caller that turns verify on covers the
 * multi-write high-level calls too, without a single call site changing.
 *
 * Declared extern here rather than in a header for the same reason
 * kw307_default_uart() is: it stays an internal concept of the io/cmd/api
 * triad and never becomes part of the public surface. */
extern int kw307_write_checked(uint8_t cmd, const uint8_t* data, uint16_t len);

/* Returns 0, KW307_ERR_PORT (-1, transport) or the KW307Err_t write-verify
 * produced - already a KW307Err_t, so a setter returns it as it is. */
static int cmd1(KW307Uart_t* u, uint8_t cmd, uint8_t val)
{
    (void)u;   /* the checked path uses the singleton, like kw307_set_parameter */
    return kw307_write_checked(cmd, &val, 1);
}

/* ================================================================
   System CMDs
   ================================================================ */

int kw307_cmd_read_fw_version(int timeout_ms,
                               uint8_t* major, uint8_t* minor, uint8_t* patch)
{
    uint8_t  resp[3];
    uint16_t n = 0;
    int ret = kw307_read_blocking(KW307_CMD_FW_VERSION, NULL, 0,
                                  resp, sizeof(resp), &n, timeout_ms);
    if (ret != KW307_OK) return ret;
    if (n < 3)           return KW307_ERR_RESP_LEN;
    if (major) *major = resp[0];
    if (minor) *minor = resp[1];
    if (patch) *patch = resp[2];
    return KW307_OK;
}

int kw307_cmd_factory_reset(KW307Uart_t* u)
{
    return cmd1(u, KW307_CMD_FACTORY_RESET, 0x01u);
}

int kw307_cmd_save_settings(KW307Uart_t* u)
{
    return cmd1(u, KW307_CMD_SAVE_SETTINGS, 0x01u);
}

int kw307_cmd_clear_state(KW307Uart_t* u)
{
    return cmd1(u, KW307_CMD_CLEAR_STATE, 0x01u);
}

/* ================================================================
   Customer CMDs
   ================================================================ */

int kw307_cmd_power_mode  (KW307Uart_t* u, uint8_t mode)   { return cmd1   (u, KW307_CMD_POWER_MODE,   mode);   }
int kw307_cmd_gain        (KW307Uart_t* u, uint8_t index)  { return cmd1   (u, KW307_CMD_GAIN,         index);  }

/* CMD 0x11 — customer-subset encoder. See KW307_cmd.h. */
int kw307_cmd_output_mode(KW307Uart_t* u, uint8_t mode)
{
    /* Customer output set (MCU FW 2.7.6 or newer):
     *   0x00 default frame / 0xFF off. Other mode values are enabled by
     *   optional feature modules. */
    if (mode > 0x02u && mode != 0xFFu) return KW307_ERR_PARAM;
    return cmd1(u, KW307_CMD_OUTPUT_MODE, mode);
}

/* The module refuses a bound that would cross the one it holds.
 *
 * Without write-verify a refusal goes unseen, so max, min, max lands whichever
 * way the window moves: the first max may be refused, and the second one, sent
 * after the new min, is not.
 *
 * With write-verify the first refusal would stop the call. So read the minimum
 * the module holds and send the two in the order that never crosses it: max
 * first unless the new max is below that minimum. If the read fails, fall back
 * to max, min, max. */
static int set_range_pid(uint8_t pid, uint16_t mm)
{
    return kw307_set_parameter(pid, (uint8_t)(mm >> 8), (uint8_t)(mm & 0xFFu), 2u);
}

int kw307_cmd_detect_range(KW307Uart_t* u, uint16_t min_mm, uint16_t max_mm)
{
    int16_t cur_min = 0;
    int     r;

    (void)u;
    if (kw307_i_write_verify_on() &&
        kw307_i_param_read_i16(0x0Fu, &cur_min, KW307_READ_TIMEOUT_MS) == KW307_OK) {
        if ((int32_t)max_mm >= (int32_t)cur_min) {
            r = set_range_pid(0x10u, max_mm);  if (r) return r;
            return set_range_pid(0x0Fu, min_mm);
        }
        r = set_range_pid(0x0Fu, min_mm);  if (r) return r;
        return set_range_pid(0x10u, max_mm);
    }

    r = set_range_pid(0x10u, max_mm);  if (r) return r;
    r = set_range_pid(0x0Fu, min_mm);  if (r) return r;
    return set_range_pid(0x10u, max_mm);
}

int kw307_cmd_detect_fov(KW307Uart_t* u, int16_t deg10, uint8_t shrink_deg10)
{
    (void)u;
    int r = kw307_set_parameter(0x11u, (uint8_t)((uint16_t)deg10 >> 8),
                                  (uint8_t)((uint16_t)deg10 & 0xFFu), 2u);
    if (r) return r;
    return    kw307_set_parameter(0x12u, 0u, shrink_deg10, 2u);
}

int kw307_cmd_signal_threshold(KW307Uart_t* u, int16_t moving, int16_t stationary)
{
    (void)u;
    int r = kw307_set_parameter(0x24u, (uint8_t)((uint16_t)moving >> 8),
                                  (uint8_t)((uint16_t)moving & 0xFFu), 2u);
    if (r) return r;
    return    kw307_set_parameter(0x25u, (uint8_t)((uint16_t)stationary >> 8),
                                  (uint8_t)((uint16_t)stationary & 0xFFu), 2u);
}

int kw307_cmd_doppler_filter(KW307Uart_t* u,
                              uint8_t fhpf_khpf, uint8_t fhpf_nd, uint8_t flpf,
                              uint8_t shpf_khpf, uint8_t shpf_nd, uint8_t slpf)
{
    uint8_t buf[6] = { fhpf_khpf, fhpf_nd, flpf, shpf_khpf, shpf_nd, slpf };
    (void)u;
    return kw307_write_checked(KW307_CMD_DOPPLER_FILTER, buf, 6);
}

static int send_tracker_pids(uint8_t pid_cnt_max, uint8_t pid_cnt_show,
                              uint8_t cnt_limit, uint8_t display_th)
{
    int r;
    r = kw307_set_parameter(pid_cnt_max,   0u, cnt_limit,     1u);
    if (r) return r;
    return kw307_set_parameter(pid_cnt_show, 0u, display_th, 1u);
}

int kw307_cmd_moving_tracker_cnt(KW307Uart_t* u, uint8_t cnt_limit, uint8_t display_th)
{
    (void)u;
    return send_tracker_pids(0x2Eu, 0x2Fu, cnt_limit, display_th);
}

int kw307_cmd_stationary_tracker_cnt(KW307Uart_t* u, uint8_t cnt_limit, uint8_t display_th)
{
    (void)u;
    return send_tracker_pids(0x29u, 0x2Au, cnt_limit, display_th);
}


/* ================================================================
   Parameter write (CMD 0x90)
   ================================================================ */

/* Forward declaration of the SDK's singleton UART accessor (implemented in
 * KW307_api.c — kept out of any header so the singleton stays an internal
 * concept of the api/cmd layers, not part of the public surface). */
extern KW307Uart_t* kw307_default_uart(void);

int kw307_set_parameter(uint8_t pid, uint8_t d1, uint8_t d0, uint8_t len)
{
    if (len == 0u || len > 2u) return -1;
    uint8_t buf[3];
    buf[0] = pid;
    if (len == 2u) {
        buf[1] = d1;   /* HI */
        buf[2] = d0;   /* LO */
    } else {           /* len == 1 */
        buf[1] = d0;   /* the value byte */
    }
    return kw307_write_checked(KW307_CMD_ALGO_PARAM, buf, (uint16_t)(1u + len));
}

/* ---- Generic raw CMD escape hatches (declared in KW307_cmd.h) ---- */

int kw307_send_cmd(uint8_t cmd, const uint8_t* data, uint16_t len)
{
    if (!data && len > 0) return KW307_ERR_PARAM;
    if (!kw307_port_is_open()) return KW307_ERR_PORT_CLOSED;
    {
        /* Propagate the real code: with write-verify on this can be ERR_NACK or
         * ERR_TIMEOUT, which a flat ERR_PORT_SEND would hide. The two return
         * sets are distinguishable because the transport layer only ever answers
         * 0 or -1 (KW307_uart.c send_frame), while every KW307Err_t that
         * kw307_write_blocking can produce is -2 or lower. */
        int rc = kw307_write_checked(cmd, data, len);
        if (rc == 0)  return KW307_OK;
        if (rc == -1) return KW307_ERR_PORT_SEND;   /* raw transport failure */
        return rc;                                   /* a specific KW307Err_t */
    }
}

int kw307_read_cmd(uint8_t cmd, const uint8_t* data, uint16_t len)
{
    if (!data && len > 0) return KW307_ERR_PARAM;
    if (!kw307_port_is_open()) return KW307_ERR_PORT_CLOSED;
    return kw307_uart_send_read(kw307_default_uart(), cmd, data, len) == 0
           ? KW307_OK : KW307_ERR_PORT_SEND;
}

/* ================================================================
   Shared by the three API groups (KW307_api*.c)
   ================================================================ */

/* ---- Readback helpers ----------------------------------------------------
 * The module answers a parameter read with the parameter id followed by its
 * value, so the id is checked before the value is trusted: a reply for a
 * different parameter means the link is out of step, not that the value is 0. */

static int param_read(uint8_t pid, uint8_t* v, uint8_t n_val, int timeout_ms)
{
    uint8_t  buf[3];
    uint16_t n  = 0u;
    int      rc = kw307_read_blocking(0x90u, &pid, 1u, buf,
                                      (uint16_t)(n_val + 1u), &n, timeout_ms);
    uint8_t  i;

    if (rc != KW307_OK)              return rc;
    if (n != (uint16_t)(n_val + 1u)) return KW307_ERR_PORT;
    if (buf[0] != pid)               return KW307_ERR_PORT;

    for (i = 0u; i < n_val; i++) v[i] = buf[i + 1u];
    return KW307_OK;
}

int kw307_i_param_read_u8(uint8_t pid, uint8_t* out, int timeout_ms)
{
    return param_read(pid, out, 1u, timeout_ms);
}

int kw307_i_param_read_i16(uint8_t pid, int16_t* out, int timeout_ms)
{
    uint8_t b[2];
    int     rc = param_read(pid, b, 2u, timeout_ms);

    if (rc != KW307_OK) return rc;
    *out = (int16_t)(((uint16_t)b[0] << 8) | (uint16_t)b[1]);
    return KW307_OK;
}

/* One read of CMD 0x17 - both bands' filter bytes: moving band [0..2]
 * (high-pass corner, nd, low-pass), stationary band [3..5]. */
int kw307_i_dop_read(uint8_t dop[6], int timeout_ms)
{
    uint16_t n  = 0u;
    int      rc = kw307_read_blocking(0x17u, NULL, 0u, dop, 6u, &n, timeout_ms);

    if (rc != KW307_OK) return rc;
    return (n < 6u) ? KW307_ERR_PORT : KW307_OK;
}

/* Smoothing is Q15 on the wire and 0~1000 in this API. It rounds to the nearest
 * raw step rather than truncating, so 1000 really does reach full scale. */

int kw307_i_permille_to_q15(int16_t v, uint16_t* out)
{
    if (v < 0 || v > KW307_I_PERMILLE_MAX) return KW307_ERR_PARAM;
    /* +500 so the division rounds; 1000 lands on 32768, which saturates to
     * the largest value the field can hold. */
    uint32_t q = (((uint32_t)v * 32768u) + 500u) / 1000u;
    *out = (uint16_t)((q > 32767u) ? 32767u : q);
    return KW307_OK;
}

int16_t kw307_i_q15_to_permille(int16_t q)
{
    if (q < 0) q = 0;
    return (int16_t)((((uint32_t)q * 1000u) + 16384u) / 32768u);
}

/** @endcond */


int16_t kw307_i_i16be(const uint8_t* p)
{
    return (int16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}
