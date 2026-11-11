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
 * KW307 SDK - API, Advanced Settings
 * FW: 2.11.0
 *
 * The receive chain and the update cycle, one call per command. A field
 * that is not a parameter is sent at its default.
 *
 * Included by KW307_api.h - include that one.
 *
 * Every one of these takes effect from the next frame, with no init call.
 * Call kw307_save_settings() to keep it: an unsaved change is lost on a power
 * cycle and on kw307_radar_init(). kw307_factory_reset() returns all of them
 * to their defaults.
 *
 * The background / environment-subtraction switches are never saved - set
 * them again after every power-up.
 *
 * Each getter waits about KW307_READ_TIMEOUT_MS for the answer - see
 * KW307_api.h.
 */

#ifndef KW307_API_ADVANCED_H
#define KW307_API_ADVANCED_H

#include <stdint.h>
#include <stdbool.h>
#include "KW307_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What a getter reports for a value its setter does not offer - set some
 *  other way, or by older firmware. */
#define KW307_VALUE_NONE  0xFFu

/* ---- Doppler filter corners --------------------------------------------
 *
 * Which speeds of movement each band listens to. A high-pass corner drops
 * movement slower than itself; a low-pass corner drops movement faster.
 *
 *   corner  0~7. Each step halves the high-pass corner, so a higher value lets
 *           slower movement through -- and lets more environmental vibration
 *           through with it.
 *   nd      0~3, the downsample factor. Halves the high-pass corner again per
 *           step, exactly as corner does.
 *   (low-pass) corner  0~255. The low-pass corner rises with the value.
 *
 * At the default 2 kHz sampling rate:
 *
 *   high-pass   2000 / 2^corner / 2^(nd + 1) / 2pi   Hz
 *   low-pass    2000 * corner / 256 / 2pi            Hz
 *
 * The high-pass depends on corner + nd and on nothing else, so 4/2 and 6/0 are
 * the same 2.5 Hz. Eight corners and four nd give eleven distinct corner
 * frequencies, each half the one above it:
 *
 *   corner+nd    0    1    2    3    4   5   6    7     8     9    10
 *   Hz          160   80   40   20   10   5  2.5  1.2  0.62  0.31  0.16
 *
 * Reach for nd only once corner is at 7 and the corner still has to come down;
 * that is what the sensitivity levels do.
 *
 * Defaults: moving 3 / 0 / 128 (high-pass 20 Hz, low-pass 160 Hz),
 *                stationary 7 / 2 / 8 (high-pass 0.31 Hz, low-pass 10 Hz).
 * That is motion level 2 and stationary level 1 - the DEMO_SCENE_OFFICE
 * settings the firmware ships, and every field of both rows matches. So the moving band
 * takes 20~160 Hz and the stationary 0.31~10, and the two do not overlap:
 * 10~20 Hz is the roll-off edge of both. Motion level 0, or corner 4, sets
 * the moving band's high-pass to 10 Hz. These are the defaults, not a
 * recommendation.
 *
 * For scale: micro-Doppler such as respiration starts around 0.3 Hz, and macro
 * movement such as walking around 20 Hz.
 *
 * kw307_set_motion_sensitivity() and kw307_set_stationary_sensitivity() set
 * these same corners - a level is a row of them. After setting a corner
 * directly the matching sensitivity getter reports KW307_LEVEL_NONE, unless
 * the values happen to land on a level.
 * ------------------------------------------------------------------------- */

int kw307_set_moving_hpf_corner(uint8_t corner, uint8_t nd);
int kw307_set_moving_lpf_corner(uint8_t corner);
int kw307_set_stationary_hpf_corner(uint8_t corner, uint8_t nd);
int kw307_set_stationary_lpf_corner(uint8_t corner);

/** Read a corner back, on the same scales as the setter. No pointer may be
 *  NULL; each is written only on KW307_OK. */
int kw307_get_moving_hpf_corner(uint8_t* corner, uint8_t* nd);
int kw307_get_moving_lpf_corner(uint8_t* corner);
int kw307_get_stationary_hpf_corner(uint8_t* corner, uint8_t* nd);
int kw307_get_stationary_lpf_corner(uint8_t* corner);


/** Equalizer gain, in the analog receive chain. Set once for a housing; a
 *  wrong value gives a quieter or noisier module, not an error.
 *
 *    index  0 = 0 dB   1 = +6 dB   2 = +9 dB   3 = +12 dB
 *
 *  Default 1 (+6 dB). Anything above 3 returns KW307_ERR_PARAM.
 *  On firmware 2.7.6 kw307_set_gain() puts this back to 1.
 *
 *  Coming from SDK 2.16.0 or earlier: kw307_set_analog_gain(g1, g2) is
 *  kw307_set_equalizer_gain(g1) and kw307_set_baseband_att(g2). */
int kw307_set_equalizer_gain(uint8_t index);

/** Read the equalizer gain back, on the setter's 0~3 scale. index must not be
 *  NULL, and is only written on KW307_OK. */
int kw307_get_equalizer_gain(uint8_t* index);

/** Baseband attenuator, in the analog receive chain. Takes signal out at the
 *  receiver, ahead of both bands, so it quietens the moving and the stationary
 *  band alike.
 *
 *    index  0 = 0 dB   1 = -6 dB   2 = -12 dB
 *
 *  Default 0. Anything above 2 returns KW307_ERR_PARAM.
 *  kw307_set_stationary_sensitivity() levels 7~9 set this to 1 and the
 *  others set it to 0, so set it after the level if you want your own value. */
int kw307_set_baseband_att(uint8_t index);

/** Read the baseband attenuator back, on the setter's 0~2 scale, or
 *  KW307_VALUE_NONE for a value the setter does not offer. index must not be
 *  NULL, and is only written on KW307_OK. */
int kw307_get_baseband_att(uint8_t* index);


/** Digital gain, both bands. Taken after the filters, so unlike
 *  kw307_set_gain() it cannot saturate the front end.
 *
 *    index  0 = 1x   1 = 2x   2 = 4x   3 = 8x
 *
 *  Default 0. Not available on firmware 2.7.6. */
int kw307_set_digital_gain(uint8_t index);

/** Read back the digital gain, on the same 0~3 scale as the setter.
 *  index must not be NULL, and is only written when the call returns KW307_OK.
 *  Not available on firmware 2.7.6. */
int kw307_get_digital_gain(uint8_t* index);


/** Analog equalizer order.
 *    order  1 = 1st order   2 = 2nd order
 *  Default: 2. Not available on firmware 2.7.6. */
int kw307_set_equalizer_order(uint8_t order);

/** Read the equalizer order back: 1, 2, or KW307_VALUE_NONE when the
 *  equalizer is at a combination neither order is.
 *  order must not be NULL, and is only written on KW307_OK.
 *  Not available on firmware 2.7.6. */
int kw307_get_equalizer_order(uint8_t* order);

/** The equalizer as its three bytes, for combinations the two orders do not
 *  name. kw307_set_equalizer_order() is this call with one of two fixed sets.
 *    response   the response byte, 0x00~0x0F.  Default 0x0B
 *    gain_set1  the first gain set.            Default 0xAA
 *    gain_set2  the second gain set.           Default 0x00
 *  All three go together and replace what the order had set, so read first to
 *  change only one. response above 0x0F returns KW307_ERR_PARAM.
 *  Not available on firmware 2.7.6. */
int kw307_set_equalizer_cmd(uint8_t response, uint8_t gain_set1, uint8_t gain_set2);

/** Read the three equalizer bytes back, exactly as the module holds them.
 *  No pointer may be NULL; all three are written only on KW307_OK.
 *  Not available on firmware 2.7.6. */
int kw307_get_equalizer_cmd(uint8_t* response, uint8_t* gain_set1, uint8_t* gain_set2);


/* ---- Static clutter rejection: background switches ----------------------
 *
 * The block the radar uses to reject static clutter -- floors, walls, furniture,
 * the antenna's own TX-to-RX crosstalk, all of which sit at DC -- is controlled
 * in two stages.
 *
 * The first stage acts before the signal is split into the two bands, so each
 * of its two switches acts on both bands:
 *
 *   background update        kw307_en_background_update()
 *   background subtraction   kw307_en_background_subtraction()
 *
 * Both are on by default, and kw307_save_settings() keeps them.
 *
 * The second stage has two switches on each band, independent of each other:
 *
 *   background switch    whether the background estimate keeps adapting
 *   environment          whether that estimate is subtracted from the signal
 *   subtraction switch
 *
 * Each exists on both bands, so there are four calls. These are not saved:
 * power-up, kw307_radar_init() and kw307_factory_reset() all put both bands
 * back to Update with subtraction on. Set them again after each.
 * ------------------------------------------------------------------------- */

/** Background update, first stage. Acts on both bands. On by default.
 *
 *  To see a target that never moves -- a parked car, for example -- turn off
 *  both first-stage switches, this one and kw307_en_background_subtraction().
 *  Either one left on is enough to remove the target. Expect a noisier signal
 *  once they are off.
 *
 *  Changing either switch steps the signal, and the signal needs a moment to
 *  settle afterwards.
 *
 *  Not available on firmware 2.7.6. */
int kw307_en_background_update(bool on);

/** Read background update back. on must not be NULL; it is written only on
 *  KW307_OK. Not available on firmware 2.7.6. */
int kw307_get_background_update(bool* on);

/** Background subtraction, first stage. Acts on both bands.
 *
 *  true   the background is removed from the signal (default)
 *  false  the background stays in the signal
 *
 *  The second switch to turn off to see a target that never moves; see
 *  kw307_en_background_update(). */
int kw307_en_background_subtraction(bool on);

/** Read background subtraction back. on must not be NULL; it is written only
 *  on KW307_OK. */
int kw307_get_background_subtraction(bool* on);


/** Background switch. true = Update (default), false = Freeze.
 *
 *  Update runs the block as a standard Doppler high-pass: the background
 *  estimate tracks the room, so anything that stops moving is learnt and
 *  subtracted away within a few seconds.
 *
 *  Freeze holds the estimate where it is. That extends the high-pass corner
 *  down to DC, so an object that arrives after the freeze and then stands still
 *  -- a car in a bay, a box on a shelf -- stays visible against the background
 *  that was memorised.
 *
 *  Switching between them does not change the output path, so it causes no step
 *  in the reported magnitude and needs no settling wait. */
int kw307_set_moving_background(bool update);
int kw307_set_stationary_background(bool update);

/** Read the background switch back. on must not be NULL; it is written only on
 *  KW307_OK. */
int kw307_get_moving_background(bool* update);
int kw307_get_stationary_background(bool* update);

/** Environment subtraction switch. true = subtract (default), false = raw.
 *
 *  With it on, the background estimate is subtracted and the band reports the
 *  high-passed signal -- the normal way to run.
 *
 *  With it off, nothing is subtracted and the band reports the raw signal,
 *  environment and all. That is how you look at what the environment actually
 *  is, which is the reference for deciding when a background update is due.
 *
 *  Unlike the background switch, this one DOES change the output path, so the
 *  reported magnitude steps when it moves and the signal needs a moment to
 *  settle afterwards. */
int kw307_en_moving_env_subtraction(bool on);
int kw307_en_stationary_env_subtraction(bool on);

/** Read the environment subtraction switch back. on must not be NULL; it is
 *  written only on KW307_OK. */
int kw307_get_moving_env_subtraction(bool* on);
int kw307_get_stationary_env_subtraction(bool* on);

/** Hold the stationary detections while you disturb the stationary band.
 *
 *  true   the module stops re-deciding what is in the stationary band. Every
 *         stationary slot keeps the distance, angle, magnitude and state it
 *         had the moment the hold went on, and keeps reporting them
 *  false  normal (default)
 *
 *  WHAT IT IS FOR. Changing either of the two switches above steps the
 *  stationary signal, and it takes seconds to settle. While it settles the
 *  module can report a stationary target that is not there, or drop one that
 *  is - most often one that has only just appeared. Hold across it and
 *  neither happens.
 *
 *  The moving band is deliberately NOT held. Movement has to keep being
 *  reported, because seeing a target walk in is what tells you to abandon a
 *  measurement of an empty room rather than trust it.
 *
 *  ONE THING STILL GETS THROUGH. A held stationary slot whose position falls
 *  OUTSIDE the detect range and FOV is dropped anyway, within a few frames, hold or
 *  no hold. So set them (kw307_set_detect_range() and kw307_set_detect_fov())
 *  wider than the area you are measuring, and it cannot reach anything. If you
 *  narrow them later, this effect returns without any error being reported.
 *
 *  Turn it off again as soon as the band has settled. Nothing releases it on a
 *  timer: while it is on the stationary output is frozen, so if the host
 *  enables it and then stops communicating, the module keeps reporting the
 *  frozen stationary result. Besides this call, only kw307_radar_init(),
 *  kw307_factory_reset() and a power cycle release it; it is never saved.
 *
 *   kw307_en_stationary_hold(true);
 *   kw307_en_stationary_env_subtraction(false);   step the band
 *   ... measure, then put the band back and let it settle ...
 *   kw307_en_stationary_hold(false);
 *
 *  Not available on firmware 2.7.6. */
int kw307_en_stationary_hold(bool on);

/** Read the stationary hold back. on must not be NULL; it is written only on
 *  KW307_OK. Not available on firmware 2.7.6. */
int kw307_get_stationary_hold(bool* on);


/** Which antennas the module uses, and whether it measures angle.
 *
 *  @warning RF configuration, not a threshold. The values must match the
 *  antennas the board has; a wrong set can stop a working module detecting.
 *
 *    tx_mode     0 = TX1, 1 = TX1 + TX2
 *    rx_mode     0~7, the receiver mode for the module topology
 *    with_angle  true  = distance and angle
 *                false = distance only; the frame's angle_tenth is not
 *                        meaningful
 *
 *  The values for each module topology:
 *
 *    1T1R (configuration 1)   0, 3, false
 *    1T1R (configuration 2)   0, 1, true
 *    2T3R, 4T3R               0, 1, true
 *    4T4R                     0, 0, true
 *    4T6R                     1, 0, true
 *
 *  Anything outside those ranges returns KW307_ERR_PARAM and nothing is sent.
 *  Defaults: 0 / 1 / true.
 *
 *  Not available on firmware 2.7.6.
 *
 *  Coming from SDK 2.17.4 or earlier: the call took (pa_num, rx_ant_num,
 *  with_angle). tx_mode is pa_num - 1. rx_mode for rx_ant_num 1, 2, 3, 4 is
 *  2, 4, 1, 0 with angle, and 3, 1, 0, 6 without. */
int kw307_set_antenna(uint8_t tx_mode, uint8_t rx_mode, bool with_angle);

/** Read the antenna configuration back, on the setter's scales: what you
 *  wrote is what you read. A value outside the setter's range reads as
 *  KW307_VALUE_NONE.
 *  No pointer may be NULL; all three are written only on KW307_OK.
 *  Not available on firmware 2.7.6. */
int kw307_get_antenna(uint8_t* tx_mode, uint8_t* rx_mode, bool* with_angle);

/** Which magnitude algorithm the module uses.
 *
 *    mag_type  0 = the magnitude without angle estimation
 *              1 = the magnitude from the angle estimation
 *
 *  Set it to match kw307_set_antenna(): 0 with with_angle false, 1 with
 *  with_angle true. kw307_set_antenna() does not set it.
 *
 *  Anything above 1 returns KW307_ERR_PARAM and nothing is sent.
 *  Default: 1. */
int kw307_set_mag_type(uint8_t mag_type);

/** Read the magnitude algorithm back, 0 or 1. mag_type must not be NULL, and is
 *  only written on KW307_OK. */
int kw307_get_mag_type(uint8_t* mag_type);


/** Receiver sampling rate. Lower rates see slower movement better and cost
 *  less bandwidth; higher rates resolve faster movement.
 *    sel 0 = 4 kHz   1 = 2 kHz   2 = 1.6 kHz   3 = 1.3 kHz
 *  Default: 1 (2 kHz). */
int kw307_set_sample_rate(uint8_t sel);

/** Read back the sampling rate, on the same 0~3 scale as the setter.
 *  sel must not be NULL, and is only written when the call returns KW307_OK. */
int kw307_get_sample_rate(uint8_t* sel);


/* ---- PLL calibration ---------------------------------------------------
 *
 * The module calibrates its PLL at every power-on, about 17 ms before the
 * first frame is valid, in three phases: F0 (where the sweep starts), AVG
 * (its slope) and ERR (its straightness). No call is needed to start it.
 */

/** true (DEFAULT) runs all three phases at every power-on, and saves nothing.
 *
 *  false runs none of them: from the next power-on the module loads the
 *  calibration saved by kw307_save_settings() - worth having if the product
 *  hard power-cycles the module often. So: set this false, then
 *  kw307_save_settings(), which saves the calibration running at that moment,
 *  provided all three phases converged. If they did not, the next power-on
 *  calibrates fully and saves the result itself.
 *
 *  Not available on firmware 2.7.6. */
int kw307_en_pll_cal_on_boot(bool on);

/** Read the power-on calibration switch back. on must not be NULL, and is
 *  only written on KW307_OK. Not available on firmware 2.7.6. */
int kw307_get_pll_cal_on_boot(bool* on);

/** Periodic re-calibration while running. false (DEFAULT) = off.
 *
 *  It costs a frame per phase: the transmitter is off for 2~9 ms, so no
 *  signal is transmitted during that frame and it carries no valid data.
 *  Turn it on if the module sees a wide temperature range in service. */
int kw307_en_auto_pll_cal(bool on);

/** Read the periodic re-calibration switch back. on must not be NULL, and is
 *  only written on KW307_OK. */
int kw307_get_auto_pll_cal(bool* on);

/** How often the periodic calibration runs, in FRAMES, 1~32767. Default 6000,
 *  which is five minutes at 20 frames a second.
 *
 *  0, or anything above 32767, returns KW307_ERR_PARAM and nothing is sent. */
int kw307_set_auto_pll_cal_interval(uint16_t frames);

/** Read the interval back, in frames. frames must not be NULL, and is only
 *  written on KW307_OK. */
int kw307_get_auto_pll_cal_interval(uint16_t* frames);

/** Whether the periodic calibration includes ERR. false (DEFAULT) = F0 and AVG
 *  only, because the ramp's straightness barely moves with temperature and ERR
 *  is by far the longest of the three phases. */
int kw307_en_auto_pll_err_cal(bool on);

/** Read the ERR switch back. on must not be NULL, and is only written on
 *  KW307_OK. */
int kw307_get_auto_pll_err_cal(bool* on);


#ifdef __cplusplus
}
#endif

#endif /* KW307_API_ADVANCED_H */
