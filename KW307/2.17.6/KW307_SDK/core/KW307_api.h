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
 * KW307 SDK - API
 * FW: 2.11.0
 *
 * The one header to include. It pulls in the other two API groups and the
 * runtime layer, so a single
 *
 *   #include "KW307_api.h"
 *
 * gives the whole SDK:
 *
 *   KW307_api.h                   system, output (default frame and output
 *                                 modes), device settings, register access
 *   KW307_api_advanced.h          the receive chain and the update cycle
 *   KW307_api_tracker_optimize.h  how the trackers behave
 *   KW307_io.h                    init / update / on_frame / read_blocking
 *
 * Add every .c in core/ to your build.
 *
 *   Sec.1  System         firmware version / factory reset / save / radar re-init /
 *                         clear detection state
 *   Sec.2  Output         the default frame and the output modes
 *   Sec.3  Device config  gain / range / FOV / sensitivity / trackers / hold / power
 *   Sec.4  Registers      direct register reads and writes
 *
 * Typical startup:
 *   kw307_init();
 *   kw307_check_fw_version();
 *   // apply your settings here (see example_platform/<port>/main.c)
 *   kw307_on_frame(on_radar);
 *   while (1) kw307_update();
 *
 * Every setter returns KW307_OK on success, or a negative KW307_ERR_* code
 * (see KW307_io.h). Arguments out of range return KW307_ERR_PARAM.
 */

#ifndef KW307_API_H
#define KW307_API_H

#include <stdint.h>
#include <stdbool.h>
#include "KW307_io.h"
#include "KW307_protocol.h"
#include "KW307_cmd.h"      /* raw access: kw307_send_cmd / read_cmd / set_parameter */
#include "KW307_api_advanced.h"
#include "KW307_api_tracker_optimize.h"
#include "KW307_output.h"       /* custom frame 0xF3, block by block */

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
   Sec.1  System
   ================================================================ */

/* KW307 SDK version — the version of THIS SDK source, independent of the paired
 * module firmware version below. Bump on any API or behaviour change. */
#define KW307_SDK_VERSION_MAJOR  2u
#define KW307_SDK_VERSION_MINOR  17u
#define KW307_SDK_VERSION_PATCH  6u

/* Module firmware this SDK release is paired with — the baseline
 * kw307_check_fw_version() measures against. Run the check before applying
 * settings: on firmware 2.7.6 it returns 1. Once it has found 2.7.6, the
 * calls marked "Not available on firmware 2.7.6" return
 * KW307_ERR_NOT_SUPPORTED and send nothing.
 *
 * UPGRADING A MODULE FROM 2.7.6 TO 2.11 OR NEWER RESETS ITS SAVED SETTINGS -
 * gain, filters, thresholds, antenna, all of it. Read your settings out
 * before flashing, write them back afterwards, and save them again. */
#define KW307_SDK_FW_MAJOR  2u
#define KW307_SDK_FW_MINOR  11u
#define KW307_SDK_FW_PATCH  0u

/** Re-initialise the radar (~130 ms, blocking).
 *
 *  Settings return to what kw307_save_settings() last saved (or to the
 *  defaults if nothing was saved): anything changed and not saved is
 *  DISCARDED. No setter needs this call to take effect - use it only to
 *  recover, and use kw307_clear_state() to drop stale targets instead.
 *
 *  The output mode is left as it is; the power mode returns to NORMAL and
 *  kw307_en_stationary_hold() is released. */
int kw307_radar_init(void);

/** Clear saved settings and return every setting to its default.
 *
 *  This includes the output mode (back to KW307_OUTPUT_NORMAL - call
 *  kw307_set_output() again if you use another), the power mode (NORMAL) and
 *  kw307_en_gpio_presence_flag() (on). */
int kw307_factory_reset(void);

/** Persist the current settings to flash; restored automatically on next
 *  power-up and by kw307_radar_init(). Don't call in a loop — each call uses
 *  a flash write cycle.
 *
 *  Saving takes the module up to about 1 s, and a command sent before it has
 *  finished can be lost. Wait that long before the next call, or send the
 *  save with kw307_write_blocking(KW307_CMD_SAVE_SETTINGS, ...) and a
 *  1000 ms timeout, which returns as soon as the save is done.
 *
 *  Not saved: the four background / environment-subtraction switches,
 *  kw307_en_gpio_presence_flag(), kw307_en_stationary_hold(), the output mode
 *  and the power mode. Set them again after every power-up. */
int kw307_save_settings(void);

/**
 * Drop the targets the module is holding and reset the presence flag, so detection
 * starts over; settings are kept. Targets take a moment to reappear.
 */
int kw307_clear_state(void);

/** Check the connected module's firmware is compatible with this SDK: at or above
 *  the paired baseline KW307_SDK_FW_MAJOR.MINOR.PATCH.
 *  Waits about KW307_FW_CHECK_TIMEOUT_MS (300 ms) for the answer.
 *    0   compatible
 *    1   firmware older than the baseline
 *   <0   link error (no response / UART down) */
int kw307_check_fw_version(void);


/* ================================================================
   Sec.2  Output
   ================================================================ */
/** Output modes for kw307_set_output() / kw307_get_output().
 *
 *  Three shapes, and off.
 *
 *  Any other shape of frame 0xF3 - pick the blocks, the range gates, how much
 *  of each - is a custom frame, built with KW307_output.h and received through
 *  kw307_on_frame_f3(), which decodes every shape. The two 0xF3 modes below
 *  are two fixed shapes of it, and kw307_out_normal_with_spd() /
 *  kw307_out_normal_with_spd_rg() hand you either as a starting point.
 *
 *  The two 0xF3 modes are not available on firmware 2.7.6. */
#define KW307_OUTPUT_OFF      0u   /* nothing sent; the radar keeps running */
#define KW307_OUTPUT_NORMAL   1u   /* the default frame (0xF0) */
#define KW307_OUTPUT_NORMAL_WITH_SPD 4u /* the default frame + moving speed (0xF3) */
#define KW307_OUTPUT_NORMAL_WITH_SPD_RG 5u /* + the near stationary gates (0xF3) */

/** kw307_get_output() only. The module is sending one of the shapes that
 *  share frame 0xF3, and the wire cannot say which - read the frame, whose
 *  first bytes describe it exactly. Never pass this to kw307_set_output(). */
#define KW307_OUTPUT_EXTENDED 6u

/** What the module sends.
 *
 *    KW307_OUTPUT_OFF     stop the periodic stream; the radar keeps running
 *    KW307_OUTPUT_NORMAL  the default frame - presence + 2 moving + 1 stationary
 *                         target (consume with kw307_on_frame), 27 bytes.
 *                         The smallest frame, and the only one firmware 2.7.6
 *                         sends.
 *    KW307_OUTPUT_NORMAL_WITH_SPD
 *                         everything KW307_OUTPUT_NORMAL carries, plus how fast
 *                         each moving target is going, frame 0xF3, 34 bytes:
 *                           [0~1]     frame index
 *                           [2~4]     0xB0 0x2C 0x09 - the shape bytes
 *                           [5]       presence flag
 *                           [6~21]    moving[0], moving[1], 8 bytes each
 *                           [22~25]   speed of moving[0], then moving[1]
 *                           [26~33]   the stationary-target slot
 *                         all 16-bit big-endian. Bytes [5~21] and [26~33] are
 *                         byte-for-byte the default frame's body, which is why
 *                         the same tracker reader works on both.
 *                         Receive it with kw307_on_frame_spd().
 *    KW307_OUTPUT_NORMAL_WITH_SPD_RG
 *                         the above, plus the stationary band of gates 0~3 in
 *                         full, frame 0xF3, 66 bytes:
 *                           [0~1]     frame index
 *                           [2~4]     0x30 0x2E 0x09 - the shape bytes
 *                           [5]       presence flag
 *                           [6~21]    moving[0], moving[1], 8 bytes each
 *                           [22~25]   speed of moving[0], then moving[1]
 *                           [26~33]   the stationary-target slot
 *                           [34~41]   distance, mm   [4 gates] (struct: cm)
 *                           [42~49]   angle, 0.1 deg [4]
 *                           [50~57]   magnitude      [4]
 *                           [58~65]   speed          [4]
 *                         all 16-bit big-endian, stationary band only.
 *                         Receive it with kw307_on_frame_spd_rg().
 *
 *                         The gates are raw readings from the stationary
 *                         band, one per range gate, before grouping and
 *                         before any tracker - the band's own view rather than
 *                         the tracker's conclusion. For calibration. Bytes
 *                         [5~33] are the mode above, unchanged, so the targets
 *                         do not stop being reported while you look.
 *
 *                         It costs 32 bytes a frame over the mode above, so it
 *                         is worth switching into for a measurement and back
 *                         out again rather than sitting in.
 *
 *  Any other value returns KW307_ERR_PARAM. Default: KW307_OUTPUT_NORMAL. */
int kw307_set_output(uint8_t mode);

/** Is the module sending, and on which frame. Three answers, and each one is
 *  what the module itself said:
 *
 *    KW307_OUTPUT_OFF       not sending. The radar is still running.
 *    KW307_OUTPUT_NORMAL    the default 27-byte frame.
 *    KW307_OUTPUT_EXTENDED  one of the shapes that share frame 0xF3.
 *
 *  mode must not be NULL, and is only written on KW307_OK. Anything else the
 *  module answers reads back as KW307_VALUE_NONE.
 *
 *  IT DOES NOT TELL YOU WHICH 0xF3 SHAPE: every one of those shapes reads
 *  back the same. The shape is described in the first bytes of every 0xF3
 *  frame instead.
 *
 *  So do not call this to decide how to parse. Register the callbacks and let
 *  each frame pick its own; that answer is carried by the frame it describes
 *  and cannot disagree with it.
 *
 *  What this is for is the question a frame cannot answer, because when the
 *  answer is OFF there are no frames: nothing is arriving - is the module
 *  switched off, or is the link down? */
int kw307_get_output(uint8_t* mode);

/* ----------------------------------------------------------------
   One receive call per mode. Register the one that matches the mode you
   selected - before you select it, or the first frames arrive with nowhere
   to go - and keep it registered:

     KW307_OUTPUT_NORMAL
       void kw307_on_frame       (void (*cb)(const KW307Frame_t*));
     KW307_OUTPUT_NORMAL_WITH_SPD
       void kw307_on_frame_spd   (void (*cb)(const KW307FrameSpd_t*));
     KW307_OUTPUT_NORMAL_WITH_SPD_RG
       void kw307_on_frame_spd_rg(void (*cb)(const KW307FrameSpdRg_t*));
     a custom frame (KW307_output.h), or any 0xF3 shape at all
       void kw307_on_frame_f3    (void (*cb)(const KW307FrameF3_t*));

   Your callback is a plain function with that shape, and you pass its name:

     static void on_radar(const KW307FrameSpd_t* f) { ... }
     kw307_on_frame_spd(on_radar);        <- the NAME, not on_radar()

   The frame pointer is valid only for the length of the call - copy out what
   you need. The call happens inside kw307_update(), so keep it short.

   kw307_on_frame() is declared in KW307_io.h, which this header includes, so
   including KW307_api.h is enough.

   Registering all four is fine and is the simple thing to do: only the
   selected mode's frames are ever sent, and a frame of the speed or gate
   mode's exact shape goes to its own callback, so switching modes then needs
   nothing from you.
   ---------------------------------------------------------------- */

/** Register a callback for the default-frame-plus-speed stream, decoded into a
 *  KW307FrameSpd_t. Pass NULL to unregister. Fires from kw307_update() while
 *  KW307_OUTPUT_NORMAL_WITH_SPD is selected.
 *
 *  Not available on firmware 2.7.6. */
void kw307_on_frame_spd(void (*cb)(const KW307FrameSpd_t*));

/** Receive KW307_OUTPUT_NORMAL_WITH_SPD_RG frames, decoded into a
 *  KW307FrameSpdRg_t.
 *
 *  This does NOT replace kw307_on_frame_spd(): keep both registered. Each
 *  frame goes to the callback that matches its own shape, so you can switch
 *  into this mode for a calibration and back out without swapping callbacks.
 *
 *  Not available on firmware 2.7.6. */
void kw307_on_frame_spd_rg(void (*cb)(const KW307FrameSpdRg_t*));

/* ----------------------------------------------------------------
   The default frame (wire CMD 0xF0) — layout and how to read it

   >>> Display a slot only when its state is Valid (1); do not use `mag`.

   kw307_on_frame() hands you one decoded KW307Frame_t, ~20 per second:

     +---------------+------------------------------------------------+
     | frame_index   | running frame counter                          |
     +---------------+------------------------------------------------+
     | presence_flag | 1 = target present / 0 = no target.            |
     |               | Held over short dropouts                       |
     |               | (kw307_set_presence_hold_time),                |
     |               | so for a plain presence output THIS FLAG ALONE |
     |               | is enough — the slots are only needed when you |
     |               | want target positions.                         |
     +---------------+------------------------------------------------+
     | moving[0]     | distance_cm  angle_tenth  mag  state  counter  |
     | moving[1]     | distance_cm  angle_tenth  mag  state  counter  |
     | stationary[0] | distance_cm  angle_tenth  mag  state  counter  |
     +---------------+------------------------------------------------+
                                                 ^^^^^
                          which value is Valid (= the only one to display):

      moving[0..1]      |  Valid  = 1
      stationary[0]     |  Valid  = 1
      empty slot        |  Empty  = 0  (all other fields zeroed as well)

     Test for Valid (`state == 1`), not `state != 0`: on firmware older than
     2.7.6 a moving target is Valid at 3, and 1 and 2 are not yet confirmed.
     Never test `mag != 0`: `mag` is the magnitude, not a presence flag.
   ---------------------------------------------------------------- */

/* ================================================================
   Sec.3  Device config
   ================================================================ */

/* ---- Reading a setting back ------------------------------------
 *
 * Each kw307_get_*() below asks the module what it is currently using and waits
 * for the reply, so unlike the setters it takes a few milliseconds to return.
 * Do not call one from inside your frame callback.
 *
 * The reply is what the module is actually running - not an echo of what you
 * sent. That is what makes these worth calling: if a setting was not applied,
 * the readback shows it.
 *
 * Each one waits about KW307_READ_TIMEOUT_MS (200 ms) for the answer, then
 * returns KW307_ERR_TIMEOUT; the module normally answers well inside that. A
 * setting you have just written can be read back straight away.
 *
 * Only settings this SDK can also write have a getter. ---------------- */

/** What the two sensitivity getters report when the module's settings match no
 *  level - it has been tuned field by field rather than by level. */
#define KW307_LEVEL_NONE  ((int8_t)-128)

/** Baseband gain - the main amplification step in the analog receive chain,
 *  ahead of both bands. The equalizer gain and the baseband attenuator are
 *  separate settings.
 *    index 1~8: 0 / +3 / +6 / +9 / +12 / +15 / +18 / +21 dB.
 *  Default: 5 (+12 dB).
 *
 *  On firmware 2.7.6 the baseband gain and the equalizer gain share one setting:
 *  setting the baseband gain puts the equalizer gain back to 1, and while the
 *  equalizer gain is not 1, kw307_get_gain() fails. */
int kw307_set_gain(uint8_t index);

/** Read back the baseband gain the module is using, on the same 1~8 scale as the setter.
 *  index must not be NULL, and is only written when the call returns KW307_OK. */
int kw307_get_gain(uint8_t* index);

/** Detect range - the near and far limits, in cm.
 *    min_cm : near limit (e.g. 50 = 0.5 m).  max_cm : far limit (e.g. 1000 = 10 m).
 *  Defaults: 50 / 1000. min_cm must be < max_cm, and max_cm may not exceed
 *  1200 (12 m, the module's full range). */
int kw307_set_detect_range(uint16_t min_cm, uint16_t max_cm);

/** Read back the detect range, in cm, as the setter takes it.
 *  Neither pointer may be NULL; both are only written on KW307_OK. */
int kw307_get_detect_range(uint16_t* min_cm, uint16_t* max_cm);

/** Field of view (FOV) - how wide the radar sees in front.
 *    fov_deg          FOV in whole degrees, 1~180. Default 120
 *    shrink_rate_deg  how many degrees it narrows per metre, 0~25. Default 8,
 *                     which squares the cone off into a near-rectangle */
int kw307_set_detect_fov(int16_t fov_deg, uint8_t shrink_rate_deg);

/** Read back the field of view and its shrink rate, in whole degrees, as the
 *  setter takes them. Neither pointer may be NULL; both are only written on
 *  KW307_OK. */
int kw307_get_detect_fov(int16_t* fov_deg, uint8_t* shrink_rate_deg);

/** Motion sensitivity. Moving band only.
 *    0 ~ 9 : 0 most sensitive, 9 least
 *       -1 : one step below 0 - responds to any motion however slow, for a
 *            target that must be caught the instant it appears
 *  Any other value returns KW307_ERR_PARAM.
 *  Default: 2.
 *
 *  Coming from SDK 2.5 or earlier: this call took (level, byp_spd), and the
 *  level was ignored whenever byp_spd was set. Replace byp_spd = 1 with
 *  level = -1; leave every other level as it is. kw307_get_motion_sensitivity()
 *  loses its byp_spd pointer for the same reason - -1 is what it reports now.
 *  Both sensitivity levels are int8_t so KW307_LEVEL_NONE fits; a level held
 *  in a uint8_t still passes unchanged. */
int kw307_set_motion_sensitivity(int8_t level);

/** Read back which motion level the module is running: 0~9, -1, or
 *  KW307_LEVEL_NONE when the moving band matches no level. Naming a level
 *  needs every value the setter wrote, so a later
 *  kw307_set_motion_threshold() or kw307_set_*_corner() call that changed
 *  one of them reads as no level.
 *  level must not be NULL, and is only written on KW307_OK. */
int kw307_get_motion_sensitivity(int8_t* level);

/** Stationary-target sensitivity as a single level.
 *    0~9 : most-sensitive (0, catches breathing-rate motion) -> least (9)
 *  There is no -1 here: the level is the whole range, and anything outside
 *  0~9 returns KW307_ERR_PARAM. Default: 1.
 *
 *  A level sets the stationary band's two filter corners, and for 7~9 it takes
 *  6 dB out at the receiver - kw307_set_baseband_att() step 1. It does NOT
 *  change the detection thresholds; only kw307_set_mag_threshold() does.
 *
 *  The receiver's gain is ahead of the band split, so 7~9 quieten the MOVING
 *  band by the same 6 dB. If that matters, stay at 6 or below and set the
 *  threshold yourself.
 *
 *  Coming from SDK 2.9.0 or earlier: 7~9 used to double the detection thresholds
 *  instead (350/100 -> 700/200). A product that relied on that should set
 *  kw307_set_mag_threshold() explicitly. */
int kw307_set_stationary_sensitivity(int8_t level);

/** Read back which stationary level the module is running: 0~9, or
 *  KW307_LEVEL_NONE when the stationary band matches no level. Levels 6 and 7
 *  are the same filter and differ only in that 6 dB, so a module whose baseband
 *  attenuator was set by hand to something neither level uses reads as no
 *  level.
 *  level must not be NULL, and is only written on KW307_OK. */
int kw307_get_stationary_sensitivity(int8_t* level);

/** Presence hold time: how long the presence flag stays set after the last
 *  confirmed target, so a brief dropout does not clear it. The flag sets in
 *  the same frame a target is confirmed; the hold time does not delay that.
 *  frames : 1~32767 frames (32767 = about 1638 s, 27 min); 0 acts as 1.
 *           Above that returns KW307_ERR_PARAM.
 *  Default: 220 frames (11 s).
 *
 *  Normal operation is 20 Hz, so 1 s = 20 frames. */
int kw307_set_presence_hold_time(uint16_t frames);

/** Read back the presence hold time, in frames, as the setter takes it.
 *  frames must not be NULL, and is only written on KW307_OK. */
int kw307_get_presence_hold_time(uint16_t* frames);

/** Detection threshold - the magnitude a moving or stationary target must
 *  reach. Higher = less sensitive. Range per band 0~32767.
 *    baseline_moving     : moving-band threshold      (default 350)
 *    baseline_stationary : stationary-band threshold  (default 100) */
int kw307_set_mag_threshold(int16_t baseline_moving, int16_t baseline_stationary);

/** Read back the two detection thresholds the setter takes.
 *
 *  Both are written by kw307_set_mag_threshold() and by nothing else - a
 *  sensitivity level does not change them - so what you read here is what
 *  you last set here.
 *
 *  Neither pointer may be NULL; both are only written on KW307_OK. */
int kw307_get_mag_threshold(int16_t* baseline_moving,
                            int16_t* baseline_stationary);

/** Tune the moving-target tracker counters.
 *    cnt_limit  : how long a moving target survives unseen,
 *                 in frames (20 frames a second). Default 32.
 *    display_th : response time of a moving target.
 *                 The target becomes Valid once its counter reaches display_th.
 *                 Default 8. Must be <= cnt_limit. */
int kw307_set_moving_tracker_cnt(uint8_t cnt_limit, uint8_t display_th);

/** Read back the moving-target tracker counters, in frames, as the setter takes
 *  them. Neither pointer may be NULL; both are only written on KW307_OK. */
int kw307_get_moving_tracker_cnt(uint8_t* cnt_limit, uint8_t* display_th);

/** Tune the stationary-target tracker counters.
 *  Same meanings as the moving tracker;
 * Defaults: cnt_limit 64, display_th 8.
 *
 *  cnt_limit is how long a stationary target stays reported after it stops
 *  being seen, in frames (64 = about 3.2 s at 20 frames a second). A target
 *  that is still seen but has not moved for a long time is released by
 *  kw307_set_stationary_release_time() instead. */
int kw307_set_stationary_tracker_cnt(uint8_t cnt_limit, uint8_t display_th);

/** Read back the stationary-target tracker counters, in frames, as the setter
 *  takes them. Neither pointer may be NULL; both are only written on KW307_OK. */
int kw307_get_stationary_tracker_cnt(uint8_t* cnt_limit, uint8_t* display_th);

/** RangeGate IIR - the per-band average over frames of each range gate's
 *  complex data, ahead of the extraction. 0~1000 (0.000~1.000): higher
 *  follows each new measurement more closely, lower holds the previous
 *  reading harder.
 *    moving, stationary : default 150 (0.15) for both.
 *  One coefficient covers all four quantities - the module applies it to the
 *  I/Q data behind magnitude, distance, angle and speed alike - so this is
 *  not the tracker's smoothing (kw307_set_moving_smoothing() and
 *  kw307_set_stationary_tracker_opt()), which acts on the reported track.
 *  Lower it if a distance reading is jumpy at a fixed range. */
int kw307_set_RangeGate_IIR(int16_t moving, int16_t stationary);

/** Read both RangeGate IIR values back, on the same 0~1000 scale.
 *  Neither pointer may be NULL; both are written only on KW307_OK. */
int kw307_get_RangeGate_IIR(int16_t* moving, int16_t* stationary);

/** Hold presence on while a stationary target stands inside a near forward
 *  zone, so brief dropouts do not make it flicker.
 *
 *    on          enable / disable
 *    distance_cm zone depth, 0~3276
 *    fov_deg     zone width edge to edge, 0~180 (60 = +/- 30 deg ahead)
 *
 *  Defaults: true, 150, 40.
 *
 *  The spatial half of a pair. kw307_set_presence_hold_time() holds presence
 *  for a while AFTER the last sighting; this holds it WHILE a target is
 *  standing in one particular place - close, and in front - which is the case
 *  the moving-target detector is worst at, because there is nothing moving.
 *
 *  It only ever adds presence, never removes it, and only when nothing else
 *  has found a target.
 *
 *  ONE MORE EFFECT, worth knowing before you widen the zone. When the zone is
 *  what is holding presence on, the stationary target that triggered it is also
 *  reported in the stationary slot - but only when no ordinary stationary
 *  target is being reported there already. So presence and position stay
 *  consistent: the module does not say a target is there and then leave the
 *  slot empty. */
int kw307_set_presence_hold_zone(bool on, int16_t distance_cm, int16_t fov_deg);

/** Read back the presence hold zone: whether it is on, and the cm / whole
 *  degrees the setter takes. No pointer may be NULL; all three are only written
 *  on KW307_OK. */
int kw307_get_presence_hold_zone(bool* on, int16_t* distance_cm, int16_t* fov_deg);

/** How long a stationary target keeps being reported before its slot is
 *  released.
 *    enable  : 1 = enforce the cap, 0 = never release on time alone.
 *    frames  : 0~32767 frames (32767 = about 1638 s, 27 min); above that
 *              returns KW307_ERR_PARAM. Higher for offices/lounges, lower
 *              for fast-cycle scenes (urinals, hand dryers).
 *  Default: disabled, 400 frames (20 s).
 *  1 s = 20 frames, as for kw307_set_presence_hold_time().
 *
 *  The third of the three that decide how long the module goes on saying
 *  a target is there, and the only one that ENDS it. The other two extend it:
 *  kw307_set_presence_hold_time() holds on after the last sighting, and
 *  kw307_set_presence_hold_zone() holds on while a target stays close and in
 *  front. This one releases a target that has stayed still long enough to be
 *  treated as part of the room.
 *
 *  It caps the REPORTING, not the target: a target that stays put past the
 *  limit is released and then detected again on its next movement.
 *
 *  Tuning: presence stays "occupied" too long after the target left -> shorten;
 *  target no longer reported while still there -> lengthen, or set
 *  enable = 0. */
int kw307_set_stationary_release_time(uint8_t enable, uint16_t frames);

/** Read back the stationary release time: whether it is on, and its length in
 *  frames, as the setter takes them. Neither pointer may be NULL; both are
 *  only written on KW307_OK. */
int kw307_get_stationary_release_time(uint8_t* enable, uint16_t* frames);

/** Reject repetitive motion (AC, machinery, ...) so it is not
 *  reported as presence.
 *    en     : true = on, false = report every moving target (default).
 *    spd_th : motion threshold — higher = reject more. Range 0~1000, default 300.
 *    dev_th : allowed drift in cm — higher = reject more. Recommended range
 *             0~50, default 20. Negative values or values above 6553
 *             return KW307_ERR_PARAM. */
int kw307_set_advanced_clutter_rejection(bool en, int16_t spd_th, int16_t dev_th);

/** Read back the advanced clutter rejection settings the setter takes.
 *  No pointer may be NULL; all three are only written on KW307_OK. */
int kw307_get_advanced_clutter_rejection(bool* en, int16_t* spd_th,
                                         int16_t* dev_th);

/** Power mode. One of KW307_POWER_DOWN / _NORMAL / _PA_OFF.
 *
 *    DOWN    lowest current; sensing and frame output stop. The module still
 *            accepts commands, but sensing resumes only on
 *            kw307_set_power_mode(KW307_POWER_NORMAL). Make that the first
 *            call after DOWN; other settings and getters are not reliable
 *            until then.
 *    NORMAL  sensing
 *    PA_OFF  transmit off, the rest still running: it receives and answers, and
 *            detects nothing. Return through NORMAL, not through DOWN.
 *
 *  Default: NORMAL. Any other value returns KW307_ERR_PARAM. */
int kw307_set_power_mode(uint8_t mode);

/** Read back the power mode, as the setter takes it.
 *  mode must not be NULL, and is only written on KW307_OK.
 *  On firmware 2.7.6 this reports the mode last requested. */
int kw307_get_power_mode(uint8_t* mode);

/** Report the room's noise floor - its peak magnitude - in an empty slot, to
 *  judge signal-to-noise against. kw307_out_room_max() sends the same reading
 *  in every frame of a custom frame.
 *    true   while no target is detected, the first moving slot and the nearest
 *           stationary slot carry the room's peak magnitude in their mag
 *           instead of 0 - what a real target has to stand out from
 *    false  an empty slot reports mag 0 (default)
 *
 *  Three limits to keep in mind when reading the value:
 *    - it reports only while the presence flag is 0, and it gates on TARGETS,
 *      not on objects. A chair left in the room does not stop the reading, it
 *      becomes the reading
 *    - only those two slots can carry it. The measurement is per band rather
 *      than per slot, so the others stay 0
 *    - it is the room's PEAK, not its average
 *
 *  Use `state`, never `mag`, to tell whether a slot holds a target: with this on
 *  an empty slot has a non-zero magnitude. It is a commissioning instrument -
 *  measure the room, set kw307_set_mag_threshold(), turn it off again. */
int kw307_en_noise_floor_report(bool on);

/** Read back whether empty slots report the noise floor.
 *  on must not be NULL, and is only written on KW307_OK. */
int kw307_get_noise_floor_report(bool* on);

/** Presence output pin on / off.
 *    true   the pin follows the presence flag (default)
 *    false  the pin is held low
 *  Switching it back on uses the presence the module already holds, so a target
 *  already in front drives the pin high on the next frame. Not kept by
 *  kw307_save_settings(). */
int kw307_en_gpio_presence_flag(bool on);

/** Read back whether the presence output pin is following the presence flag.
 *  on must not be NULL, and is only written on KW307_OK. */
int kw307_get_gpio_presence_flag(bool* on);

/* ================================================================
   Sec.4  Registers
   ================================================================ */

/* ---- Engineer register access ----------------------------------
 *
 * The radar chip's own registers, one at a time. Every other call in this SDK
 * ends up in these registers, so writing one by hand can undo a setting made
 * through a call above, and nothing here checks what a value means. Read
 * before writing, and keep what was there.
 * ---------------------------------------------------------------- */

/** The two register banks. */
#define KW307_DSP_REG_FIRST      200u
#define KW307_DSP_REG_LAST       253u
#define KW307_ANALOG_REG_FIRST   48u
#define KW307_ANALOG_REG_LAST    111u

/** Write one DSP register. addr KW307_DSP_REG_FIRST~_LAST; anything else
 *  returns KW307_ERR_PARAM. Default: each register's own value in the chip's
 *  init table - there is no one default, so read it first. */
int kw307_set_dsp_reg(uint8_t addr, uint8_t value);

/** Read one DSP register back. value must not be NULL, and is only written on
 *  KW307_OK. */
int kw307_get_dsp_reg(uint8_t addr, uint8_t* value);

/** Write one analog register. addr KW307_ANALOG_REG_FIRST~_LAST; anything
 *  else returns KW307_ERR_PARAM. Default: each register's own value in the
 *  chip's init table - there is no one default, so read it first. */
int kw307_set_analog_reg(uint8_t addr, uint8_t value);

/** Read one analog register back. value must not be NULL, and is only
 *  written on KW307_OK. */
int kw307_get_analog_reg(uint8_t addr, uint8_t* value);

#ifdef __cplusplus
}
#endif

#endif /* KW307_API_H */
