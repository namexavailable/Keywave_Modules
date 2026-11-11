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
 * KW307 SDK - Protocol Layer
 * FW: 2.11.0
 *
 * Defines:
 *   - CMD IDs
 *   - ACK status codes
 *   - Output data struct (self-contained, no algorithm dependency)
 *   - Decode: parse output frame DATA payload into struct
 *
 * Dependencies: stdint.h only - no OS dependencies.
 */

#ifndef KW307_PROTOCOL_H
#define KW307_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
   System CMDs
   ================================================================ */
#define KW307_CMD_FW_VERSION          0x01   /* Read only: [major][minor][patch] */
#define KW307_CMD_FACTORY_RESET       0x02   /* Write 0x01 to trigger */
#define KW307_CMD_SAVE_SETTINGS       0x03   /* Write 0x01 to persist settings to flash */
#define KW307_CMD_CLEAR_STATE         0x09   /* Write 0x01 to clear detection state */

/* ================================================================
   Customer CMDs
   Same CMD ID used for Write (AA 55) and Read (AA 56).
   ================================================================ */
#define KW307_CMD_POWER_MODE          0x10   /* 1 byte: see KW307_POWER_xxx */
#define KW307_CMD_OUTPUT_MODE         0x11   /* 1 byte: see KW307_WIRE_OUTPUT_xxx */
#define KW307_CMD_GAIN                0x12   /* 1 byte: index 1~8 = 0 to +21 dB, 3 dB steps */
#define KW307_CMD_DOPPLER_FILTER      0x17   /* 6 bytes: Doppler filter settings, both bands */
#define KW307_CMD_ALGO_PARAM          0x90   /* parameter write/read: 1-byte parameter ID, then a 1- or 2-byte value, big-endian */

/* ACK response CMD */
#define KW307_CMD_ACK                 0xAC

/* Periodic output CMDs (module -> host) */
#define KW307_CMD_OUT_FRAME           0xF0   /* default frame, 27 bytes: presence flag + 2 moving + 1 stationary target */
/* Additional output-frame CMD IDs are defined by optional feature modules. */

/* ================================================================
   Power Mode values (CMD 0x10)
   ================================================================ */
#define KW307_POWER_DOWN              0   /* lowest current; sensing resumes only on NORMAL */
#define KW307_POWER_NORMAL            1   /* sensing */
#define KW307_POWER_PA_OFF            2   /* transmit off, rest running; return via NORMAL */

/* ================================================================
   Output Mode values (CMD 0x11)
   ================================================================ */
#define KW307_WIRE_OUTPUT_NORMAL            0x00   /* default — frame containing presence_flag + 2 moving + 1 stationary target (decode: KW307Frame_t) */
#define KW307_WIRE_OUTPUT_PARKING           0x02   /* frame 0xF2: 2 moving targets + stationary gates 0~3.
                                                      Not used or decoded by this SDK; parking uses 0xF3. */
/* Additional output-mode values are defined by optional feature modules. */
#define KW307_WIRE_OUTPUT_OFF              0xFF   /* stop periodic UART output; radar still runs */

/* ================================================================
   ACK Status codes
   ================================================================ */
#define KW307_STATUS_OK               0x00
#define KW307_STATUS_INVALID          0x01   /* value out of range */
#define KW307_STATUS_UNKNOWN          0x02   /* unknown CMD */
#define KW307_STATUS_CRC_ERR          0x03   /* CRC-16 mismatch */
#define KW307_STATUS_LEN_ERR          0x04   /* wrong length */

/* ================================================================
   Output data structs - self-contained (no algorithm dependency)
   All fields Big-Endian in wire format; parsed to host-endian here.
   ================================================================ */
#pragma pack(push, 1)

/**
 * @brief One tracked target.
 *
 * Element type of KW307Frame_t::moving[] and KW307Frame_t::stationary[].
 * Wire format on UART is mm, but the SDK decoder converts to cm before
 * populating this struct so the customer sees whole-cm units (sub-cm
 * precision is truncated).
 *
 * The two arrays use different `state` value ranges (documented below).
 */
typedef struct {
    uint16_t distance_cm;    /**< Distance in cm (decoder: wire mm / 10). */
    int16_t  angle_tenth;    /**< Angle in tenths of a degree (e.g. -350 = -35.0 deg). */
    uint16_t mag;            /**< Magnitude of the target in this slot, 0 when the
                              *   slot is Empty - or the room's noise floor, if
                              *   kw307_en_noise_floor_report() is on. Use `state`, not
                              *   `mag`, to tell whether a slot holds a target. */
    uint8_t  state;          /**< Slot status, both arrays:
                              *     0 = Empty, 1 = Valid. An Empty slot also has
                              *     distance / angle / mag / counter zeroed.
                              *     A target rejected as repetitive motion
                              *     (e.g. a fan) is reported as Empty.
                              *
                              *   A slot holds a real target at Valid: `state == 1`.
                              *   Do not use `state != 0`: on firmware older than
                              *   2.7.6, moving[] is Valid at 3, and 1 / 2 are
                              *   not yet confirmed. */
    uint8_t  counter;        /**< Age / confidence count, from 0 up to the cnt_limit
                               *   set with kw307_set_moving_tracker_cnt() /
                               *   kw307_set_stationary_tracker_cnt() (defaults 32
                               *   moving, 64 stationary). It rises while
                              *   the target keeps being detected and falls when it is
                              *   missed; the slot is released at 0. Useful as a
                              *   confidence or fade level. */
} KW307Tracker_t;

#pragma pack(pop)

/**
 * @brief Periodic radar frame delivered to your callback.
 *
 * Decoded from the module's auto-streamed frame output. Contains a frame
 * counter, the presence flag, two moving-target slots, and one
 * stationary-target slot. Pass-by-pointer in kw307_on_frame() callbacks;
 * the pointer is only valid for the duration of the callback - copy
 * fields out if you need them later.
 */
typedef struct {
    uint16_t       frame_index;     /**< Frame counter. Wraps at 65535 and
                                     *   restarts from 0 on kw307_clear_state(),
                                     *   kw307_radar_init() and
                                     *   kw307_factory_reset(). */
    uint8_t        presence_flag;   /**< 1 = presence detected this frame. */
    KW307Tracker_t moving[2];       /**< Two moving-target slots (walking, gesturing, ...). */
    KW307Tracker_t stationary[1];   /**< One stationary-target slot (a stationary target, micro-motion). */
} KW307Frame_t;

/**
 * @brief The default frame, plus how fast each moving target is going.
 *
 * Identical content to KW307Frame_t with moving_speed[] added. It arrives on
 * frame 0xF3, not 0xF0; apart from the speed, the body is byte-for-byte what
 * 0xF0 carries.
 *
 * Select it with kw307_set_output(KW307_OUTPUT_NORMAL_WITH_SPD) and receive it
 * with kw307_on_frame_spd().
 */
typedef struct {
    uint16_t       frame_index;     /**< Frame counter. Wraps at 65535 and
                                     *   restarts from 0 on kw307_clear_state(),
                                     *   kw307_radar_init() and
                                     *   kw307_factory_reset(). */
    uint8_t        presence_flag;   /**< 1 = presence detected this frame. */
    KW307Tracker_t moving[2];       /**< Two moving-target slots. */
    KW307Tracker_t stationary[1];   /**< One stationary-target slot. */
    int16_t        moving_speed[2]; /**< Mean radial speed of moving[0] / moving[1],
                                     *   in the module's own unit, NOT mm/s - the
                                     *   unit the tracker speed settings use. At
                                     *   the default sampling rate 500 is about
                                     *   0.19 m/s; after kw307_set_sample_rate()
                                     *   calibrate against a known walking speed.
                                     *
                                     *   The SIGN is the useful part and needs no
                                     *   calibration: POSITIVE = approaching the
                                     *   radar, negative = moving away. That answers
                                     *   "coming or going", which a magnitude alone
                                     *   cannot.
                                     *
                                     *   Full int16 range. Zero when the matching
                                     *   slot holds no target, so read `state`
                                     *   first - a target standing still and an
                                     *   empty slot both report 0 here. */
} KW307FrameSpd_t;

/** The same frame with the near stationary gates added (wire CMD 0xF3, 66 B).
 *
 * Everything KW307FrameSpd_t carries, unchanged and in the same order, plus the
 * stationary band's first four range gates in full. The targets keep being
 * reported while you look at the gates, so switch into this mode for a
 * measurement and back out afterwards.
 *
 * The gates are raw readings, one per range gate, taken before grouping and
 * before any tracker. A large magnitude means "something is reflecting from
 * about there", not "a target is there".
 *
 * Select it with kw307_set_output(KW307_OUTPUT_NORMAL_WITH_SPD_RG) and receive
 * it with kw307_on_frame_spd_rg().
 */
typedef struct {
    uint16_t       frame_index;     /**< Frame counter. Wraps at 65535 and
                                     *   restarts from 0 on kw307_clear_state(),
                                     *   kw307_radar_init() and
                                     *   kw307_factory_reset(). */
    uint8_t        presence_flag;   /**< 1 = presence detected this frame. */
    KW307Tracker_t moving[2];       /**< Two moving-target slots. */
    KW307Tracker_t stationary[1];   /**< One stationary-target slot. */
    int16_t        moving_speed[2]; /**< As KW307FrameSpd_t: raw units, sign is
                                     *   approaching (+) or receding (-). */
    uint16_t       rg_distance_cm[4];  /**< Distance reported for that gate, cm -
                                        *   the same unit KW307Tracker_t uses, and
                                        *   converted the same way (wire mm / 10).
                                        *   One struct, one unit. */
    int16_t        rg_angle_tenth[4];  /**< Angle, 0.1 degree. */
    uint16_t       rg_mag[4];          /**< Magnitude. The one that moves when the
                                        *   room changes. */
    int16_t        rg_speed[4];        /**< Raw speed units, same scale and sign
                                        *   convention as moving_speed. */
} KW307FrameSpdRg_t;

/* Optional feature modules carry their own output-frame struct + decoder. */

/* ================================================================
   Decode - parse incoming output frame DATA payload
   Returns 0 on success, -1 if len is wrong.
   ================================================================ */
int kw307_dec_frame        (const uint8_t* data, uint16_t len, KW307Frame_t*        out);

/* Parse the default-frame-plus-speed payload (wire CMD 0xF3, 34 bytes). Returns
   0 on success, -1 if the length or the frame's shape bytes are not the
   shape kw307_set_output(KW307_OUTPUT_NORMAL_WITH_SPD) asks for. */
int kw307_dec_frame_spd    (const uint8_t* data, uint16_t len, KW307FrameSpd_t*     out);

/* Parse the same payload with the near stationary gates (wire CMD 0xF3, 66
   bytes). Returns 0 on success, -1 if the length or the frame's shape
   bytes are not the shape
   kw307_set_output(KW307_OUTPUT_NORMAL_WITH_SPD_RG) asks for. */
int kw307_dec_frame_spd_rg (const uint8_t* data, uint16_t len, KW307FrameSpdRg_t*   out);

/* Parse ACK frame: returns STATUS byte, or -1 if not an ACK frame */
int kw307_dec_ack(uint8_t cmd, const uint8_t* data, uint16_t len,
                  uint8_t* acked_cmd_out);

#ifdef __cplusplus
}
#endif

#endif /* KW307_PROTOCOL_H */
