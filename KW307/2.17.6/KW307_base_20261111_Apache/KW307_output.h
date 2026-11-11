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
 * KW307 SDK - Custom output frame (wire CMD 0xF3)
 * FW: 2.11.0
 *
 * Frame 0xF3 carries whichever blocks you ask for. Build the request one block
 * at a time, send it once, and receive every shape through one callback.
 * Not available on firmware 2.7.6.
 *
 *   KW307OutputConfig_t c;
 *   kw307_out_clear(&c);
 *   kw307_out_presence(&c, true);
 *   kw307_out_moving_trk(&c, true, true, true);    // full records + speed
 *   kw307_out_rg(&c, 0, 5, false, true, false);    // stationary band, gates 0~5
 *   kw307_on_frame_f3(on_f3);                      // before switching
 *   kw307_set_output_config(&c);
 *
 * The kw307_out_* block calls only edit c. Nothing reaches the module until
 * kw307_set_output_config(), which sends the whole shape in one write. To
 * change one block of a running shape, change it in c and send c again.
 *
 * The named modes are this with a fixed shape - KW307_OUTPUT_NORMAL_WITH_SPD
 * is kw307_out_normal_with_spd(), and so on. Start from one, change what you
 * need, send it:
 *
 *   kw307_out_normal_with_spd_rg(&c);
 *   kw307_out_rg(&c, 0, 7, false, true, false);    // same, gates 0~7
 *   kw307_set_output_config(&c);
 *
 * Every frame repeats its shape bytes at the start, and
 * kw307_dec_frame_f3() decodes from that, not from what was last requested -
 * so a frame still in flight from the previous shape decodes correctly too,
 * and a frame whose length disagrees with its own shape is refused rather than
 * mis-read.
 *
 * Names follow one rule: `moving` and `stationary` are the two bands, `_trk`
 * is a tracker. `stationary[]` (no suffix) is the stationary-target slot the
 * default frame has always called stationary[0].
 */

#ifndef KW307_OUTPUT_H
#define KW307_OUTPUT_H

#include <stdint.h>
#include <stdbool.h>
#include "KW307_protocol.h"     /* KW307Tracker_t */
#include "KW307_io.h"           /* KW307OutputHandler_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
   What to send
   ================================================================ */

/** Range gates, each about 1 m. Gate numbers run 0~11. */
#define KW307_OUT_RG_NUM   12u

/** The shape of one 0xF3 frame. Fill it with the kw307_out_* calls below, or
 *  write the fields directly - kw307_set_output_config() checks it either way.
 *  A block that is off sends nothing, and its sub-options are ignored. */
typedef struct {
    bool presence;              /**< presence flag, 1 byte */
    struct {
        bool on;                /**< the two moving trackers */
        bool full;              /**< 8 bytes each; false = state byte only */
        bool speed;             /**< adds each one's mean speed, 2 bytes each */
    } moving_trk;
    struct {
        bool nearest;           /**< nearest stationary target, 8 bytes */
        bool second;            /**< second-nearest, 8 bytes */
    } stationary;
    struct {
        bool on;                /**< the four stationary-band trackers */
        bool full;              /**< 8 bytes each; false = state byte only */
    } stationary_trk;
    bool room_max;              /**< peak magnitude in the room, per band, 4 bytes */
    struct {
        bool    moving;         /**< moving band's gates */
        bool    stationary;     /**< stationary band's gates */
        uint8_t start_rg;       /**< first gate, 0~11 */
        uint8_t end_rg;         /**< last gate, start_rg~11 */
        bool    mag_only;       /**< magnitude only; false = distance, angle,
                                 *   magnitude and speed for every gate */
    } rg;
} KW307OutputConfig_t;

/* ---- One call per block. Each edits c only; NULL c is ignored. ---- */

/** Every block off. Start here, or from one of the named shapes below. */
void kw307_out_clear(KW307OutputConfig_t* c);

/** The presence flag. */
void kw307_out_presence(KW307OutputConfig_t* c, bool on);

/** The two moving trackers. full: whole 8-byte records (distance, angle,
 *  magnitude, state, counter) rather than the state byte alone. speed: add each
 *  tracker's mean speed - raw units, positive = approaching. */
void kw307_out_moving_trk(KW307OutputConfig_t* c, bool on, bool full, bool speed);

/** The stationary-target slots - nearest, and second-nearest. The same slot the
 *  default frame carries as stationary[0]. */
void kw307_out_stationary(KW307OutputConfig_t* c, bool nearest, bool second);

/** The four stationary-band trackers, with their raw state: 0 Empty,
 *  1 Checking, 2 Birth, 3 Valid. A target forming shows here before
 *  it reaches stationary[] - which is what board tuning wants, and what a
 *  product does not. */
void kw307_out_stationary_trk(KW307OutputConfig_t* c, bool on, bool full);

/** The room's peak magnitude per band: the same reading
 *  kw307_en_noise_floor_report() puts in an empty slot, but sent in every
 *  frame, even while a target is present. */
void kw307_out_room_max(KW307OutputConfig_t* c, bool on);

/** Raw range-gate readings, gates start_rg~end_rg, per band. Readings are
 *  taken before grouping and before any tracker. Both bands false turns the
 *  block off. KW307_ERR_PARAM, and c unchanged, on a gate above 11 or
 *  start_rg > end_rg. */
int  kw307_out_rg(KW307OutputConfig_t* c, uint8_t start_rg, uint8_t end_rg,
                  bool moving_band, bool stationary_band, bool mag_only);

/* ---- The named shapes, as starting points ---- */

/** KW307_OUTPUT_NORMAL_WITH_SPD: presence, moving trackers in full with speed,
 *  nearest stationary target. 34 bytes. */
void kw307_out_normal_with_spd(KW307OutputConfig_t* c);

/** KW307_OUTPUT_NORMAL_WITH_SPD_RG: the above plus the stationary band,
 *  gates 0~3, all four quantities. 66 bytes. */
void kw307_out_normal_with_spd_rg(KW307OutputConfig_t* c);

/* ---- Size and send ---- */

/** Frame length, in payload bytes, that c will produce - 6 to 270. 0 when c is
 *  NULL or would be refused by kw307_set_output_config(). Worth checking
 *  against the link: at about 20 frames a second a 270-byte frame is about
 *  5.4 KB a second, which wants 115200 baud and fills a 256-byte
 *  KW307_UART_RX_BUFSIZE in about 45 ms - call kw307_update() more often than
 *  that, or enlarge it. */
uint16_t kw307_out_len(const KW307OutputConfig_t* c);

/** Switch the periodic output to frame 0xF3, shaped by c, in one write.
 *  KW307_ERR_PARAM when c is NULL, nothing is selected, or the gate range is
 *  invalid - checked here rather than left to the module, which would clamp it
 *  without saying so.
 *
 *  The module does not keep the shape across a power cycle: it comes back on
 *  the default frame (0xF0). Send it again after a reset. */
int kw307_set_output_config(const KW307OutputConfig_t* c);

/** Gates start_rg~end_rg of both bands, all four quantities, plus the two
 *  moving trackers and/or the four stationary-band trackers in full. The view
 *  for tuning a board: what each gate sees, and what the trackers made of it.
 *  (0, 11, false, false) is the full range-gate profile, 197 bytes.
 *  KW307_ERR_PARAM on a gate above 11 or start_rg > end_rg. */
int kw307_set_output_rg(uint8_t start_rg, uint8_t end_rg,
                        bool en_moving_trk, bool en_stationary_trk);

/* ================================================================
   What arrives
   ================================================================ */

/** Bits of KW307FrameF3_t.blocks - which blocks this frame carries. */
#define KW307_F3_RG_MOVING          0x01u
#define KW307_F3_RG_STATIONARY      0x02u
#define KW307_F3_PRESENCE           0x04u
#define KW307_F3_STATIONARY_NEAREST 0x08u
#define KW307_F3_STATIONARY_SECOND  0x10u
#define KW307_F3_MOVING_TRK         0x20u
#define KW307_F3_STATIONARY_TRK     0x40u
#define KW307_F3_ROOM_MAX           0x80u

/** Bits of KW307FrameF3_t.format - how much of each block. */
#define KW307_F3_MOVING_TRK_FULL     0x01u
#define KW307_F3_STATIONARY_TRK_FULL 0x02u
#define KW307_F3_RG_MAG_ONLY         0x04u
#define KW307_F3_MOVING_SPEED        0x08u

/** Band index of KW307FrameF3_t.rg[gate][band]. */
#define KW307_BAND_MOVING      0u
#define KW307_BAND_STATIONARY  1u

/** One range gate's reading in one band. */
typedef struct {
    uint16_t distance_cm;   /**< cm, converted like KW307Tracker_t (wire mm / 10) */
    int16_t  angle_tenth;   /**< 0.1 degree */
    uint16_t mag;           /**< magnitude; 0 for a gate beyond the maximum
                             *   distance of kw307_set_detect_range() (gate 11
                             *   at the default 10 m). Distance, angle and speed
                             *   are still reported there. */
    int16_t  speed;         /**< raw units, positive = approaching */
} KW307Gate_t;

/** One decoded 0xF3 frame, whatever its shape.
 *
 *  Every field exists in every frame; what the frame did not carry is zero,
 *  and `blocks` says which is which - test the KW307_F3_* bit, not the value.
 *  A tracker sent as state only has its state filled and the rest zero.
 *
 *  rg[] is indexed by the real gate number, so rg[3] is gate 3 whatever range
 *  was selected; gates outside start_rg~end_rg are zero. With
 *  KW307_F3_RG_MAG_ONLY only .mag is filled. */
typedef struct {
    uint16_t       frame_index;       /**< running frame counter */
    uint8_t        blocks;            /**< KW307_F3_* block bits, as the module sent them */
    uint8_t        format;            /**< KW307_F3_* format bits, as the module sent them */
    uint8_t        start_rg;          /**< gate range of rg[], as the module sent it */
    uint8_t        end_rg;

    uint8_t        presence_flag;     /**< 1 = presence detected */
    KW307Tracker_t moving_trk[2];     /**< same record and state as KW307Frame_t.moving[] */
    int16_t        moving_speed[2];   /**< as KW307FrameSpd_t.moving_speed */
    KW307Tracker_t stationary[2];     /**< [0] nearest, [1] second; [0] is
                                       *   KW307Frame_t.stationary[0] */
    KW307Tracker_t stationary_trk[4]; /**< state is raw: 0 Empty, 1 Checking,
                                       *   2 Birth, 3 Valid */
    uint16_t       room_max[2];       /**< [KW307_BAND_MOVING], [KW307_BAND_STATIONARY] */
    KW307Gate_t    rg[KW307_OUT_RG_NUM][2]; /**< [gate][KW307_BAND_*] */
} KW307FrameF3_t;

/** Decode any 0xF3 payload. 0 on success; -1 when data or out is NULL, or the
 *  length is not the one the frame's own shape bytes call for. */
int kw307_dec_frame_f3(const uint8_t* data, uint16_t len, KW307FrameF3_t* out);

/** Receive every 0xF3 frame as a KW307FrameF3_t. Pass NULL to unregister.
 *
 *  Works alongside kw307_on_frame_spd() / kw307_on_frame_spd_rg(): a frame of
 *  their exact shape goes to them when they are registered, and everything
 *  else comes here. The frame pointer is valid for the length of the call. */
void kw307_on_frame_f3(void (*cb)(const KW307FrameF3_t*));

/* SDK-internal: the three shape bytes c sends (rg_sel, blocks, format), for an
 * add-on whose decoder checks an arriving frame against the shape it asked for.
 * 0, or -1 when kw307_set_output_config() would refuse c. */
int kw307_i_out_pack(const KW307OutputConfig_t* c, uint8_t shape[3]);

/* SDK-internal: hand every 0xF3 frame of exactly this shape to h, through the
 * one 0xF3 handler rather than in place of it, so the callbacks above keep
 * working alongside an add-on's. KW307_OK, or KW307_ERR_PARAM on a NULL
 * argument or a full table. */
int kw307_i_out_on_shape(const uint8_t shape[3], KW307OutputHandler_t h);

#ifdef __cplusplus
}
#endif

#endif /* KW307_OUTPUT_H */
