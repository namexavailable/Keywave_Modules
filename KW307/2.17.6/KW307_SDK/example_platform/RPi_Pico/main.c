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
 * KW307 SDK - Raspberry Pi Pico demo
 * File: example_platform/RPi_Pico/main.c
 *
 * The onboard LED follows presence, and every transition is printed over USB
 * serial. Start from this file for your own application.
 *
 * Build + flash: see README.md in this folder.
 */

#include <stdio.h>

#include "pico/stdlib.h"

#include "KW307_api.h"        /* pulls in KW307_io.h + KW307_protocol.h */
#include "user_config.h"

/* Demo scene selector — change DEMO_SCENE to retarget the demo
 * configuration applied before the main loop. */
#define DEMO_SCENE_OUTDOOR     0   /* FOV 120, 12 m, hold  5 s              */
#define DEMO_SCENE_OFFICE      1   /* FOV 120, 10 m, hold 11 s              */
#define DEMO_SCENE_DESK        2   /* FOV 120,  2 m, hold 11 s              */
#define DEMO_SCENE_URINAL      3   /* FOV  60, 1.5 m, hold 1 s              */
#define DEMO_SCENE_HAND_DRYER  4   /* FOV  80,  1 m, hold 1 s              */
#define DEMO_SCENE             DEMO_SCENE_OFFICE

/* Pico / Pico 2 onboard LED. On a Pico W the LED hangs off the wireless chip
 * instead, so there is no PICO_DEFAULT_LED_PIN — use the cyw43 driver there, or
 * just watch the USB output. */
#ifdef PICO_DEFAULT_LED_PIN
#define LED_PIN PICO_DEFAULT_LED_PIN
#endif

/* Set by the frame callback, read by the main loop. `volatile` because the
 * callback runs from inside the SDK's receive path. */
static volatile bool     g_present = false;
static volatile uint16_t g_dist_cm = 0;
static volatile int16_t  g_angle_tenth = 0;
static volatile uint16_t g_fc = 0;

/* A slot is Valid at 1, moving and stationary alike. On firmware older than
 * 2.7.6 a moving slot is Valid at 3: change this to 3 there. Never test
 * state != 0, and never decide on mag: mag is the magnitude, not a presence
 * flag. */
#define MOVING_VALID  1u

/* Fires on every radar frame. Keep it short — assign and return. Do NOT call
 * kw307_* from in here: the blocking calls would re-enter the receive path. */
static void on_radar(const KW307Frame_t* f)
{
    g_present = (f->presence_flag != 0);
    g_fc      = f->frame_index;

    if (f->moving[0].state == MOVING_VALID) {
        g_dist_cm     = f->moving[0].distance_cm;
        g_angle_tenth = f->moving[0].angle_tenth;
    }
}

int main(void)
{
    bool     prev = false;
    bool     first = true;
    uint8_t  maj = 0, min_ = 0, pat = 0;

    stdio_init_all();          /* USB serial — see CMakeLists.txt */

#ifdef LED_PIN
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);
#endif

    /* Give a host terminal a moment to attach before the first prints, or the
     * startup lines vanish into a port nobody is listening to yet. */
    sleep_ms(2000);
    printf("\nKW307 SDK Pico demo\n");

    if (kw307_init() != KW307_OK) {
        printf("  kw307_init failed — check user_config.h (UART + pins)\n");
        while (1) tight_loop_contents();
    }

    /* A UART that initialises proves nothing about the module: an unpowered or
     * miswired one looks identical. CMD 0x01 is answered by every firmware, so
     * asking for the version is the real liveness check. */
    if (kw307_cmd_read_fw_version(500, &maj, &min_, &pat) != KW307_OK) {
        printf("  no answer from the module. Check:\n"
               "   - 5V on VBUS (pin 40) and a common GND\n"
               "   - TX/RX crossed: GP%d -> module RXD, GP%d <- module TXD\n"
               "   - baud matches (user_config.h)\n",
               KW307_UART_TX_PIN, KW307_UART_RX_PIN);
        while (1) tight_loop_contents();
    }
    printf("  module firmware v%u.%u.%u\n",
           (unsigned)maj, (unsigned)min_, (unsigned)pat);
    if (kw307_check_fw_version() == 1)
        printf("  note: firmware older than this SDK's paired baseline\n");

    /* Apply the demo scene. Edit DEMO_SCENE at the top of the file to
     * retarget. */
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

    kw307_on_frame(on_radar);
    printf("  streaming\n\n");

    while (true) {
        kw307_update();                     /* drains the ring, fires on_radar */

        if (first || g_present != prev) {
            first = false;
            prev  = g_present;
#ifdef LED_PIN
            gpio_put(LED_PIN, g_present ? 1 : 0);
#endif
            if (g_present)
                printf("PRESENT  dist=%4u cm  angle=%+5.1f deg   (fc=%u)\n",
                       (unsigned)g_dist_cm, g_angle_tenth / 10.0f,
                       (unsigned)g_fc);
            else
                printf("ABSENT                                   (fc=%u)\n",
                       (unsigned)g_fc);
        }

        /* The module streams ~20 frames a second into a 256-byte ring, so the
         * pump must stay well above that. 5 ms is ~200 Hz and leaves the core
         * almost entirely free for your own work. */
        sleep_ms(5);
    }
}
