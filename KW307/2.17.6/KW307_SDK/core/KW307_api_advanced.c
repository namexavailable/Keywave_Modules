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
 * KW307 SDK - API, Advanced Settings - Implementation
 * FW: 2.11.0
 */

#include <stddef.h>     /* NULL */
#include "KW307_api.h"
#include "KW307_cmd.h"
#include "KW307_io.h"

/* ---- Doppler filter corners (CMD 0x17) --------------------------------- */

/* Four calls, one per corner, because that is how the filters are reasoned
 * about: a band's high-pass decides the slowest movement it still reports, its
 * low-pass the fastest.
 *
 * The command carries all six bytes and has no per-byte write, so each call
 * reads the six back, changes its own and sends the six. What the module holds
 * is the only record: a copy kept here would start from the defaults on every
 * host reset and put them back over whatever the module had saved.
 * kw307_set_motion_sensitivity() and kw307_set_stationary_sensitivity() come
 * through these same four. */

/* Change n bytes of CMD 0x17 starting at slot i, the rest as the module has
 * them. A failed read sends nothing. */
static int dop_write(uint8_t i, const uint8_t* v, uint8_t n)
{
    uint8_t dop[6];
    uint8_t k;
    int rc = kw307_i_dop_read(dop, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;

    for (k = 0u; k < n; k++) dop[i + k] = v[k];
    return kw307_send_cmd(KW307_CMD_DOPPLER_FILTER, dop, 6u);
}

int kw307_set_moving_hpf_corner(uint8_t corner, uint8_t nd)
{
    uint8_t v[2];
    if (corner > 7u || nd > 3u) return KW307_ERR_PARAM;
    v[0] = corner;  v[1] = nd;
    return dop_write(0u, v, 2u);
}

int kw307_set_moving_lpf_corner(uint8_t corner)
{
    return dop_write(2u, &corner, 1u);
}

int kw307_set_stationary_hpf_corner(uint8_t corner, uint8_t nd)
{
    uint8_t v[2];
    if (corner > 7u || nd > 3u) return KW307_ERR_PARAM;
    v[0] = corner;  v[1] = nd;
    return dop_write(3u, v, 2u);
}

int kw307_set_stationary_lpf_corner(uint8_t corner)
{
    return dop_write(5u, &corner, 1u);
}

int kw307_get_moving_hpf_corner(uint8_t* corner, uint8_t* nd)
{
    uint8_t dop[6];
    int rc;

    if (corner == NULL || nd == NULL) return KW307_ERR_PARAM;
    rc = kw307_i_dop_read(dop, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;

    *corner = (uint8_t)(dop[0] & 0x07u);
    *nd     = (uint8_t)(dop[1] & 0x03u);
    return KW307_OK;
}

int kw307_get_moving_lpf_corner(uint8_t* corner)
{
    uint8_t dop[6];
    int rc;

    if (corner == NULL) return KW307_ERR_PARAM;
    rc = kw307_i_dop_read(dop, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;

    *corner = dop[2];
    return KW307_OK;
}

int kw307_get_stationary_hpf_corner(uint8_t* corner, uint8_t* nd)
{
    uint8_t dop[6];
    int rc;

    if (corner == NULL || nd == NULL) return KW307_ERR_PARAM;
    rc = kw307_i_dop_read(dop, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;

    *corner = (uint8_t)(dop[3] & 0x07u);
    *nd     = (uint8_t)(dop[4] & 0x03u);
    return KW307_OK;
}

int kw307_get_stationary_lpf_corner(uint8_t* corner)
{
    uint8_t dop[6];
    int rc;

    if (corner == NULL) return KW307_ERR_PARAM;
    rc = kw307_i_dop_read(dop, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;

    *corner = dop[5];
    return KW307_OK;
}

/* ---- Front end (CMD 0x32) ---------------------------------------------- */

/* One command carries three fields: the two first-stage background switches,
 * the equalizer gain and the baseband attenuator. The calls below are split
 * by field rather than by command, so each writes only its own bytes and reads
 * the rest back first.
 *
 * Byte[0] is the two first-stage background switches, independent of each
 * other:
 *
 *   bit0  kw307_en_background_subtraction()
 *   bit1  kw307_en_background_update()
 *
 * Both are on at power-up. Both bits mean what they say, so nothing is
 * negated on the way through. */
#define FE_BG_SUBTRACTION_BIT  0x01u
#define FE_BG_UPDATE_BIT       0x02u

/* Read the current three bytes so a setter can change its own and put the
 * others back unchanged. */
static int fe_read(uint8_t b[3])
{
    uint16_t n = 0u;
    int rc = kw307_read_blocking(0x32u, NULL, 0u, b, 3u, &n, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;
    return (n < 3u) ? KW307_ERR_PORT : KW307_OK;
}

/* Change one bit of byte[0] and put the other two bytes back as read. */
static int fe_write_bit(uint8_t mask, bool set)
{
    uint8_t b[3];
    int rc = fe_read(b);
    if (rc != KW307_OK) return rc;

    if (set) b[0] |= mask;
    else     b[0] = (uint8_t)(b[0] & (uint8_t)~mask);
    return kw307_send_cmd(0x32u, b, 3u);
}

static int fe_read_bit(uint8_t mask, bool* out)
{
    uint8_t b[3];
    int rc;

    if (out == NULL) return KW307_ERR_PARAM;
    rc = fe_read(b);
    if (rc != KW307_OK) return rc;

    *out = (b[0] & mask) != 0u;
    return KW307_OK;
}

int kw307_en_background_subtraction(bool on)
{
    return fe_write_bit(FE_BG_SUBTRACTION_BIT, on);
}

int kw307_get_background_subtraction(bool* on)
{
    return fe_read_bit(FE_BG_SUBTRACTION_BIT, on);
}

int kw307_en_background_update(bool on)
{
    if (kw307_i_fw_is_old()) return KW307_ERR_NOT_SUPPORTED;
    return fe_write_bit(FE_BG_UPDATE_BIT, on);
}

int kw307_get_background_update(bool* on)
{
    if (kw307_i_fw_is_old()) return KW307_ERR_NOT_SUPPORTED;
    return fe_read_bit(FE_BG_UPDATE_BIT, on);
}

/* Change one whole byte of the three and put the other two back as read. */
static int fe_write_byte(uint8_t idx, uint8_t value)
{
    uint8_t b[3];
    int rc = fe_read(b);
    if (rc != KW307_OK) return rc;

    b[idx] = value;
    return kw307_send_cmd(0x32u, b, 3u);
}

#define FE_EQ_GAIN_BYTE  1u
#define FE_BB_ATT_BYTE   2u

int kw307_set_equalizer_gain(uint8_t index)
{
    if (index > 3u) return KW307_ERR_PARAM;
    return fe_write_byte(FE_EQ_GAIN_BYTE, index);
}

int kw307_get_equalizer_gain(uint8_t* index)
{
    uint8_t b[3];
    int rc;

    if (index == NULL) return KW307_ERR_PARAM;
    rc = fe_read(b);
    if (rc != KW307_OK) return rc;

    *index = (uint8_t)(b[FE_EQ_GAIN_BYTE] & 0x03u);
    return KW307_OK;
}

int kw307_set_baseband_att(uint8_t index)
{
    if (index > 2u) return KW307_ERR_PARAM;
    return fe_write_byte(FE_BB_ATT_BYTE, index);
}

int kw307_get_baseband_att(uint8_t* index)
{
    uint8_t b[3];
    uint8_t v;
    int rc;

    if (index == NULL) return KW307_ERR_PARAM;
    rc = fe_read(b);
    if (rc != KW307_OK) return rc;

    v = (uint8_t)(b[FE_BB_ATT_BYTE] & 0x03u);
    *index = (v <= 2u) ? v : KW307_VALUE_NONE;
    return KW307_OK;
}

/* ---- Digital gain (CMD 0x3A) ------------------------------------------ */

/* Both gain fields ride in one byte: [3:2] moving band, [1:0] stationary. */
int kw307_set_digital_gain(uint8_t index)
{
    uint8_t b0;

    if (kw307_i_fw_is_old()) return KW307_ERR_NOT_SUPPORTED;
    if (index > 3u) return KW307_ERR_PARAM;

    b0 = (uint8_t)((index << 2) | index);
    return kw307_send_cmd(0x3Au, &b0, 1u);
}

int kw307_get_digital_gain(uint8_t* index)
{
    uint8_t  v = 0u;
    uint16_t n = 0u;
    int      rc;

    if (kw307_i_fw_is_old()) return KW307_ERR_NOT_SUPPORTED;
    if (index == NULL) return KW307_ERR_PARAM;

    rc = kw307_read_blocking(0x3Au, NULL, 0u, &v, 1u, &n, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;
    if (n < 1u)         return KW307_ERR_PORT;

    /* The setter puts the same value in both halves of the byte. If they differ
     * the module is running something this SDK did not set, so there is no one
     * index to report. */
    if ((uint8_t)(v >> 2) != (uint8_t)(v & 0x03u)) return KW307_ERR_PORT;

    *index = (uint8_t)(v & 0x03u);
    return KW307_OK;
}

/* ---- Equaliser order (CMD 0x39) --------------------------------------- */

/* The two orders as their three bytes; index 0 is order 1. */
static const uint8_t EQ_ORDER_T[2][3] = {
    {  0u,  85u, 0u },   /* 1st order */
    { 11u, 170u, 0u },   /* 2nd order - the module default */
};

int kw307_set_equalizer_order(uint8_t order)
{
    const uint8_t* t;

    if (kw307_i_fw_is_old()) return KW307_ERR_NOT_SUPPORTED;
    if (order < 1u || order > 2u) return KW307_ERR_PARAM;

    t = EQ_ORDER_T[order - 1u];
    return kw307_set_equalizer_cmd(t[0], t[1], t[2]);
}

int kw307_get_equalizer_order(uint8_t* order)
{
    uint8_t eq[3];
    uint8_t i;
    int     rc;

    if (kw307_i_fw_is_old()) return KW307_ERR_NOT_SUPPORTED;
    if (order == NULL) return KW307_ERR_PARAM;

    rc = kw307_get_equalizer_cmd(&eq[0], &eq[1], &eq[2]);
    if (rc != KW307_OK) return rc;

    *order = KW307_VALUE_NONE;
    for (i = 0u; i < 2u; i++) {
        if (eq[0] == EQ_ORDER_T[i][0] && eq[1] == EQ_ORDER_T[i][1] &&
            eq[2] == EQ_ORDER_T[i][2]) {
            *order = (uint8_t)(i + 1u);
            break;
        }
    }
    return KW307_OK;
}

int kw307_set_equalizer_cmd(uint8_t response, uint8_t gain_set1, uint8_t gain_set2)
{
    uint8_t b[3];

    if (kw307_i_fw_is_old()) return KW307_ERR_NOT_SUPPORTED;
    if (response > 0x0Fu) return KW307_ERR_PARAM;   /* [7:4] reserved */

    /* All three bytes go together; the module has no per-byte write. */
    b[0] = response;
    b[1] = gain_set1;
    b[2] = gain_set2;
    return kw307_send_cmd(0x39u, b, 3u);
}

int kw307_get_equalizer_cmd(uint8_t* response, uint8_t* gain_set1, uint8_t* gain_set2)
{
    uint8_t  b[3] = { 0u, 0u, 0u };
    uint16_t n    = 0u;
    int      rc;

    if (kw307_i_fw_is_old()) return KW307_ERR_NOT_SUPPORTED;
    if (response == NULL || gain_set1 == NULL || gain_set2 == NULL) return KW307_ERR_PARAM;

    rc = kw307_read_blocking(0x39u, NULL, 0u, b, 3u, &n, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;
    if (n < 3u)         return KW307_ERR_PORT;

    *response  = b[0];
    *gain_set1 = b[1];
    *gain_set2 = b[2];
    return KW307_OK;
}

/* ---- Static clutter rejection: two switches per band (CMD 0x35) -------- */

/* One command byte per band, two bits in it:
 *
 *   bit0  en_hpf       the background estimate keeps adapting
 *   bit1  en_bypass    the band outputs raw instead of subtracted
 *
 * bit1 is the inverse of what the API says: environment subtraction ON means
 * The inversion is applied here and nowhere else.
 *
 * Each setter reads the pair back first and changes only its own bit; the
 * rest of both bytes goes back as read. */

#define SCR_HPF_BIT   0x01u
#define SCR_BYP_BIT   0x02u

static int scr_read(uint8_t b[2])
{
    uint16_t n = 0u;
    int rc = kw307_read_blocking(0x35u, NULL, 0u, b, 2u, &n, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;
    if (n < 2u)         return KW307_ERR_PORT;
    return KW307_OK;
}

/* band: 0 moving, 1 stationary. mask: which bit. set: its new value. */
static int scr_write_bit(uint8_t band, uint8_t mask, bool set)
{
    uint8_t b[2];
    int rc = scr_read(b);
    if (rc != KW307_OK) return rc;

    if (set) b[band] |= mask;
    else     b[band] = (uint8_t)(b[band] & (uint8_t)~mask);
    return kw307_send_cmd(0x35u, b, 2u);
}

static int scr_read_bit(uint8_t band, uint8_t mask, bool* out)
{
    uint8_t b[2];
    int rc;

    if (out == NULL) return KW307_ERR_PARAM;
    rc = scr_read(b);
    if (rc != KW307_OK) return rc;

    *out = (b[band] & mask) != 0u;
    return KW307_OK;
}

int kw307_set_moving_background(bool update)
{
    return scr_write_bit(0u, SCR_HPF_BIT, update);
}

int kw307_set_stationary_background(bool update)
{
    return scr_write_bit(1u, SCR_HPF_BIT, update);
}

int kw307_get_moving_background(bool* update)
{
    return scr_read_bit(0u, SCR_HPF_BIT, update);
}

int kw307_get_stationary_background(bool* update)
{
    return scr_read_bit(1u, SCR_HPF_BIT, update);
}

int kw307_en_moving_env_subtraction(bool on)
{
    return scr_write_bit(0u, SCR_BYP_BIT, !on);      /* bypass is the inverse */
}

int kw307_en_stationary_env_subtraction(bool on)
{
    return scr_write_bit(1u, SCR_BYP_BIT, !on);
}

int kw307_get_moving_env_subtraction(bool* on)
{
    bool byp;
    int rc = scr_read_bit(0u, SCR_BYP_BIT, &byp);
    if (rc != KW307_OK) return rc;
    *on = !byp;
    return KW307_OK;
}

int kw307_get_stationary_env_subtraction(bool* on)
{
    bool byp;
    int rc = scr_read_bit(1u, SCR_BYP_BIT, &byp);
    if (rc != KW307_OK) return rc;
    *on = !byp;
    return KW307_OK;
}

/* ---- Stationary hold (PID 0x73) --------------------------------------- */

int kw307_en_stationary_hold(bool on)
{
    if (kw307_i_fw_is_old()) return KW307_ERR_NOT_SUPPORTED;
    /* The module stops re-deciding the stationary band, so every stationary
     * slot keeps what it held. For riding out the transient the environment
     * subtraction switch leaves behind. */
    return kw307_set_parameter(0x73u, 0u, on ? 1u : 0u, 1u);
}

int kw307_get_stationary_hold(bool* on)
{
    uint8_t v = 0u;
    int     rc;

    if (kw307_i_fw_is_old()) return KW307_ERR_NOT_SUPPORTED;
    if (on == NULL) return KW307_ERR_PARAM;

    rc = kw307_i_param_read_u8(0x73u, &v, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;

    *on = (v != 0u);
    return KW307_OK;
}

/* ---- Antenna (CMD 0x33) ----------------------------------------------- */

/* One call for all three bytes, because that is how the module takes them:
 * splitting transmit from receive would turn one write into a read and a
 * write per half, with a window where the module holds a topology neither
 * caller asked for.
 *
 * rx_mode goes to the module as it is: the module writes it straight into
 * the chip's receiver mode. The module numbers the transmitters from 1, the
 * call from 0, so tx_mode is sent one higher and read back one lower. */

int kw307_set_antenna(uint8_t tx_mode, uint8_t rx_mode, bool with_angle)
{
    uint8_t b[3];

    /* CMD 0x33 was the sweep command on older firmware: never send it there. */
    if (kw307_i_fw_is_old()) return KW307_ERR_NOT_SUPPORTED;
    if (tx_mode > 1u) return KW307_ERR_PARAM;
    if (rx_mode > 7u) return KW307_ERR_PARAM;

    b[0] = (uint8_t)(tx_mode + 1u);
    b[1] = rx_mode;
    b[2] = with_angle ? 1u : 0u;
    return kw307_send_cmd(0x33u, b, 3u);
}

int kw307_get_antenna(uint8_t* tx_mode, uint8_t* rx_mode, bool* with_angle)
{
    uint8_t  b[3] = { 0u, 0u, 0u };
    uint16_t got  = 0u;
    int      rc;

    if (tx_mode == NULL || rx_mode == NULL || with_angle == NULL)
        return KW307_ERR_PARAM;
    if (kw307_i_fw_is_old()) return KW307_ERR_NOT_SUPPORTED;

    rc = kw307_read_blocking(0x33u, NULL, 0u, b, 3u, &got, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;
    if (got < 3u)       return KW307_ERR_PORT;

    *tx_mode    = (b[0] >= 1u && b[0] <= 2u) ? (uint8_t)(b[0] - 1u) : KW307_VALUE_NONE;
    *rx_mode    = (b[1] <= 7u) ? b[1] : KW307_VALUE_NONE;
    *with_angle = (b[2] != 0u);
    return KW307_OK;
}

/* ---- Magnitude type (PID 0x53) ---------------------------------------- */

/* Which magnitude algorithm the module uses: without, or from, the angle
 * estimation. The module keeps it as a parameter on its own;
 * kw307_set_antenna() leaves it alone, so a caller who sets it is not undone
 * by the next antenna call. */
int kw307_set_mag_type(uint8_t mag_type)
{
    if (mag_type > 1u) return KW307_ERR_PARAM;
    return kw307_set_parameter(0x53u, 0u, mag_type, 1u);
}

int kw307_get_mag_type(uint8_t* mag_type)
{
    uint8_t v = 0u;
    int     rc;

    if (mag_type == NULL) return KW307_ERR_PARAM;

    rc = kw307_i_param_read_u8(0x53u, &v, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;
    *mag_type = (v != 0u) ? 1u : 0u;
    return KW307_OK;
}

/* ---- Sampling rate (CMD 0x34) ----------------------------------------- */

int kw307_set_sample_rate(uint8_t sel)
{
    if (sel > 3u) return KW307_ERR_PARAM;
    return kw307_send_cmd(0x34u, &sel, 1u);
}

int kw307_get_sample_rate(uint8_t* sel)
{
    uint8_t  v = 0u;
    uint16_t n = 0u;
    int      rc;

    if (sel == NULL) return KW307_ERR_PARAM;

    rc = kw307_read_blocking(0x34u, NULL, 0u, &v, 1u, &n, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;
    if (n < 1u)         return KW307_ERR_PORT;

    *sel = (uint8_t)(v & 0x03u);
    return KW307_OK;
}

/* ---- PLL calibration (PID 0x56~0x58, 0x72) ----------------------------- */

#define PID_EN_AUTO_PLL_CAL        0x56u
#define PID_AUTO_PLL_CAL_INTERVAL  0x57u
#define PID_EN_AUTO_PLL_ERR_CAL    0x58u
#define PID_EN_PLL_CAL_ON_BOOT     0x72u

static int pid_set_bool(uint8_t pid, bool on)
{
    return kw307_set_parameter(pid, 0u, on ? 1u : 0u, 1u);
}

static int pid_get_bool(uint8_t pid, bool* on)
{
    uint8_t v = 0u;
    int rc;
    if (on == NULL) return KW307_ERR_PARAM;
    rc = kw307_i_param_read_u8(pid, &v, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;
    *on = (v != 0u);
    return KW307_OK;
}

int kw307_en_pll_cal_on_boot(bool on)
{
    if (kw307_i_fw_is_old()) return KW307_ERR_NOT_SUPPORTED;
    return pid_set_bool(PID_EN_PLL_CAL_ON_BOOT, on);
}

int kw307_get_pll_cal_on_boot(bool* on)
{
    if (kw307_i_fw_is_old()) return KW307_ERR_NOT_SUPPORTED;
    return pid_get_bool(PID_EN_PLL_CAL_ON_BOOT, on);
}

int kw307_en_auto_pll_cal(bool on)
{
    return pid_set_bool(PID_EN_AUTO_PLL_CAL, on);
}

int kw307_get_auto_pll_cal(bool* on)
{
    return pid_get_bool(PID_EN_AUTO_PLL_CAL, on);
}

int kw307_set_auto_pll_cal_interval(uint16_t frames)
{
    /* A zero interval has no meaning, and the module refuses the top half. */
    if (frames == 0u || frames > 32767u) return KW307_ERR_PARAM;
    return kw307_set_parameter(PID_AUTO_PLL_CAL_INTERVAL,
                               (uint8_t)(frames >> 8),
                               (uint8_t)(frames & 0xFFu), 2u);
}

int kw307_get_auto_pll_cal_interval(uint16_t* frames)
{
    int16_t v = 0;
    int rc;

    if (frames == NULL) return KW307_ERR_PARAM;

    rc = kw307_i_param_read_i16(PID_AUTO_PLL_CAL_INTERVAL, &v, KW307_READ_TIMEOUT_MS);
    if (rc != KW307_OK) return rc;
    *frames = (uint16_t)v;
    return KW307_OK;
}

int kw307_en_auto_pll_err_cal(bool on)
{
    return pid_set_bool(PID_EN_AUTO_PLL_ERR_CAL, on);
}

int kw307_get_auto_pll_err_cal(bool* on)
{
    return pid_get_bool(PID_EN_AUTO_PLL_ERR_CAL, on);
}
