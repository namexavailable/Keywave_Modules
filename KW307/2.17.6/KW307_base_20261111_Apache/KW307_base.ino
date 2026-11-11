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
 * KW307 SDK - WiFi Dashboard (ESP32-S3)
 *
 * One-page web UI mapping each KW307 SDK public API call to a button / input,
 * plus a Canvas-based radar fan plot.
 *
 *  ONE DASHBOARD
 *    Four tabs, every item written and read back through the SDK:
 *      General Settings      the 17 settings a customer tunes; ENTER writes
 *                            them and reads them back
 *      Advanced Settings     18 items, one per SDK setter
 *      Tracker Optimization  6 items, one per SDK setter
 *      UART Output           the output mode and frame shape
 *    A write returning OK only says the bytes left the board, so the read-back
 *    is what makes an apply mean anything. The plot also draws what the module
 *    enforces: both range gates as solid boundaries and the FOV shrink as
 *    dashed curves.
 *
 *    The boot banner and the mDNS host carry the build name (app_config.h).
 *
 *  BOARD
 *    ESP32-S3. Nothing in the sketch is tied to that part beyond the two UART
 *    pins below.
 *
 *  REQUIRED LIBRARY
 *    "WebSockets" by Markus Sattler
 *      Sketch > Include Library > Manage Libraries > search "WebSockets"
 *    The dashboard is pushed, not polled: the board sends one 30-byte binary
 *    frame per module frame (~20 Hz) over ws://<board>:81, so the plot moves
 *    when the radar does rather than when a browser timer fires. The settings
 *    panel is still pulled, from /state at 1 Hz, because nothing on it changes
 *    unless someone presses ENTER. If the socket cannot open the page falls
 *    back to polling /data by itself, at the old 10 Hz.
 *
 *    The status bar shows which transport is live (Push / Polling) and the
 *    measured rate beside it, so "is it really 20 Hz" is a thing you read off
 *    the page rather than infer. A "drop N%" figure there means frames are
 *    being lost on the link: frame_index is monotonic, so gaps prove it.
 *
 *  TWO CONNECTION METHODS - pick one in setup() below:
 *    Option A  Access Point:  wifi.connectAP(KW307_AP_SSID, KW307_AP_PASS, ...)
 *                              -> join that SSID, open http://192.168.4.1
 *    Option B  Station mode:  wifi.connectSTA(WIFI_SSID, WIFI_PASS)
 *                              -> open the http://kw307-<board>.local it prints
 *
 *    The SSID, the .local name and the channel are all per-board and come from
 *    board_config.h, so several boards can be powered up at once without
 *    colliding. The board prints both on boot.
 *
 *  WIRING
 *    The pin numbers live in board_config.h, which is also where you switch
 *    boards. Both maps at a glance:
 *
 *                             ESP32-S3 Super Mini    ESP32-C3 LuatOS
 *        Module TX  ->             GPIO 12               GPIO 1      (RADAR_RX_PIN)
 *        Module RX  ->             GPIO 13               GPIO 0      (RADAR_TX_PIN)
 *        GND        ->               GND                   GND
 *        VCC 3V3    ->               3V3                   3V3
 *
 *    On the Super Mini those two sit right under the 3V3/GND pair on the
 *    right-hand header, so all four wires leave from one edge of the board.
 *    On the C3 they are that board's own labelled UART1 pair. The two maps
 *    are NOT interchangeable - 12/13 drive the C3-CORE's on-board LEDs.
 *
 *  All SDK .c / .h files live in this sketch folder so Arduino compiles
 *  them automatically. Each /cmd endpoint on the web side maps to exactly
 *  one kw307_* API function - see KW307WiFi.cpp::_handleCmd.
 *
 *============================================================================
 * READING A FRAME - THE ONE RULE THAT IS EASY TO GET WRONG
 *============================================================================
 *  A target slot counts ONLY when its state means Valid.
 *
 *      NEVER write   if (slot.state != 0)      <-- wrong
 *      NEVER write   if (slot.mag   != 0)      <-- wrong, mag is signal
 *                                                  strength, not presence
 *      ALWAYS write  if (slot.state == <the Valid value>)
 *
 *  Which number is Valid depends on the reporting form the module is in:
 *
 *      form                      moving[0..1]     stationary[0]
 *      ----------------------    -------------    -------------
 *      simple   (module default)  Valid = 1        Valid = 1
 *      advanced (evaluation)      Valid = 3        Valid = 1
 *
 *  The other non-zero values are NOT targets to act on: in the advanced form
 *  1 and 2 are candidates the module has not confirmed, and a stationary 2 is
 *  a still target sitting where a moving one already is. Treating them as real
 *  reports people who are not there, or the same person twice.
 *
 *  The form cannot be assumed either, and there is no test that works for both:
 *  state 1 means Valid in the simple form but "not confirmed yet" in the
 *  advanced one, so reading it the wrong way either hides every moving target
 *  or invents ones that are not there. This sketch never hard-codes 1 or 3.
 *  It settles the question two ways, neither of which costs a byte on the wire:
 *
 *    - if this sketch set the form itself, it already knows;
 *    - otherwise it watches the frames. A moving slot above 1 cannot occur in
 *      the simple form, so the first target that appears proves which form is
 *      running, and the answer corrects itself if anything changes it later.
 *
 *  That third rule is what makes a module someone else left in the advanced
 *  form work on first power-up without anyone noticing. See advForm() in the
 *  dashboard script and _saw_adv in KW307WiFi.h.
 *
 *  For the minimal non-web version of the same rule, see
 *  KW307_SDK_base/example_platform/Arduino_ESP32S3/main.ino (MOVING_VALID).
 *****************************************************************************/

#include "KW307_api.h"
#include "KW307_io.h"     /* kw307_register_output_handler - 0xF3 is an extension point */
#include "port_arduino.h"
#include "app_config.h"
#include "board_config.h"
#include "KW307WiFi.h"

/* ----- Board / pin selection ------------------------------------------- */
/* RADAR_SERIAL, RADAR_RX_PIN and RADAR_TX_PIN all come from board_config.h.
 * Switching boards is one line there, not an edit in here. */

/* ----- WiFi (Option B only) ------------------------------------------- */
#define WIFI_SSID      "YOUR_WIFI"
#define WIFI_PASS      "YOUR_PASSWORD"

KW307WiFi wifi;

/* Latches every incoming frame into the dashboard. Handling frames yourself
 * instead? Read "READING A FRAME" above first. */
static void on_radar(const KW307Frame_t* f)
{
    wifi.feedFrame(f);
}

/* The self-describing 0xF3 frame, latched raw for the Advanced UART Output tab.
 * 0xF0 comes through kw307_on_frame() already decoded; 0xF3 does not, and must
 * not - the page owns that decoder and decoding it twice is how two readings of
 * one format come to disagree. */
static void on_f3(uint8_t cmd, const uint8_t* data, uint16_t len)
{
    wifi.feedF3(cmd, data, len);
}

void setup()
{
    /* The console runs over native USB, so it never touches the radar's UART. */
    Serial.begin(115200);
    Serial.println(F("\n== KW307 WiFi Dashboard [" KW307_BUILD_NAME "] =="));

    /* Which SDK this was built from and which MCU firmware that SDK is paired
     * with. Both are compile-time constants out of KW307_api.h, so this says
     * what went into the binary - it is not a reading of the module on the
     * other end of the UART and makes no claim about it. A board handed over
     * without its build notes still answers "which version is this" on its
     * first line of output.
     *
     * printf rather than a stringified macro: the constants carry a u suffix,
     * so #x would put "2u" on the screen. */
    Serial.printf("[SDK] v%u.%u.%u   built for MCU FW %u.%u.%u\n",
                  KW307_SDK_VERSION_MAJOR, KW307_SDK_VERSION_MINOR,
                  KW307_SDK_VERSION_PATCH,
                  KW307_SDK_FW_MAJOR, KW307_SDK_FW_MINOR, KW307_SDK_FW_PATCH);

    /* Which board this binary was built for, and the two pins that follow from
     * it. A board wired to the other map reads its own pin numbers back here
     * and disagrees with the bench in one line, rather than coming up looking
     * healthy with a radar that never answers. */
    Serial.printf("[BOARD] %s   radar UART: RX=GPIO%d (<- module TX)  TX=GPIO%d (-> module RX)\n",
                  KW307_BOARD_NAME, RADAR_RX_PIN, RADAR_TX_PIN);

    /* 1. Start WiFi. Pick ONE method. The delay lets the 3V3 rail settle
     *    before the WiFi PHY draws its startup current - keep it. */
    delay(500);
    /* SSID and channel come from board_config.h, so two boards on the bench
     * never advertise the same name. The password is shared across all of
     * them - see the note beside KW307_AP_PASS. */
    wifi.connectAP(KW307_AP_SSID, KW307_AP_PASS, KW307_AP_CHANNEL);
    // wifi.connectSTA(WIFI_SSID, WIFI_PASS);

    Serial.print(F("[WiFi] SSID: " KW307_AP_SSID "   Dashboard: "));
    Serial.println(wifi.getURL());

    /* 2. Bind HardwareSerial to the SDK and start it. */
    kw307_port_arduino_begin(RADAR_SERIAL, RADAR_RX_PIN, RADAR_TX_PIN);

    /* 3. Open the SDK. On failure the dashboard just reports NO DATA. */
    if (kw307_init() != KW307_OK)
        Serial.println(F("[SDK] kw307_init failed - radar link unavailable"));

    /* 4. No boot preset - the module keeps its own saved settings, so Save on
     *    the dashboard survives a power cycle. To force one, call kw307_set_*
     *    here plus wifi.setScene(n) to match the UI. */
    Serial.println(F("[MCU] not contacted - press Check FW on the dashboard"));

    /* 5. Register the per-frame callback. */
    kw307_on_frame(on_radar);
    kw307_register_output_handler(0xF3, on_f3);
}

void loop()
{
    kw307_update();    /* drains UART RX + dispatches on_radar callback */
    wifi.tick();       /* services HTTP + mDNS + captive DNS */
}
