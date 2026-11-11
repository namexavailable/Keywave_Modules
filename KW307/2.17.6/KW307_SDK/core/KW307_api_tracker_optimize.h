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
 * KW307 SDK - API, Tracker Optimization
 * FW: 2.11.0
 *
 * How the moving and stationary trackers behave, each tracker's parameters in
 * one call.
 *
 * Included by KW307_api.h - include that one.
 *
 * Each getter waits about KW307_READ_TIMEOUT_MS for the answer - see
 * KW307_api.h.
 */

#ifndef KW307_API_TRACKER_OPTIMIZE_H
#define KW307_API_TRACKER_OPTIMIZE_H

#include <stdint.h>
#include <stdbool.h>
#include "KW307_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What it takes to report a stationary target, and how steadily it is
 *  reported. The counters are set with
 *  kw307_set_stationary_tracker_cnt().
 *
 *    confirm_frames     consecutive frames a stationary target must be seen in
 *                       before it is reported, 1~255. Higher stops furniture and
 *                       reflections being reported; lower reports a target
 *                       that has just stopped moving sooner. Default 3
 *    smoothing          how closely the reported position follows each new
 *                       measurement, 0~1000 (0.000~1.000). Lower holds the
 *                       position steadier. Default 60 (0.06)
 *    match_distance_cm  how far a new sighting may be from where a stationary
 *                       target was last seen and still count as the same
 *                       target, 0~3276. Default 100 */
int kw307_set_stationary_tracker_opt(uint8_t confirm_frames, int16_t smoothing,
                                     uint16_t match_distance_cm);

/** Read it back, on the same scales as the setter. No pointer may be NULL;
 *  all three are written only on KW307_OK. */
int kw307_get_stationary_tracker_opt(uint8_t* confirm_frames, int16_t* smoothing,
                                     uint16_t* match_distance_cm);


/** The moving tracker, every parameter in one call.
 *
 *  Speeds are the module's own unit. At the default sampling rate 500 is
 *  about 0.19 m/s; kw307_set_sample_rate() changes that scale.
 *
 *    confirm_frames  frames a new moving target's speed is added up over
 *                    before it is reported, 1~32. Default 8. Outside 1~32
 *                    returns KW307_ERR_PARAM
 *    min_speed       slowest movement picked up as a moving target.
 *                    Default 250
 *    speed_clip      each frame's speed is capped at this before it is added
 *                    up, so one spike cannot make a target on its own.
 *                    0 = no cap. Default 500
 *    min_speed_sum   total the added-up speed must pass over confirm_frames
 *                    for the target to be reported. 0 = no check.
 *                    Default 2000
 *
 *  Keep speed_clip >= min_speed and
 *  min_speed_sum <= confirm_frames * speed_clip, or a moving
 *  target may never be reported; neither applies when speed_clip is 0.
 *  Nothing here checks them.
 *
 *  kw307_set_motion_sensitivity() sets these same four as a single level. Use
 *  the level unless it lands between two you want; this is where you go when
 *  it does. Reading the level back after setting these reports no level,
 *  because the four no longer match any.
 *
 *  Neither kw307_set_moving_smoothing() below nor the counters in
 *  kw307_set_moving_tracker_cnt() are touched here. */
int kw307_set_motion_threshold(uint8_t confirm_frames, int16_t min_speed,
                               int16_t speed_clip, int16_t min_speed_sum);

/** Read the four back. No pointer may be NULL; all four are written only on
 *  KW307_OK. */
int kw307_get_motion_threshold(uint8_t* confirm_frames, int16_t* min_speed,
                               int16_t* speed_clip, int16_t* min_speed_sum);

/** How closely a moving target's reported position follows each new
 *  measurement, 0~1000 (0.000~1.000). Lower holds the position steadier.
 *  Default 300 (0.3). Nothing else writes it - not the sensitivity level, not the
 *  counters. */
int kw307_set_moving_smoothing(int16_t smoothing);

/** Read it back, on the setter's scale. Written only on KW307_OK. */
int kw307_get_moving_smoothing(int16_t* smoothing);


/** The narrowest the FOV gets as kw307_set_detect_fov()'s shrink
 *  closes it at range, in whole degrees, 0~180. Default 20. */
int kw307_set_fov_min(int16_t fov_deg);

/** Read the minimum FOV back, in whole degrees.
 *  fov_deg must not be NULL, and is only written on KW307_OK. */
int kw307_get_fov_min(int16_t* fov_deg);


/** Line of sight: stop reporting what is hidden behind something nearer.
 *
 *  true   normal (default). A target directly behind a nearer one, on the same
 *         angle, is not reported - the module treats it as something it
 *         cannot actually see
 *  false  every target is reported, whether or not something stands in front
 *         of it
 *
 *  WHY IT IS ON. A target at a metre does not occupy only its own angle;
 *  it scatters, and most of what comes back from a target standing behind it
 *  is that scatter rather than the second target. Report both and you get a
 *  target whose distance is real and whose existence is not. So the module
 *  drops the one behind - and the nearer the front target, the wider the wedge
 *  it clears, because a target at arm's length hides far more of the room than
 *  a target across it.
 *
 *  WHY YOU MIGHT TURN IT OFF. Two targets queueing are two targets, and with
 *  this on you see one. Turn it off when you need every candidate the module
 *  has: counting a line, or looking at what the radar actually returns before
 *  deciding whether to trust a detection.
 *
 *  IT TURNS OFF MORE THAN THAT ONE RULE. The same wedge is how the module
 *  recognises a reflection - an echo appearing behind a moving target, on that
 *  target's angle, is usually that target bounced off something. With line
 *  of sight off those echoes are reported too. Expect more targets than are
 *  actually present, not only the hidden ones you were after.
 *
 *  IT IS KEPT BY kw307_save_settings(), unlike the holds and switches around
 *  it. A module saved with line of sight off comes back up with it off.
 *
 *  THE WEDGE DEPENDS ON HOW FAR THE NEARER TARGET IS: 16 degrees within 1 m,
 *  narrowing to 4 degrees at 3 m and beyond. Two targets further apart in
 *  angle than that are both reported; closer, and the nearer one hides the
 *  other. There is no call to change it.
 *
 *   kw307_en_los(false);
 *   ... read every candidate ...
 *   kw307_en_los(true); */
int kw307_en_los(bool on);

/** Read line of sight back. on must not be NULL; it is written only on
 *  KW307_OK. */
int kw307_get_los(bool* on);

#ifdef __cplusplus
}
#endif

#endif /* KW307_API_TRACKER_OPTIMIZE_H */
