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

/******************************************************************************
 * KW307 SDK - Minimal Application Template
 * File: example_platform/port_template/main.c
 *
 * * Edit to match your MCU's reset / init flow *
 *
 * Presence-triggered application skeleton:
 *   1. on_radar() callback latches the radar's presence_flag into a volatile flag.
 *   2. main loop reads the flag and runs the user-supplied TODO branches.
 *
 * Keep the callback SHORT (it runs in the SDK's UART RX path). Put your
 * reaction code (LED / relay / timer / message send / ...) inside the
 * `if (g_presence_flag)` branch in the main loop, NOT inside on_radar().
 *
 * Build inputs (add to your toolchain):
 *   KW307_SDK/core/KW307_io.c                                 (runtime / IO layer)
 *   KW307_SDK/core/KW307_api.c                                (feature / config layer)
 *   KW307_SDK/core/KW307_api_advanced.c
 *   KW307_SDK/core/KW307_api_tracker_optimize.c
 *   KW307_SDK/core/KW307_output.c
 *   KW307_SDK/core/KW307_cmd.c
 *   KW307_SDK/core/KW307_protocol.c
 *   KW307_SDK/core/KW307_uart.c
 *   KW307_SDK/example_platform/port_template/port_uart.c     (this folder)
 *   KW307_SDK/example_platform/port_template/user_isr.c      (if ISR-driven)
 *   KW307_SDK/example_platform/port_template/main.c          (this file)
 *
 * Include paths:
 *   KW307_SDK/core/
 *   KW307_SDK/example_platform/
 *   KW307_SDK/example_platform/port_template/
 *****************************************************************************/

#include <stdint.h>
#include "KW307_api.h"


/* Demo scene selector — change DEMO_SCENE to retarget the demo
 * configuration applied before the main loop. */
#define DEMO_SCENE_OUTDOOR     0   /* FOV 120, 12 m, hold  5 s              */
#define DEMO_SCENE_OFFICE      1   /* FOV 120, 10 m, hold 11 s              */
#define DEMO_SCENE_DESK        2   /* FOV 120,  2 m, hold 11 s              */
#define DEMO_SCENE_URINAL      3   /* FOV  60, 1.5 m, hold 1 s              */
#define DEMO_SCENE_HAND_DRYER  4   /* FOV  80,  1 m, hold 1 s              */
#define DEMO_SCENE             DEMO_SCENE_OFFICE


/* Application-level state — set from the callback, read in main().
 * `volatile` because the callback may run from inside the UART RX path. */
static volatile uint8_t  g_presence_flag   = 0;
static volatile uint16_t g_target_dist_cm  = 0;   /* nearest moving-target distance, cm */


/* Fires on every radar frame. Keep short — heavy work belongs in main(). */
/* A slot is Valid at 1, moving and stationary alike. On firmware older than
   2.7.6 a moving slot is Valid at 3: change this to 3 there. Never test
   state != 0, and never decide on mag: mag is the magnitude, not a presence
   flag. */
#define MOVING_VALID  1u

static void on_radar(const KW307Frame_t* f)
{
    /* The frame carries the whole result: f->presence_flag, both moving
       targets (f->moving[0], f->moving[1]) and the stationary target
       (f->stationary[0]). This demo uses only the nearest moving target;
       read the others the same way — a slot counts only when it is Valid
       (MOVING_VALID above). */

    g_presence_flag  = f->presence_flag ? 1u : 0u;
    g_target_dist_cm = (f->moving[0].state == MOVING_VALID)
                     ? f->moving[0].distance_cm
                     : 0u;                             /* 0 when no target */

    /* Beyond plain presence — your own rule goes here, where the whole frame is
       available. A target counts when a slot is Valid AND inside the area you care
       about, so latch that instead of (or alongside) the flag:

           g_in_zone = (f->moving[0].state      == MOVING_VALID &&
                        f->moving[0].distance_cm <= 300 &&        -- within 3 m
                        f->moving[0].angle_tenth >= -300 &&       -- -30 .. +30 deg
                        f->moving[0].angle_tenth <=  300) ? 1u : 0u;

       then drive the LED from g_in_zone in the main loop. Copy out whatever fields
       you need here: f is only valid inside this callback. f->presence_flag is the
       module's own decision with the hold time already applied
       (kw307_set_presence_hold_time), which is enough when the position does not matter. */
}


int main(void)
{
    /* (a) Bring up your MCU (clocks, GPIO, NVIC, SysTick, UART peripheral).
     *     UART IRQ must be enabled BEFORE kw307_init() so early frames are
     *     captured.
     *     TODO: call your platform's SystemInit / SYS_Init / board_init().
     *     TODO: e.g. NVIC_EnableIRQ(UART1_IRQn); */


    /* (b) Open the SDK (reads UART settings from user_config.h). */
    if (kw307_init() != KW307_OK) {
        while (1) { /* init failed — halt for debugger */ }
    }

    /* (c) Optional but recommended: check the module's FW version against this SDK's
     *     pairing. 0=compatible, 1=firmware 2.7.6 (older), <0=link error. */
    kw307_check_fw_version();

    /* (d) Apply the demo scene — switch{} below applies its radar configuration. Edit
     *     DEMO_SCENE at top of file to retarget.
     *     The raw kw307_set_parameter() writes are firmware-version specific. */
    kw307_set_output(KW307_OUTPUT_NORMAL);
    switch (DEMO_SCENE) {
    case DEMO_SCENE_OUTDOOR:
        kw307_set_gain               (5);              /* +12 dB */
        kw307_set_detect_range       (50, 1200);       /* 0.5~12 m */
        kw307_set_detect_fov         (120, 8);        /* FOV 120 deg, shrink 8 deg/m */
        kw307_set_motion_sensitivity    (6);
        kw307_set_stationary_sensitivity(3);
        kw307_set_presence_hold_time (5 * 20);      /* presence flag hold 5 s (20 frames a second) */
        kw307_set_mag_threshold   (350, 100);       /* threshold: moving 350 / stationary 100 */
        kw307_set_moving_tracker_cnt(32, 8);    /* cnt_limit 32, display_th 8 */
        kw307_set_stationary_tracker_cnt(64, 8);    /* cnt_limit 64, display_th 8 */
        kw307_set_presence_hold_zone(false, 150, 40); /* off */
        kw307_set_stationary_release_time(0, 5 * 20);      /* stationary release off */
        kw307_set_advanced_clutter_rejection(false, 300, 20);
        break;
    case DEMO_SCENE_OFFICE:
        kw307_set_gain               (5);              /* +12 dB */
        kw307_set_detect_range       (50, 1000);       /* 0.5~10 m */
        kw307_set_detect_fov         (120, 8);        /* FOV 120 deg, shrink 8 deg/m */
        kw307_set_motion_sensitivity    (2);
        kw307_set_stationary_sensitivity(1);
        kw307_set_presence_hold_time (11 * 20);     /* presence flag hold 11 s */
        kw307_set_mag_threshold   (350, 100);       /* threshold: moving 350 / stationary 100 */
        kw307_set_moving_tracker_cnt(32, 8);    /* cnt_limit 32, display_th 8 */
        kw307_set_stationary_tracker_cnt(64, 8);    /* cnt_limit 64, display_th 8 */
        kw307_set_presence_hold_zone(true, 150, 40);  /* on, 150 cm, 40 deg */
        kw307_set_stationary_release_time(0, 20 * 20);     /* stationary release off */
        kw307_set_advanced_clutter_rejection(false, 300, 20);
        break;
    case DEMO_SCENE_DESK:
        kw307_set_gain               (3);              /* +6 dB */
        kw307_set_detect_range       (50, 200);        /* 0.5~2 m */
        kw307_set_detect_fov         (120, 8);        /* FOV 120 deg, shrink 8 deg/m */
        kw307_set_motion_sensitivity    (0);
        kw307_set_stationary_sensitivity(0);
        kw307_set_presence_hold_time (11 * 20);     /* presence flag hold 11 s */
        kw307_set_mag_threshold   (350, 100);       /* threshold: moving 350 / stationary 100 */
        kw307_set_moving_tracker_cnt(32, 8);    /* cnt_limit 32, display_th 8 */
        kw307_set_stationary_tracker_cnt(64, 8);    /* cnt_limit 64, display_th 8 */
        kw307_set_presence_hold_zone(true, 150, 40);  /* on, 150 cm, 40 deg */
        kw307_set_stationary_release_time(0, 120 * 20);    /* stationary release off */
        kw307_set_advanced_clutter_rejection(false, 300, 20);
        break;
    case DEMO_SCENE_URINAL:
        kw307_set_gain               (3);              /* +6 dB */
        kw307_set_detect_range       (10, 150);        /* 0.1~1.5 m */
        kw307_set_detect_fov         (60, 5);          /* FOV 60 deg, shrink 5 deg/m */
        kw307_set_motion_sensitivity    (1);
        kw307_set_stationary_sensitivity(1);
        kw307_set_presence_hold_time (1 * 20);      /* presence flag hold 1 s */
        kw307_set_mag_threshold   (350, 100);       /* threshold: moving 350 / stationary 100 */
        kw307_set_moving_tracker_cnt(32, 8);    /* cnt_limit 32, display_th 8 */
        kw307_set_stationary_tracker_cnt(64, 8);    /* cnt_limit 64, display_th 8 */
        kw307_set_presence_hold_zone(true, 150, 40);  /* on, 150 cm, 40 deg */
        kw307_set_stationary_release_time(0, 60 * 20);     /* stationary release off */
        kw307_set_advanced_clutter_rejection(false, 300, 20);
        break;
    case DEMO_SCENE_HAND_DRYER:
        kw307_set_gain               (5);              /* +12 dB */
        kw307_set_detect_range       (10, 100);        /* 0.1~1 m */
        kw307_set_detect_fov         (80, 5);          /* FOV 80 deg, shrink 5 deg/m */
        kw307_set_motion_sensitivity    (-1);        /* any motion, however slow */
        kw307_set_stationary_sensitivity(1);
        kw307_set_presence_hold_time (1 * 20);      /* presence flag hold 1 s */
        kw307_set_mag_threshold   (200, 5000);      /* stationary threshold out of reach:
                                                       this scene is moving-band only */
        kw307_set_moving_tracker_cnt(20, 2);    /* cnt_limit 20, display_th 2 */
        kw307_set_stationary_tracker_cnt(20, 2);    /* cnt_limit 20, display_th 2 */
        kw307_set_presence_hold_zone(false, 150, 40); /* off - nothing stationary to hold */
        kw307_set_stationary_release_time(0, 10 * 20);     /* stationary release off */
        kw307_set_advanced_clutter_rejection(false, 300, 20);
        break;
    }

    /* (e) Register the frame callback. */
    kw307_on_frame(on_radar);


    /* (f) Main loop — drain RX and run the application reaction.
     *     Keep cadence >= 20 Hz so kw307_update() drains the RX buffer fast
     *     enough. */
    while (1) {
        kw307_update();

        if (g_presence_flag) {
            /* TODO: target detected — your reaction here.
             *   e.g. turn on LED / start timer / send signal.
             *   Nearest target distance is in g_target_dist_cm.
             *   For a range / angle limited rule, gate on Valid in on_radar()
             *   instead of using g_presence_flag — see the note there. */
        } else {
            /* TODO: no target — idle / clear output.
             *   e.g. turn off LED / low-power mode. */
        }
    }
}
