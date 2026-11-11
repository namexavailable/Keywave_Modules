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
 * KW307WiFi.h - WiFi HTTP dashboard wrapper for the KW307 radar (ESP32 family)
 *
 * Serves a one-page dashboard over WiFi:
 *   - a live radar plot showing the targets the module reports
 *   - five module actions, drawn over the plot:
 *       Check FW / Initial / Clear State / Factory Reset / Save Setting
 *   - a settings panel: gain, detection range, field of view, sensitivity,
 *     signal thresholds, trackers, hold time and the feature switches
 *
 * Editing a control only changes what the browser is holding. ENTER then
 * applies the whole panel to the module in one batch. ENTER sits directly
 * under the plot so the radar stays in view while the settings go out.
 *
 * Every setting on the page maps to one public kw307_set_* function from
 * KW307_api.h; _processCommand() in the .cpp is the whole mapping.
 *
 * Scene presets (Outdoor / Office / Desk / Urinal / Hand Dryer) live in the
 * browser. Picking one only fills the panel — nothing reaches the module
 * until ENTER.
 *
 * TRACKER STATE FORM — see "READING A FRAME" in the .ino for the full rule.
 *   A slot holds a target only at its Valid value: moving 1 in the simple form,
 *   3 in the advanced one, stationary 1 in both. Never test state != 0.
 *
 *   The dashboard no longer switches the form (SDK 2.17.6 withdrew PID 0x6B),
 *   so which one is running is only ever seen, at no extra traffic:
 *     _saw_adv         a moving slot above 1 was seen, which the simple form
 *                      cannot produce (feedFrame)
 *   It rides every push frame to the page. It is what lets a module left in
 *   the advanced form, or on firmware older than 2.7.6, work on first
 *   power-up, and is why boot neither writes nor reads anything.
 *
 * USAGE (in your .ino):
 *
 *   KW307WiFi wifi;
 *   static void on_radar(const KW307Frame_t* f) { wifi.feedFrame(f); }
 *
 *   void setup() {
 *       kw307_port_arduino_begin(Serial1, 12, 13);
 *       kw307_init();
 *       kw307_on_frame(on_radar);
 *       wifi.connectAP("KW307-Radar", "12345678");
 *   }
 *
 *   Boot deliberately puts nothing on the UART — not even a version read. The
 *   module is listened to and never touched until the operator presses
 *   Check FW, which unlocks the rest of the page.
 *****************************************************************************/

#ifndef KW307_WIFI_H
#define KW307_WIFI_H

/* Any ESP32 variant the Arduino core supports — nothing below is specific to
 * one. Only the UART pins in the .ino need to match the board. */
#if !defined(ESP32)
#error "KW307WiFi requires an ESP32-family board (ESP32 / S2 / S3 / C3 / C6)."
#endif

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>

/* Frames are PUSHED over a WebSocket, one per module frame (~20 Hz), rather
 * than polled — see _wsPush(). Polling cost what it could not buy back: a
 * request every 100 ms still only sampled half the frames, and each one paid
 * for a fresh TCP connection plus ~700 bytes of JSON that was mostly settings
 * which had not changed. The push frame is 30 bytes and costs no request.
 *
 * Needs "WebSockets" by Markus Sattler:
 *   Sketch > Include Library > Manage Libraries > search "WebSockets"
 *   or https://github.com/Links2004/arduinoWebSockets */
#if !defined(__has_include) || !__has_include(<WebSocketsServer.h>)
#error "Missing library: install 'WebSockets' by Markus Sattler via Sketch > Include Library > Manage Libraries."
#endif
#include <WebSocketsServer.h>

#include "KW307_api.h"

#include "app_config.h"   /* KW307_BUILD_NAME */

/* Since SDK 2.17.1 the presence hold time and the stationary release time take
 * the module's frame count, not seconds. The module streams at 20 Hz (also with
 * adaptive power saving on), so seconds * 20 = frames, as in the SDK examples.
 * At 1 Hz with adaptive saving off a frame is 1 s and this no longer holds. */
#define KW307_FRAMES_PER_SEC  20                                 /* 20 Hz frame rate */
#define KW307_MAX_HOLD_SEC    (32767 / KW307_FRAMES_PER_SEC)     /* SDK max 32767 frames = 1638 s */

/* KW307_BOARD_TAG — which board, so several can be on the bench at once.
 * Optional: this header still works in a sketch folder that has no
 * board_config.h, it just goes back to the single unqualified name. */
#if defined(__has_include)
  #if __has_include("board_config.h")
    #include "board_config.h"
  #endif
#endif

/* The hostname behind http://<name>.local. Two boards answering to the same
 * one is not a warning — mDNS gives the name to whichever replies first, so
 * the link opens the wrong dashboard and looks right while doing it. */
#ifdef KW307_BOARD_TAG
    #define KW307_MDNS_NAME  "kw307-" KW307_BOARD_TAG
#else
    #define KW307_MDNS_NAME  "kw307"
#endif

/* Port 80 stays with the HTTP server, so the socket needs its own. */
#define KW307_WS_PORT    81

class KW307WiFi {
public:
    KW307WiFi();

    /* ----- Lifecycle ------------------------------------------------ */
    void   connectAP (const char* ssid = "KW307-Radar",
                      const char* password = "12345678",
                      uint8_t channel = 11);
    void   connectSTA(const char* ssid, const char* password);
    void   tick();
    String getURL();

    /* ----- Fed by the sketch --------------------------------------- */
    void feedFrame(const KW307Frame_t* f);

    /* Latch one raw 0xF3 frame for the Advanced UART Output tab.
     *
     * Raw, not decoded: that tab's job is to show the frame as it arrived -
     * descriptor, byte layout, what was parsed and what was skipped - and the
     * page already has the decoder. Decoding here and shipping JSON would be a
     * second implementation of the same self-describing format, and the two
     * would stop agreeing. The browser gets the bytes and runs the code it
     * already runs over serial.
     *
     * One frame, no queue: the tab asks what the latest frame looks like, not
     * for a capture of the stream. */
    void feedF3(uint8_t cmd, const uint8_t* data, uint16_t len);
    void setScene(uint8_t mode)    { _scene = mode; }

private:
    WebServer        _server;
    WebSocketsServer _ws;
    DNSServer        _dns;
    bool             _apMode = false;

    /* Push-frame wire format: 30 bytes, little-endian, laid out in _wsPush().
     * The matching decoder is decodeFrame() in the dashboard script — the two
     * must be changed together, so bump VER whenever the layout moves. The page
     * drops a frame whose version it does not recognise rather than reading the
     * old offsets out of new data, which would show plausible wrong targets
     * instead of an obvious failure. */
    static const uint8_t WS_FRAME_VER = 1;
    static const uint8_t WS_FRAME_LEN = 30;

    /* Latest frame snapshot (latched by feedFrame()). _frame_ms timestamps it
     * so /data can report the frame's age: _have_frame alone only says "a frame
     * arrived at some point", which would let a dead link keep showing the last
     * reading as if it were current. */
    volatile KW307Frame_t _frame;
    volatile uint8_t      _have_frame = 0;
    volatile uint32_t     _frame_ms   = 0;

    /* 0xF3 is self-describing and variable-length. The widest it gets is both
     * range-gate bands (12 gates x 4 quantities x 2 bytes x 2 = 192), the five
     * header bytes, both endpoints, four full stationary trackers, two full
     * moving ones with their speeds, and the room peak - about 270. 320 leaves
     * room without pretending to a number the protocol states anywhere. A frame
     * longer than this is dropped rather than truncated: half a frame decodes
     * into something that looks like data.
     *
     * _f3_seq is what the page polls - it asks whether the frame it last drew
     * is still the newest, instead of re-parsing the same bytes every second. */
    static const uint16_t F3_CAP = 320;
    volatile uint8_t      _f3[F3_CAP];
    volatile uint16_t     _f3_len = 0;
    volatile uint32_t     _f3_seq = 0;
    volatile uint32_t     _f3_ms  = 0;

    /* Which reporting form the frames actually arrive in, worked out by
     * watching them rather than by asking the module. In the simple form a
     * moving slot only ever reports 0 or 1; 2 and 3 exist solely in the
     * advanced form, so seeing either is proof — no false positive is
     * possible. Set in feedFrame(), cleared whenever something may have
     * changed the form, and re-latched on the next target, so it corrects
     * itself without costing a single byte on the wire. */
    volatile uint8_t      _saw_adv    = 0;

    /* When the form was last disturbed, for the settling window in feedFrame().
     * Clearing _saw_adv is not enough on its own: the module only changes form
     * from its next frame, and frames it had already sent are still in the RX
     * buffer, so those stale ones re-latch the flag we just cleared. _saw_adv
     * never expires, so that one frame would pin the display to the old form
     * for good. 0 = never disturbed. */
    volatile uint32_t     _form_change_ms = 0;

    /* How long to stay deaf. 5 frames at the module's 20 Hz — enough to cover
     * the frame in flight, the "from the next frame" changeover and whatever
     * the RX buffer was holding, while staying far below the time it takes an
     * operator to notice anything. */
    static const uint32_t FORM_SETTLE_MS = 250u;

    /* Arm the window; call after anything that may have changed the form. */
    void _formDisturbed() { _form_change_ms = millis(); }
    /* False while the window is open. */
    bool _settled() const
    { return _form_change_ms == 0u || (millis() - _form_change_ms) > FORM_SETTLE_MS; }

    /* FW version read by the "Check FW" dashboard button — the only place it
     * comes from, since boot no longer reads it. 0/0/0 means never asked;
     * major == 0xFF means asked and the module did not answer. The page tells
     * those two apart: pressing the button unlocks the main actions either way,
     * while Clear State and the tracker-state form need a real version. */
    uint8_t _fw_major = 0;
    uint8_t _fw_minor = 0;
    uint8_t _fw_patch = 0;

    /* Mirror state — matches the dashboard's UI state, NOT a read-back of the
     * module. Seeded with the OFFICE scene preset (scene 1), which is also what
     * the dashboard's combos default to. The sketch does not push a boot preset
     * (the module keeps its own saved settings), so these values only become
     * authoritative once the user presses ENTER. */
    uint8_t  _scene          = 1;
    uint8_t  _gain           = 5;       /* +12 dB */
    uint16_t _range_min_cm   = 50;
    uint16_t _range_max_cm   = 1000;
    int16_t  _fov_deg10      = 1200;
    uint8_t  _shrink_deg10   = 80;      /* 8 deg / 100 cm */
    int16_t  _signal_moving      = 350;
    int16_t  _signal_stationary  = 100;
    uint16_t _hold_sec       = 11;

    /* Motion / Stationary combo indices (UI display + lookup key).
     * Level 0 = most sensitive ... 9 = least sensitive. */
    int8_t  _motion_idx     = 2;   /* L2; -1 = any motion, one step below L0 */
    uint8_t _stationary_idx = 1;   /* L1 */

    /* ---- Rest of the settings panel ---- */
    bool     _force_sta_on      = true;
    int16_t  _force_sta_dist_cm = 150;
    int16_t  _force_sta_fov_deg = 40;
    uint8_t  _max_sta_en        = 0;
    uint16_t _max_sta_sec       = 20;
    uint8_t  _mov_trk_max       = 32;
    uint8_t  _mov_trk_show      = 8;
    uint8_t  _sta_trk_max       = 64;

    /* RangeGate IIR, on kw307_set_RangeGate_IIR()'s 0~1000 scale (150 = 0.15,
     * the firmware's own default for both). Held only as the fallback for a
     * half-written rgiir command - nothing on the page reads these back, so
     * they stay out of the once-a-second /state JSON. */
    uint16_t _rg_iir_n          = 150;
    uint16_t _rg_iir_s          = 150;
    uint8_t  _sta_trk_show      = 8;
    bool     _clutter_en        = false; /* repetitive-motion (fan / AC) rejection */
    int16_t  _clutter_spd       = 300;
    int16_t  _clutter_dev       = 20;    /* cm */

    /* Feature switches: Power Down / presence output pin / tracker form. */
    uint8_t  _power_mode        = 1;     /* 0 Down, 1 Normal, 2 PA Off */
    bool     _gpio_flag         = true;  /* presence output pin follows the flag */
    bool     _rssi_report       = false; /* PID 0x6C, off out of the box */

    /* Wall-clock budget per read in the verify pass, enforced against millis()
     * in _readPid() / _readCmdAny() rather than handed to kw307_read_blocking()
     * — that function's "timeout_ms" is a poll-iteration count. See the note at
     * READ_SLICE for what an iteration actually costs on this port.
     *
     * Sized to permit exactly two attempts at READ_SLICE (160 + 160 = 320, so
     * the third never starts). The second exists because a leftover answer from
     * an earlier read returns kw307_read_blocking() early holding the wrong PID:
     * the first attempt can be spent on someone else's reply even though this
     * read was going to succeed.
     *
     * Every read is synchronous, so this also bounds how long loop() stops
     * servicing frames. The shipping verify pass is 20 parameter reads and the
     * expert one 24 plus 2 commands, so a silent module stalls loop() for six to
     * eight seconds; a live one answers in 50~100 ms per read and the pass is
     * over in a second or two. */
    static const uint32_t VERIFY_BUDGET_MS = 300u;

    /* The whole-bank read waits once, and for longer.
     *
     * A single register is one SPI transfer and answers inside the verify
     * budget. A bank is a 54 or 64 register burst, and the module only reaches
     * a command in the gap between radar frames - the transfer is under 10 ms
     * at 115200 either way, what varies is how long until the module gets to
     * it. READ ALL failed on both banks while single reads worked, which is
     * that difference.
     *
     * Passed as BOTH the budget and the slice, so the retry loop in
     * _readCmdAny() runs exactly once: a retry re-SENDS, and re-sending makes
     * the module start the burst again just as it was about to answer the
     * first one. Raising the budget alone turned one slow request into five
     * competing ones. The UART page, which has no board in between and sees
     * the same module, sends this command once and waits 700 ms.
     *
     * The cost is that loop() stops servicing frames for up to 700 ms when a
     * module does not answer at all. READ ALL is a button someone presses, not
     * something on a timer, so that is a pause rather than a stall. */
    static const uint32_t BANK_READ_WAIT_MS = 700u;

    /* HTTP route handlers. */
    void _startServer();
    void _startMDNS();
    void _handleRoot();
    void _handleData();
    void _handleF3();       /* the latest raw 0xF3 frame, as hex */
    void _handleState();
    void _handleCmd();

    /* Every tab's settings as the module holds them (?t= picks the tab), one SDK
     * getter per item, in the units the verbs take. */
    void _handleGet();


    /* Serialise the latched frame and broadcast it to every socket client.
     * Called straight from feedFrame(), which is safe because the SDK runs the
     * frame callback in the caller's context (KW307_io.h) — that is loop(), not
     * an ISR, so the socket write needs no queue in between. */
    void _wsPush();

    /* Parse / dispatch a "verb:args" POST body. */
    void _processCommand(const String& cmd);
    /* What /cmd answers: "err" when a verb that checks its setter saw it fail
     * (antenna, so the page forgets what it last sent), "ok" otherwise. */
    int _cmdRc = 0;

    static int _parseInt(const String& s, int fallback);

    /* The embedded dashboard HTML (defined at the bottom of the .cpp). */
    static const char _dashboardHtml[] PROGMEM;
};

#endif /* KW307_WIFI_H */
