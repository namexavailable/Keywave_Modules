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
 * KW307WiFi.cpp - ESP32 WiFi dashboard implementation
 *
 * The radar frame is PUSHED; everything else is pulled.
 *
 *   ws://<board>:81   one 30-byte binary frame per module frame (~20 Hz),
 *                     broadcast from _wsPush() the moment feedFrame() lands
 *   GET  /            the dashboard page (embedded at the bottom of this file)
 *   GET  /state       JSON: settings mirror + FW version. Slow-moving, so the
 *                     page pulls it at 1 Hz and again right after any write
 *   GET  /data        JSON: frame + settings in one blob. Kept only as the
 *                     fallback the page polls when the socket will not open
 *   POST /cmd         one "verb:args" line, applied to the module
 *
 * The split is what makes 20 Hz affordable. /data has to carry the settings
 * with every frame because it is one blob, and they are ~85% of it while
 * changing only when someone presses ENTER. The push frame carries just the six
 * things a frame can change, so the fast path costs 30 bytes and no request.
 *
 * Editing a control on the page changes browser-side state only. ENTER then
 * sends the whole panel as a batch of /cmd lines. Each verb in
 * _processCommand() below calls one kw307_* function from KW307_api.h.
 *****************************************************************************/
#include "KW307WiFi.h"
#include "KW307_cmd.h"   /* kw307_cmd_read_fw_version */
#include "KW307_api_advanced.h"          /* kw307_set_digital_gain */
#include "KW307_api_tracker_optimize.h"  /* kw307_set_moving_smoothing */
/* KW307_AP_TX_POWER, for boards whose antenna needs the PA backed off, arrives
 * through KW307WiFi.h - which pulls in board_config.h when the sketch folder
 * has one, and builds fine when it does not. */
#include <string.h>


KW307WiFi::KW307WiFi() : _server(80), _ws(KW307_WS_PORT) {}

/* ============================================================
   Push-frame serialisation helpers (little-endian, matching the
   browser's DataView(..., true) reads)
   ============================================================ */
static inline void _le16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

/* One 8-byte target slot. Written field by field rather than memcpy'd off
 * KW307Tracker_t: the struct is #pragma pack(1) and would copy correctly today,
 * but then adding a field to the SDK struct would silently shift every offset
 * the browser reads, and wrong distances look like real targets. */
static void _packSlot(uint8_t* p, const KW307Tracker_t& t)
{
    _le16(p + 0, t.distance_cm);
    _le16(p + 2, (uint16_t)t.angle_tenth);   /* two's complement; read back as Int16 */
    _le16(p + 4, t.mag);
    p[6] = t.state;
    p[7] = t.counter;
}


/* ============================================================
   Lifecycle
   ============================================================ */
void KW307WiFi::connectAP(const char* ssid, const char* password, uint8_t channel)
{
    /* Report what the AP interface actually does. Without this a client that
     * cannot associate looks identical to one that never tried: the sketch
     * carries on either way and the console stays silent. The sequence for a
     * healthy join is AP_START -> STACONNECTED -> STAIPASSIGNED. */
    WiFi.onEvent([](arduino_event_id_t ev, arduino_event_info_t info) {
        (void)info;
        switch (ev) {
            case ARDUINO_EVENT_WIFI_AP_START:          Serial.println(F("[WiFi] AP interface up"));      break;
            case ARDUINO_EVENT_WIFI_AP_STOP:           Serial.println(F("[WiFi] AP interface down"));    break;
            case ARDUINO_EVENT_WIFI_AP_STACONNECTED:   Serial.println(F("[WiFi] client associated"));    break;
            case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:Serial.println(F("[WiFi] client left"));          break;
            case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:  Serial.println(F("[WiFi] client got an IP"));     break;
            default: break;
        }
    });

    WiFi.mode(WIFI_AP);

    /* Modem sleep off. It is on by default and parks the radio between beacons,
     * which adds tens of milliseconds of jitter to anything the board sends —
     * enough to make a 50 ms push cadence arrive in bursts however promptly
     * _wsPush() hands the frame over. Costs idle current, which a mains-powered
     * dashboard is not counting. */
    WiFi.setSleep(false);

    /* Check the result. softAPIP() is not proof of anything: WiFi.mode() alone
     * creates the AP interface and gives it the default 192.168.4.1, so the
     * address prints happily even when the radio never started advertising. */
    bool ok = WiFi.softAP(ssid, password, channel);

    /* TX power cap, and the order matters: softAP() resets the PA to maximum,
     * so this has to come after it or it is silently undone. Boards that need
     * it define KW307_AP_TX_POWER in board_config.h and say there why; boards
     * with a healthy antenna leave it undefined and keep their range. */
#ifdef KW307_AP_TX_POWER
    WiFi.setTxPower(KW307_AP_TX_POWER);
#endif

    _apMode = true;
    delay(200);
    _dns.start(53, "*", WiFi.softAPIP());
    _startServer();
    _startMDNS();

    Serial.print(F("[WiFi] softAP("));
    Serial.print(ssid);
    Serial.print(F(") -> "));
    Serial.println(ok ? F("OK") : F("FAILED"));
    Serial.print(F("[WiFi] mode="));
    Serial.print((int)WiFi.getMode());
    Serial.print(F("  AP MAC="));
    Serial.print(WiFi.softAPmacAddress());
    Serial.print(F("  channel="));
    Serial.print(WiFi.channel());
    /* Read back rather than echo the define: this is what the PA ended up at,
     * so a cap that did not take (set before softAP(), say) shows as the stock
     * figure here instead of being taken on trust. */
    Serial.print(F("  txpower="));
    Serial.print(WiFi.getTxPower() / 4.0f, 2);
    Serial.println(F(" dBm"));
}

void KW307WiFi::connectSTA(const char* ssid, const char* password)
{
    WiFi.mode(WIFI_STA);
    /* See connectAP(). Station mode is where modem sleep hurts most: it parks
     * the radio until the AP's next beacon, so a pushed frame can sit waiting
     * on the order of 100 ms. */
    WiFi.setSleep(false);
    WiFi.begin(ssid, password);
    /* Same cap as connectAP(), same reason - a PA that cannot make a clean
     * beacon cannot make a clean association request either. begin() is the
     * call that raises the power here, so this follows it. */
#ifdef KW307_AP_TX_POWER
    WiFi.setTxPower(KW307_AP_TX_POWER);
#endif
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(250);
    if (WiFi.status() != WL_CONNECTED) return;   /* caller can retry / fall back to AP */
    _apMode = false;
    _startServer();
    _startMDNS();
}

void KW307WiFi::_startServer()
{
    _server.on("/",       HTTP_GET,  [this]() { _handleRoot();   });
    _server.on("/state",  HTTP_GET,  [this]() { _handleState();  });
    _server.on("/data",   HTTP_GET,  [this]() { _handleData();   });
    /* The Advanced UART Output tab, on both transports: over serial the page
       decodes the frame itself, and this is the same bytes by another road. */
    _server.on("/f3",     HTTP_GET,  [this]() { _handleF3();     });
    /* Every tab's READ and General Settings' ENTER verify, via the SDK getters. */
    _server.on("/get",    HTTP_GET,  [this]() { _handleGet();      });
    _server.on("/cmd",    HTTP_POST, [this]() { _handleCmd();    });
    _server.onNotFound([this]() { _handleRoot(); });
    _server.begin();

    /* Log associations only. Nothing is sent on connect: the page pulls /state
     * itself once the socket opens, so a client that joins mid-session gets the
     * panel without the board having to track who has already had it. */
    _ws.onEvent([](uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
        (void)payload; (void)len;
        if      (type == WStype_CONNECTED)    { Serial.print(F("[WS] client ")); Serial.print(num); Serial.println(F(" connected")); }
        else if (type == WStype_DISCONNECTED) { Serial.print(F("[WS] client ")); Serial.print(num); Serial.println(F(" gone"));      }
    });
    /* Reap a client that has gone away without closing. Without this the board
     * keeps writing to a dead socket, and WebSockets::write() spends up to
     * WEBSOCKETS_TCP_TIMEOUT (5 s) per attempt — called from feedFrame(), so one
     * absent client stalls loop(), taking the radar and every other client with
     * it. Gone in about 4 s; the page retries its own reconnect every 2 s. */
    _ws.enableHeartbeat(2000, 1000, 2);
    _ws.begin();
}

void KW307WiFi::_startMDNS()
{
    if (MDNS.begin(KW307_MDNS_NAME)) MDNS.addService("http", "tcp", 80);
}

String KW307WiFi::getURL()
{
    String url = "http://";
    url += (_apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString());
    url += " (or http://" KW307_MDNS_NAME ".local)";
    return url;
}

void KW307WiFi::tick()
{
    if (_apMode) _dns.processNextRequest();
    _server.handleClient();
    /* Services handshakes, pings and closes. The frames themselves do not come
     * through here — _wsPush() writes them as they arrive — but this still has
     * to run every loop() or a client cannot finish connecting. */
    _ws.loop();
}

/* ============================================================
   Frame latch (called from kw307_on_frame() callback in sketch)
   ============================================================ */
void KW307WiFi::feedFrame(const KW307Frame_t* f)
{
    memcpy((void*)&_frame, f, sizeof(KW307Frame_t));
    _have_frame = 1;
    _frame_ms   = millis();   /* stamped so /data can report the frame's age */

    /* Passive detection of the reporting form — see _saw_adv in the header.
     * A moving slot above 1 cannot occur in the simple form, so one sighting
     * settles it. Stays on the board even though the socket now delivers every
     * frame the browser could latch it from itself: a latch that lives in the
     * page is lost on reload, is not shared with a second browser, and is not
     * there at all on the polling fallback, which still samples at 10 Hz and so
     * still misses the brief candidate states this relies on.
     *
     * Deaf for FORM_SETTLE_MS after anything that may have changed the form,
     * because the frames arriving right then are the old form's: the module
     * switches from its next frame and the RX buffer still holds what it sent
     * before that. Latching one of those would pin the display to the form we
     * just left — permanently, since _saw_adv has no way to expire. The window
     * costs nothing: it only ever follows a write of our own. */
    if (_settled() && (f->moving[0].state > 1u || f->moving[1].state > 1u))
        _saw_adv = 1u;

    /* Straight out to the socket. Pushing from here rather than from tick() is
     * the whole point: the frame goes out on the same pass that decoded it, so
     * what the browser draws is one WiFi hop behind the module instead of
     * waiting for a request that has no idea a frame just landed. */
    _wsPush();
}

/* ============================================================
   WebSocket: broadcast the latched frame (30 bytes, little-endian)

     off  size  field
      0   u8    version (WS_FRAME_VER)
      1   u8    flags: bit0 fresh, bit1 human, bit2 saw_adv
      2   u16   age in ms since the frame landed, clamped
      4   u16   frame_index
      6   8     moving[0]     as u16 dist, i16 angle, u16 mag, u8 state, u8 cnt
     14   8     moving[1]
     22   8     stationary[0]

   Decoded by decodeFrame() in the dashboard script — change both together.
   ============================================================ */
void KW307WiFi::_wsPush()
{
    /* Nothing listening: skip the serialisation entirely. This runs 20 times a
     * second whether or not a browser is open. */
    if (_ws.connectedClients() == 0) return;   /* returns int, so compare signed */

    KW307Frame_t f;
    memcpy(&f, (const void*)&_frame, sizeof(KW307Frame_t));

    /* Age is ~0 here by construction — feedFrame() has just stamped it — but it
     * is sent anyway so the page applies one freshness rule to both transports
     * rather than trusting arrival time on one and the field on the other. */
    uint32_t age32 = _have_frame ? (millis() - _frame_ms) : 0xFFFFu;
    uint16_t age   = (age32 > 0xFFFFu) ? 0xFFFFu : (uint16_t)age32;

    uint8_t b[WS_FRAME_LEN];
    b[0] = WS_FRAME_VER;
    b[1] = (uint8_t)((_have_frame  ? 0x01u : 0x00u)
                   | (f.presence_flag ? 0x02u : 0x00u)
                   | (_saw_adv     ? 0x04u : 0x00u));
    _le16(b + 2, age);
    _le16(b + 4, f.frame_index);
    _packSlot(b +  6, f.moving[0]);
    _packSlot(b + 14, f.moving[1]);
    _packSlot(b + 22, f.stationary[0]);

    _ws.broadcastBIN(b, sizeof(b));
}

/* ============================================================
   HTTP: /  (dashboard HTML)
   ============================================================ */
void KW307WiFi::_handleRoot()
{
    _server.sendHeader("Cache-Control", "no-cache");
    _server.send_P(200, "text/html", _dashboardHtml);
}


/* ============================================================
   HTTP: /get  - the General Settings panel, as the module holds it

     {"range":[50,1000],"fov":[1200,80],"gain":5,"motion":2,"stationary":null,...}

   One SDK getter per item, and each item keyed by the verb that writes it and
   in that verb's units, so the page compares what it sent with what came back
   without converting anything: fov and shrink in tenths of a degree, hold and
   release in seconds (the module's frames / 20), RangeGate IIR on the SDK's
   0~1000 scale.

   An item whose getter failed is left out, so "no response" stays apart from a
   value. motion / stationary are null when the module matches no level
   (KW307_LEVEL_NONE) - tuned by hand, which the page shows as Custom.

   What answers also refreshes the cached members /state reports, so the plot's
   gating follows the module rather than the last ENTER.
   ============================================================ */
void KW307WiFi::_handleGet()
{
    String j;
    j.reserve(512);
    j = "{";
    bool first = true;
    auto key = [&](const char* k) {
        j += first ? "\"" : ",\""; j += k; j += "\":"; first = false;
    };
    /* Frames as seconds, to the hundredth: 20 frames a second (20 Hz), so a
     * frame is 0.05 s. hold1 folds 1 frame to 0 - the SDK sends 0 as 0 and
     * other writers floor it at 1, and the two behave alike. */
    auto sec = [&](uint16_t frames, bool hold1) {
        if (hold1 && frames <= 1u) { j += '0'; return; }
        j += (int)(frames / KW307_FRAMES_PER_SEC);
        const unsigned c = (frames % KW307_FRAMES_PER_SEC) * 100u / KW307_FRAMES_PER_SEC;
        if (c) { j += '.'; if (c < 10u) j += '0'; j += (int)c; }
    };

    /* The Advanced (t=adv) and Tracker (t=trk) tabs ask here too: one SDK
     * getter per item, keyed by the verb that writes it, in its units. A getter
     * that answers KW307_VALUE_NONE - the module holds something the SDK has no
     * name for - is null, which the page shows as Custom. */
    const String tab = _server.arg("t");

    /* What only FW 2.11 has is read on the same terms the page writes it: once
     * Check FW has read 2.11 or later. Never asked, no answer, or 2.7.6 - those
     * getters are not called. The SDK refuses several of them by itself, but
     * only once kw307_check_fw_version() has found the firmware old; before
     * that it sends, and CMD 0x33 was the sweep command on 2.7.6. Left out, the
     * items read as no answer, which is what the page shows a locked row as. */
    const bool fw210 = _fw_major != 0xFFu &&
        ((uint32_t)_fw_major * 10000u + (uint32_t)_fw_minor * 100u + _fw_patch) >= 21100u;
    /* HW Antenna - the antenna and Mag type - waits for 3.4.5. */
    const bool fw2115 = _fw_major != 0xFFu &&
        ((uint32_t)_fw_major * 10000u + (uint32_t)_fw_minor * 100u + _fw_patch) >= 30405u;

    /* t=hold: the Hold trackers button confirming its own state. */
    if (tab == "hold") {
        bool on = false;
        if (fw210 && kw307_get_stationary_hold(&on) == KW307_OK) { key("stahold"); j += on ? 1 : 0; }
        j += "}";
        _server.sendHeader("Cache-Control", "no-cache");
        _server.send(200, "application/json", j);
        return;
    }
    /* Update Rate below 20 Hz (1 Hz, 0.5 Hz or a Custom cycle): the module
     * sleeps through most of each cycle with the radar chip's RF and crystal
     * off, so what is read off the chip - Power Mode, the antenna - reads the
     * sleep, not the setting. Neither is asked for until the rate is back at
     * 20 Hz; the page says so, and the urate it is sent is how it knows. */
    uint8_t urT = 0, urM = 0;
    const bool urOk = tab != "trk" && kw307_get_update_rate(&urT, &urM) == KW307_OK;
    const bool slow = urOk && urT != 0;
    /* The page's index of it: (0, 0) 20 Hz, (20, 4) 1 Hz, (40, 4) 0.5 Hz.
     * Any other cycle is -1, null to the page, which shows it as Custom. */
    const int urIx = (urT == 0) ? 0 : (urT == 20 && urM == 4) ? 1
                   : (urT == 40 && urM == 4) ? 2 : -1;

    if (tab == "adv" || tab == "trk") {
        auto num = [&](int v) { j += v; };
        /* Not "bit": Arduino.h defines bit(b) as a macro, which turned every call
         * into (1UL << (x)); - a statement that writes nothing - and left
         * "ahpf":, in the JSON, so the page could not parse any of it. */
        auto flag01 = [&](bool v) { j += v ? 1 : 0; };
        uint8_t u1 = 0, u2 = 0, u3 = 0; bool x = false;
        if (tab == "adv") {
            /* Update Rate: kw307_get_update_rate(), SDK 2.17.6, as urIx. */
            if (urOk) { key("urate"); if (urIx < 0) j += "null"; else num(urIx); }
            if (kw307_get_moving_hpf_corner(&u1, &u2) == KW307_OK) {
                key("mhpf"); j += '['; num(u1); j += ','; num(u2); j += ']';
            }
            if (kw307_get_moving_lpf_corner(&u1) == KW307_OK) { key("mlpf"); num(u1); }
            if (kw307_get_stationary_hpf_corner(&u1, &u2) == KW307_OK) {
                key("shpf"); j += '['; num(u1); j += ','; num(u2); j += ']';
            }
            if (kw307_get_stationary_lpf_corner(&u1) == KW307_OK) { key("slpf"); num(u1); }
            /* fw210: Antenna, the CMD 0x32 front end, Equalizer and Digital
             * Gain - the gate:1 rows, locked on the page below 2.11. The
             * antenna and Mag type wait for 20 Hz as well (slow, above). */
            if (fw2115 && !slow && kw307_get_antenna(&u1, &u2, &x) == KW307_OK) {
                key("antenna");
                if (u1 == KW307_VALUE_NONE || u2 == KW307_VALUE_NONE) j += "null";
                else { j += '['; num(u1); j += ','; num(u2); j += ','; flag01(x); j += ']'; }
            }
            if (fw2115 && !slow && kw307_get_mag_type(&u1) == KW307_OK) { key("magtype"); num(u1); }
            if (fw210 && kw307_get_background_subtraction(&x) == KW307_OK) { key("ahpf");   flag01(x); }
            if (fw210 && kw307_get_background_update(&x) == KW307_OK)      { key("ahpfsw"); flag01(x); }
            if (fw210 && kw307_get_equalizer_gain(&u1) == KW307_OK)  { key("eqgain"); num(u1); }
            if (fw210 && kw307_get_baseband_att(&u1) == KW307_OK) {
                key("bbatt"); if (u1 == KW307_VALUE_NONE) j += "null"; else num(u1);
            }
            if (kw307_get_sample_rate(&u1) == KW307_OK)           { key("fs");   num(u1); }
            if (kw307_get_moving_background(&x) == KW307_OK)      { key("mbg");  flag01(x); }
            if (kw307_get_moving_env_subtraction(&x) == KW307_OK) { key("menv"); flag01(x); }
            if (kw307_get_stationary_background(&x) == KW307_OK)  { key("sbg");  flag01(x); }
            if (kw307_get_stationary_env_subtraction(&x) == KW307_OK) { key("senv"); flag01(x); }
            if (fw210 && kw307_get_equalizer_order(&u1) == KW307_OK) {
                key("eqorder"); if (u1 == KW307_VALUE_NONE) j += "null"; else num(u1);
            }
            if (fw210 && kw307_get_digital_gain(&u1) == KW307_OK) { key("dspgain"); num(u1); }
        } else {
            int16_t s1 = 0, s2 = 0, s3 = 0; uint16_t w1 = 0;
            if (kw307_get_stationary_tracker_opt(&u1, &s1, &w1) == KW307_OK) {
                key("stopt"); j += '['; num(u1); j += ','; num(s1); j += ','; num(w1); j += ']';
            }
            if (kw307_get_motion_threshold(&u1, &s1, &s2, &s3) == KW307_OK) {
                key("mthr"); j += '['; num(u1); j += ','; num(s1); j += ','; num(s2); j += ','; num(s3); j += ']';
            }
            if (kw307_get_moving_smoothing(&s1) == KW307_OK) { key("msmooth"); num(s1); }
            if (kw307_get_los(&x) == KW307_OK)     { key("los");    flag01(x); }
            if (kw307_get_fov_min(&s1) == KW307_OK) { key("fovmin"); num(s1); }
        }
        j += "}";
        _server.sendHeader("Cache-Control", "no-cache");
        _server.send(200, "application/json", j);
        return;
    }

    /* The rate, so the page knows why Power Mode and HW Antenna are missing. */
    if (urOk) { key("urate"); if (urIx < 0) j += "null"; else j += urIx; }
    uint16_t mn = 0, mx = 0;
    if (kw307_get_detect_range(&mn, &mx) == KW307_OK) {
        key("range"); j += '['; j += (int)mn; j += ','; j += (int)mx; j += ']';
        _range_min_cm = mn; _range_max_cm = mx;
    }
    int16_t fov = 0; uint8_t sh = 0;
    if (kw307_get_detect_fov(&fov, &sh) == KW307_OK) {
        key("fov"); j += '['; j += (int)(fov * 10); j += ','; j += (int)(sh * 10); j += ']';
        _fov_deg10 = (int16_t)(fov * 10); _shrink_deg10 = (uint8_t)(sh * 10);
    }
    uint8_t g = 0;
    if (kw307_get_gain(&g) == KW307_OK) { key("gain"); j += (int)g; _gain = g; }
    int8_t lv = 0;
    if (kw307_get_motion_sensitivity(&lv) == KW307_OK) {
        key("motion");
        if (lv == KW307_LEVEL_NONE) j += "null"; else { j += (int)lv; _motion_idx = lv; }
    }
    if (kw307_get_stationary_sensitivity(&lv) == KW307_OK) {
        key("stationary");
        if (lv == KW307_LEVEL_NONE) j += "null"; else { j += (int)lv; _stationary_idx = (uint8_t)lv; }
    }
    int16_t a = 0, b = 0;
    if (kw307_get_mag_threshold(&a, &b) == KW307_OK) {
        key("signal"); j += '['; j += (int)a; j += ','; j += (int)b; j += ']';
        _signal_moving = a; _signal_stationary = b;
    }
    bool on = false; int16_t c1 = 0, c2 = 0;
    if (kw307_get_advanced_clutter_rejection(&on, &c1, &c2) == KW307_OK) {
        key("clutter"); j += '['; j += on ? 1 : 0; j += ','; j += (int)c1; j += ','; j += (int)c2; j += ']';
        _clutter_en = on; _clutter_spd = c1; _clutter_dev = c2;
    }
    if (kw307_get_RangeGate_IIR(&a, &b) == KW307_OK) {
        key("rgiir"); j += '['; j += (int)a; j += ','; j += (int)b; j += ']';
        _rg_iir_n = (uint16_t)a; _rg_iir_s = (uint16_t)b;
    }
    uint8_t u1 = 0, u2 = 0;
    if (kw307_get_moving_tracker_cnt(&u1, &u2) == KW307_OK) {
        key("movtrk"); j += '['; j += (int)u1; j += ','; j += (int)u2; j += ']';
        _mov_trk_max = u1; _mov_trk_show = u2;
    }
    if (kw307_get_stationary_tracker_cnt(&u1, &u2) == KW307_OK) {
        key("statrk"); j += '['; j += (int)u1; j += ','; j += (int)u2; j += ']';
        _sta_trk_max = u1; _sta_trk_show = u2;
    }
    uint16_t fr = 0;
    if (kw307_get_presence_hold_time(&fr) == KW307_OK) {
        key("hold"); sec(fr, true);
        _hold_sec = (uint16_t)(fr <= 1u ? 0u : fr / KW307_FRAMES_PER_SEC);
    }
    if (kw307_get_presence_hold_zone(&on, &c1, &c2) == KW307_OK) {
        key("forcesta"); j += '['; j += on ? 1 : 0; j += ','; j += (int)c1; j += ','; j += (int)c2; j += ']';
        _force_sta_on = on; _force_sta_dist_cm = c1; _force_sta_fov_deg = c2;
    }
    if (kw307_get_stationary_release_time(&u1, &fr) == KW307_OK) {
        key("maxsta"); j += '['; j += (int)u1; j += ','; sec(fr, false); j += ']';
        _max_sta_en = u1; _max_sta_sec = (uint16_t)(fr / KW307_FRAMES_PER_SEC);
    }
    if (!slow && kw307_get_power_mode(&u1) == KW307_OK) { key("powermode"); j += (int)u1; _power_mode = u1; }
    if (kw307_get_noise_floor_report(&on) == KW307_OK) {
        key("rssi"); j += on ? 1 : 0; _rssi_report = on;
    }
    if (kw307_get_gpio_presence_flag(&on) == KW307_OK) {
        key("gpioflag"); j += on ? 1 : 0; _gpio_flag = on;
    }
    j += "}";

    _server.sendHeader("Cache-Control", "no-cache");
    _server.send(200, "application/json", j);
}


/* ============================================================
   HTTP: /state  (JSON: settings mirror + FW version, no frame)

   Everything here changes only when someone presses ENTER or Check FW, so the
   page pulls it at 1 Hz and again immediately after any write. Keeping it out
   of the 20 Hz path is what the split bought: these are the ~85% of /data that
   used to be re-sent with every single frame.

   saw_adv rides the push frame, but is repeated here so a page that has not
   received a frame yet still resolves the tracker-state form correctly.
   ============================================================ */
void KW307WiFi::_handleState()
{
    char j[1024];
    snprintf(j, sizeof(j),
        "{"
          "\"fw_major\":%u,\"fw_minor\":%u,\"fw_patch\":%u,"
          "\"scene\":%u,"
          "\"gain\":%u,"
          "\"range_min_cm\":%u,\"range_max_cm\":%u,"
          "\"fov_deg10\":%d,\"shrink_deg10\":%u,"
          "\"signal_moving\":%d,\"signal_stationary\":%d,"
          "\"hold_sec\":%u,"
          "\"motion_idx\":%d,\"stationary_idx\":%u,"
          "\"saw_adv\":%u,\"gpio_flag\":%u,"
          "\"clutter_en\":%u,\"clutter_spd\":%d,\"clutter_dev\":%d,"
          "\"last_nack\":%u,"
          "\"power_down\":%u,"
          "\"force_sta_on\":%u,\"force_sta_dist\":%d,\"force_sta_fov\":%d,"
          "\"max_sta_en\":%u,\"max_sta_sec\":%u,"
          "\"mov_trk_max\":%u,\"mov_trk_show\":%u,"
          "\"sta_trk_max\":%u,\"sta_trk_show\":%u"
        "}",
        (unsigned)_fw_major, (unsigned)_fw_minor, (unsigned)_fw_patch,
        (unsigned)_scene,
        (unsigned)_gain,
        (unsigned)_range_min_cm, (unsigned)_range_max_cm,
        (int)_fov_deg10, (unsigned)_shrink_deg10,
        (int)_signal_moving, (int)_signal_stationary,
        (unsigned)_hold_sec,
        (int)_motion_idx, (unsigned)_stationary_idx,
        (unsigned)_saw_adv, (unsigned)_gpio_flag,
        (unsigned)_clutter_en, (int)_clutter_spd, (int)_clutter_dev,
        (unsigned)kw307_last_nack_status(),
        (unsigned)_power_mode,
        (unsigned)_force_sta_on, (int)_force_sta_dist_cm, (int)_force_sta_fov_deg,
        (unsigned)_max_sta_en, (unsigned)_max_sta_sec,
        (unsigned)_mov_trk_max, (unsigned)_mov_trk_show,
        (unsigned)_sta_trk_max, (unsigned)_sta_trk_show);

    _server.sendHeader("Cache-Control", "no-cache");
    _server.send(200, "application/json", j);
}

/* ============================================================
   HTTP: /data  (JSON snapshot of latest frame + mirror state)

   The fallback transport, not the normal one. The page polls this at 10 Hz only
   when the WebSocket will not open — an old firmware behind a new page, a proxy
   that blocks the upgrade, port 81 firewalled. Left byte-for-byte as it was so
   that path keeps working exactly as before.
   ============================================================ */
void KW307WiFi::_handleData()
{
    KW307Frame_t f;
    memcpy(&f, (const void*)&_frame, sizeof(KW307Frame_t));
    uint8_t fresh = _have_frame;

    /* Milliseconds since the last frame landed. "fresh" only says a frame
     * arrived at some point in the past, so the dashboard needs the age to
     * tell a live reading from one frozen by a dead link or Power Down.
     * Clamped so the JSON field stays a small number. */
    uint32_t age = fresh ? (millis() - _frame_ms) : 999999u;
    if (age > 999999u) age = 999999u;

    char j[2048];
    snprintf(j, sizeof(j),
        "{"
          "\"fw_major\":%u,\"fw_minor\":%u,\"fw_patch\":%u,"
          "\"fresh\":%u,\"age\":%u,"
          "\"frame_index\":%u,"
          "\"human\":%u,"
          /* Per slot: distance cm, angle 0.1 deg, mag, state, counter. */
          "\"moving\":["
            "{\"d\":%u,\"a\":%d,\"m\":%u,\"st\":%u,\"cnt\":%u},"
            "{\"d\":%u,\"a\":%d,\"m\":%u,\"st\":%u,\"cnt\":%u}],"
          "\"stationary\":{\"d\":%u,\"a\":%d,\"m\":%u,\"st\":%u,\"cnt\":%u},"
          "\"scene\":%u,"
          "\"gain\":%u,"
          "\"range_min_cm\":%u,\"range_max_cm\":%u,"
          "\"fov_deg10\":%d,\"shrink_deg10\":%u,"
          "\"signal_moving\":%d,\"signal_stationary\":%d,"
          "\"hold_sec\":%u,"
          "\"motion_idx\":%d,\"stationary_idx\":%u,"
          "\"saw_adv\":%u,\"gpio_flag\":%u,"
          "\"clutter_en\":%u,\"clutter_spd\":%d,\"clutter_dev\":%d,"
          "\"last_nack\":%u,"
          "\"power_down\":%u,"
          "\"force_sta_on\":%u,\"force_sta_dist\":%d,\"force_sta_fov\":%d,"
          "\"max_sta_en\":%u,\"max_sta_sec\":%u,"
          "\"mov_trk_max\":%u,\"mov_trk_show\":%u,"
          "\"sta_trk_max\":%u,\"sta_trk_show\":%u"
        "}",
        (unsigned)_fw_major, (unsigned)_fw_minor, (unsigned)_fw_patch,
        (unsigned)fresh, (unsigned)age,
        fresh ? (unsigned)f.frame_index : 0u,
        fresh ? (unsigned)f.presence_flag : 0u,
        fresh ? (unsigned)f.moving[0].distance_cm : 0u, fresh ? (int)f.moving[0].angle_tenth : 0,
        fresh ? (unsigned)f.moving[0].mag : 0u,
        fresh ? (unsigned)f.moving[0].state : 0u,        fresh ? (unsigned)f.moving[0].counter : 0u,
        fresh ? (unsigned)f.moving[1].distance_cm : 0u, fresh ? (int)f.moving[1].angle_tenth : 0,
        fresh ? (unsigned)f.moving[1].mag : 0u,
        fresh ? (unsigned)f.moving[1].state : 0u,        fresh ? (unsigned)f.moving[1].counter : 0u,
        fresh ? (unsigned)f.stationary[0].distance_cm : 0u, fresh ? (int)f.stationary[0].angle_tenth : 0,
        fresh ? (unsigned)f.stationary[0].mag : 0u,
        fresh ? (unsigned)f.stationary[0].state : 0u,        fresh ? (unsigned)f.stationary[0].counter : 0u,
        (unsigned)_scene,
        (unsigned)_gain,
        (unsigned)_range_min_cm, (unsigned)_range_max_cm,
        (int)_fov_deg10, (unsigned)_shrink_deg10,
        (int)_signal_moving, (int)_signal_stationary,
        (unsigned)_hold_sec,
        (int)_motion_idx, (unsigned)_stationary_idx,
        (unsigned)_saw_adv, (unsigned)_gpio_flag,
        (unsigned)_clutter_en, (int)_clutter_spd, (int)_clutter_dev,
        (unsigned)kw307_last_nack_status(),
        (unsigned)_power_mode,
        (unsigned)_force_sta_on, (int)_force_sta_dist_cm, (int)_force_sta_fov_deg,
        (unsigned)_max_sta_en, (unsigned)_max_sta_sec,
        (unsigned)_mov_trk_max, (unsigned)_mov_trk_show,
        (unsigned)_sta_trk_max, (unsigned)_sta_trk_show);

    _server.sendHeader("Cache-Control", "no-cache");
    _server.send(200, "application/json", j);
}

/* ============================================================
   0xF3 latch and endpoint (Advanced UART Output)
   ============================================================ */
void KW307WiFi::feedF3(uint8_t cmd, const uint8_t* data, uint16_t len)
{
    (void)cmd;                       /* registered for 0xF3 only */
    if (!data || len == 0) return;
    if (len > F3_CAP) return;        /* drop, never truncate - half a frame
                                        decodes into something that looks real */
    memcpy((void*)_f3, data, len);
    _f3_len = len;
    _f3_ms  = millis();
    _f3_seq++;                       /* last: the page reads seq to decide the
                                        rest is worth reading */
}

/* {"seq":N,"len":L,"age":MS,"hex":"AABB.."}
 *
 * Hex rather than base64: the page turns it straight into the byte array its
 * serial path already produces, and a frame this size costs a few hundred
 * characters. seq is the point of the endpoint - the tab polls, and re-drawing
 * a frame it has already drawn would make the byte view flicker for nothing. */
void KW307WiFi::_handleF3()
{
    uint16_t len = _f3_len;
    uint32_t seq = _f3_seq;
    uint32_t age = seq ? (millis() - _f3_ms) : 999999u;
    if (age > 999999u) age = 999999u;

    String out;
    out.reserve(len * 2 + 64);
    out  = "{\"seq\":";  out += seq;
    out += ",\"len\":";  out += len;
    out += ",\"age\":";  out += age;
    out += ",\"hex\":\"";
    /* Not HEX: Arduino's Print.h already has that as a numeric constant, so
       HEX[b >> 4] compiles as 16[...] and the error lands in Print.h. */
    static const char NIB[] = "0123456789ABCDEF";
    for (uint16_t i = 0; i < len; i++) {
        uint8_t b = _f3[i];
        out += NIB[b >> 4];
        out += NIB[b & 0x0F];
    }
    out += "\"}";
    _server.send(200, "application/json", out);
}


/* ============================================================
   HTTP: /cmd  (POST verb:args)
   ============================================================ */
void KW307WiFi::_handleCmd()
{
    String body = _server.arg("plain");
    body.trim();
    _cmdRc = KW307_OK;
    _processCommand(body);
    _server.send(200, "text/plain", _cmdRc == KW307_OK ? "ok" : "err");
}

int KW307WiFi::_parseInt(const String& s, int fallback)
{
    if (s.length() == 0) return fallback;
    return (int)s.toInt();
}

/* Each verb maps to a kw307_* public API call (or short sequence of
 * calls for combos that aggregate multiple parameters). */
void KW307WiFi::_processCommand(const String& cmd)
{
    int colon = cmd.indexOf(':');
    String verb = (colon >= 0) ? cmd.substring(0, colon) : cmd;
    String rest = (colon >= 0) ? cmd.substring(colon + 1) : String();

    auto next = [&rest]() -> String {
        int c = rest.indexOf(':');
        String t = (c >= 0) ? rest.substring(0, c) : rest;
        rest = (c >= 0) ? rest.substring(c + 1) : String();
        return t;
    };

    /* ---- Scene preset ----------------------------------------------
     * Carries no settings of its own. The preset table lives in the browser
     * and only fills the panel there; ENTER then pushes every setting
     * explicitly. All this verb does is record which preset the panel is
     * showing, so /data can report it back. */
    if (verb == "scene") {
        uint8_t m = (uint8_t)_parseInt(rest, _scene);
        if (m < 5) _scene = m;
    }

    /* ---- Gain ------------------------------------------------------ */
    else if (verb == "gain") {
        uint8_t g = (uint8_t)_parseInt(rest, _gain);
        if (kw307_set_gain(g) == KW307_OK) _gain = g;
    }

    /* ---- Detection range, min / max in cm -------------------------- */
    else if (verb == "range") {
        uint16_t mn = (uint16_t)_parseInt(next(), _range_min_cm);
        uint16_t mx = (uint16_t)_parseInt(rest, _range_max_cm);
        if (kw307_set_detect_range(mn, mx) == KW307_OK) {
            _range_min_cm = mn; _range_max_cm = mx;
        }
    }

    /* ---- Field of view + shrink rate ------------------------------ */
    else if (verb == "fov") {
        int16_t fov = (int16_t)_parseInt(next(), _fov_deg10);
        uint8_t sh  = (uint8_t)_parseInt(rest, _shrink_deg10);
        /* Both arrive as deg×10 (1200 = 120°, 80 = 8°/100 cm); the API
         * takes whole degrees. Mirrors stay in deg×10 for /data. */
        if (kw307_set_detect_fov(fov / 10, (uint8_t)(sh / 10)) == KW307_OK) {
            _fov_deg10 = fov; _shrink_deg10 = sh;
        }
    }

    /* ---- Motion sensitivity ---------------------------------------
     * "motion:<level>", level 0 = most sensitive ... 9 = least, or -1 for one
     * step below 0: reacts to any motion regardless of how much speed has
     * built up, for something like a hand dryer that has to fire the instant
     * a hand appears. */
    else if (verb == "motion") {
        int idx = _parseInt(rest, _motion_idx);
        if (idx >= -1 && idx <= 9) {
            if (kw307_set_motion_sensitivity((int8_t)idx) == KW307_OK)
                _motion_idx = (int8_t)idx;
        }
    }

    /* ---- Stationary sensitivity, level 0~9 ------------------------ */
    else if (verb == "stationary") {
        int idx = _parseInt(rest, _stationary_idx);
        if (idx >= 0 && idx <= 9) {
            if (kw307_set_stationary_sensitivity((uint8_t)idx) == KW307_OK)
                _stationary_idx = (uint8_t)idx;
        }
    }

    /* ---- Presence flag hold time, seconds ------------------------- */
    /* The page speaks seconds; SDK 2.17.1 takes frames: seconds *
     * KW307_FRAMES_PER_SEC (20 Hz), as in the SDK examples. */
    else if (verb == "hold") {
        int s = _parseInt(rest, _hold_sec);
        if (s >= 0 && s <= KW307_MAX_HOLD_SEC &&
            kw307_set_presence_hold_time((uint16_t)(s * KW307_FRAMES_PER_SEC)) == KW307_OK)
            _hold_sec = (uint16_t)s;
    }

    /* ---- Signal threshold, moving / stationary -------------------- */
    else if (verb == "signal") {
        int16_t fa = (int16_t)_parseInt(next(), _signal_moving);
        int16_t sl = (int16_t)_parseInt(rest, _signal_stationary);
        if (kw307_set_mag_threshold(fa, sl) == KW307_OK) {
            _signal_moving = fa; _signal_stationary = sl;
        }
    }

    /* ================================================================
       Module actions — the five buttons over the radar plot
       ================================================================ */

    /* ---- Initial - re-initialise the radar -----------------------
     * Reloads the module's saved settings and restarts detection, which
     * takes about 130 ms. Any setting applied but not saved is dropped, so
     * re-send the panel (ENTER) afterwards. */
    else if (verb == "radarinit") {
        kw307_radar_init();
        /* Reloads the saved record, which may hold a different reporting form
         * than the one currently running — re-detect instead of trusting the
         * old sighting. Same reason as the factory reset below. The call blocks
         * ~130 ms with the module still streaming, so the RX buffer is holding
         * several pre-init frames by the time it returns: settle before
         * believing any of them. */
        _saw_adv = 0u;
        _formDisturbed();
    }

    /* ---- Clear State ----------------------------------------------
     * Drops the targets the module is holding and resets the presence flag.
     * Settings and flash are untouched; targets take a moment to reappear. */
    else if (verb == "clearstate") {
        kw307_clear_state();
    }

    /* ---- Factory Reset -------------------------------------------- */
    else if (verb == "reset") {
        kw307_factory_reset();
        /* Back at firmware defaults, which mean the simple form, and the frames
         * still in flight are from before that — the case the window exists for. */
        _saw_adv = 0u;
        _formDisturbed();
    }

    /* ---- Save Setting --------------------------------------------- */
    else if (verb == "save") {
        kw307_save_settings();
    }

    /* ---- Check FW - read the module's firmware version ----------- */
    else if (verb == "fwver") {
        uint8_t maj = 0, min_ = 0, patch = 0;
        /* kw307_check_fw_version() first: it is what tells the SDK the module
         * is 2.7.6, so the calls that firmware lacks answer NOT_SUPPORTED
         * instead of being sent and ignored. It does not hand the digits back,
         * so the page's copy comes from a second read. */
        if (kw307_check_fw_version() >= 0 &&
            kw307_cmd_read_fw_version(KW307_FW_CHECK_TIMEOUT_MS, &maj, &min_, &patch) == KW307_OK) {
            _fw_major = maj;
            _fw_minor = min_;
            _fw_patch = patch;
        } else {
            /* Sentinel: dashboard JS treats major == 0xFF as "read failed". */
            _fw_major = 0xFF;
            _fw_minor = 0xFF;
            _fw_patch = 0xFF;
        }
    }

    /* ---- Power Down ----------------------------------------------- */
    /* All three modes: the Settings panel is the only control for this now, and
     * it offers Power Down, Normal and PA Off. kw307_set_power_down(), which SDK 2.8.0 dropped, was the
     * two-state shortcut for exactly this call. */
    else if (verb == "powermode") {
        const long m = _parseInt(rest, _power_mode);
        if (m < 0 || m > 2) return;
        if (kw307_set_power_mode((uint8_t)m) == KW307_OK) _power_mode = (uint8_t)m;
    }

    /* ---- Force-stationary window ---------------------------------- */
    else if (verb == "forcesta") {
        bool on = (_parseInt(next(), _force_sta_on ? 1 : 0) != 0);
        int16_t dist = (int16_t)_parseInt(next(), _force_sta_dist_cm);
        int16_t fov  = (int16_t)_parseInt(rest, _force_sta_fov_deg);
        if (kw307_set_presence_hold_zone(on, dist, fov) == KW307_OK) {
            _force_sta_on = on; _force_sta_dist_cm = dist; _force_sta_fov_deg = fov;
        }
    }

    /* ---- Max stationary time, seconds x 20 Hz = frames (SDK 2.17.1) */
    else if (verb == "maxsta") {
        uint8_t en  = (uint8_t)_parseInt(next(), _max_sta_en);
        int     sec = _parseInt(rest, _max_sta_sec);
        if (sec >= 0 && sec <= KW307_MAX_HOLD_SEC &&
            kw307_set_stationary_release_time(en, (uint16_t)(sec * KW307_FRAMES_PER_SEC)) == KW307_OK) {
            _max_sta_en = en; _max_sta_sec = (uint16_t)sec;
        }
    }

    /* ---- Moving tracker counters ---------------------------------- */
    else if (verb == "movtrk") {
        uint8_t mx = (uint8_t)_parseInt(next(), _mov_trk_max);
        uint8_t sh = (uint8_t)_parseInt(rest,   _mov_trk_show);
        if (kw307_set_moving_tracker_cnt(mx, sh) == KW307_OK) {
            _mov_trk_max = mx; _mov_trk_show = sh;
        }
    }

    /* ---- Stationary tracker counters ------------------------------ */
    else if (verb == "statrk") {
        uint8_t mx = (uint8_t)_parseInt(next(), _sta_trk_max);
        uint8_t sh = (uint8_t)_parseInt(rest,   _sta_trk_show);
        if (kw307_set_stationary_tracker_cnt(mx, sh) == KW307_OK) {
            _sta_trk_max = mx; _sta_trk_show = sh;
        }
    }

    /* ---- RangeGate IIR --------------------------------------------
     * Both values on kw307_set_RangeGate_IIR()'s 0~1000 scale (150 = 0.15),
     * the scale its getter answers on too, so /get hands back exactly what was
     * sent. The SDK rounds to Q15 on the way out. */
    else if (verb == "rgiir") {
        const long kn = _parseInt(next(), _rg_iir_n);
        const long ks = _parseInt(rest,   _rg_iir_s);
        if (kn < 0 || kn > 1000 || ks < 0 || ks > 1000) return;
        if (kw307_set_RangeGate_IIR((int16_t)kn, (int16_t)ks) == KW307_OK) {
            _rg_iir_n = (uint16_t)kn; _rg_iir_s = (uint16_t)ks;
        }
    }


    /* ---- Report noise floor ----------------------------------------
     * The Settings panel carries it. */
    else if (verb == "rssi") {
        const bool on = (_parseInt(rest, _rssi_report ? 1 : 0) != 0);
        if (kw307_en_noise_floor_report(on) == KW307_OK)
            _rssi_report = on;
    }
    /* ---- Stationary hold, PID 0x73 (FW 2.11) ----------------------
     * A transient, not a setting: no member caches it and /state does not
     * report it, so nothing here can put it back on after a reset. The page
     * reads it back through /get?t=hold (kw307_get_stationary_hold) rather than
     * trusting this write -- while the
     * hold is on the stationary output is frozen, so one that did not land
     * looks exactly like one that did. */
    else if (verb == "stahold") {
        const bool on = (_parseInt(rest, 0) != 0);
        kw307_en_stationary_hold(on);
    }
    /* Line of sight. The SDK does the reading here, not this handler: the
     * switch IS the wedge angle -- zero is off -- so turning it off has to
     * learn the angle before it erases it, and turning it on must not push a
     * remembered angle over a module that already has one. All of that lives
     * in kw307_en_los(), which is the point of calling the SDK rather than
     * writing the PID from here. The page reads 0x08 back afterwards. */
    else if (verb == "los") {
        const bool on = (_parseInt(rest, 0) != 0);
        kw307_en_los(on);
    }
    /* ---- Advanced UART Output (frame 0xF3) ------------------------
     *
     * Two verbs because the module offers two ways in, and collapsing them
     * would mean this layer deciding which the operator meant:
     *
     *   output:<mode>            the named modes - OFF, NORMAL, RG, Parking,
     *                            NORMAL_WITH_SPD. What the SDK documents, and
     *                            what a customer picks from.
     *   f3:<lo>:<hi>:<blk>:<fmt> any other shape of 0xF3, straight from the
     *                            descriptor the page composes.
     *
     * Neither caches anything: the module is asked for the shape it is in, by
     * the tab, whenever the tab wants to know. A cached copy here would be a
     * second answer to a question the module can answer itself. */
    else if (verb == "output") {
        const int m = _parseInt(rest, KW307_OUTPUT_NORMAL);
        if (m >= 0 && m <= 255) kw307_set_output((uint8_t)m);
    }
    else if (verb == "f3") {
        /* lo:hi:blk:fmt - the same four numbers the descriptor carries, in the
         * order the frame carries them. */
        int lo = 0, hi = 0, blk = 0, fmt = 0;
        int n = sscanf(rest.c_str(), "%d:%d:%d:%d", &lo, &hi, &blk, &fmt);
        /* SDK 2.14.1 took kw307_set_output_advanced() out of the package;
         * 2.16 builds a shape block by block instead. Each descriptor bit is
         * handed to the block call that owns it, so the SDK still checks the
         * shape and still answers NOT_SUPPORTED on 2.7.6. The gate range only
         * matters - and is only checked - when a gate block is on. */
        if (n == 4 && blk >= 0 && blk <= 0xFF && fmt >= 0 && fmt <= 0x0F) {
            KW307OutputConfig_t c;
            const bool rgMov = (blk & KW307_F3_RG_MOVING) != 0;
            const bool rgSta = (blk & KW307_F3_RG_STATIONARY) != 0;
            bool ok = true;
            kw307_out_clear(&c);
            kw307_out_presence(&c, (blk & KW307_F3_PRESENCE) != 0);
            kw307_out_moving_trk(&c, (blk & KW307_F3_MOVING_TRK) != 0,
                                 (fmt & KW307_F3_MOVING_TRK_FULL) != 0,
                                 (fmt & KW307_F3_MOVING_SPEED) != 0);
            kw307_out_stationary(&c, (blk & KW307_F3_STATIONARY_NEAREST) != 0,
                                 (blk & KW307_F3_STATIONARY_SECOND) != 0);
            kw307_out_stationary_trk(&c, (blk & KW307_F3_STATIONARY_TRK) != 0,
                                     (fmt & KW307_F3_STATIONARY_TRK_FULL) != 0);
            kw307_out_room_max(&c, (blk & KW307_F3_ROOM_MAX) != 0);
            if (rgMov || rgSta)
                ok = kw307_out_rg(&c, (uint8_t)lo, (uint8_t)hi, rgMov, rgSta,
                                  (fmt & KW307_F3_RG_MAG_ONLY) != 0) == KW307_OK;
            if (ok) kw307_set_output_config(&c);
        }
    }
    else if (verb == "gpioflag") {
        bool on = (_parseInt(rest, _gpio_flag ? 1 : 0) != 0);
        if (kw307_en_gpio_presence_flag(on) == KW307_OK) _gpio_flag = on;
    }

    /* ---- Repetitive-motion (clutter) rejection -------------------- */
    else if (verb == "clutter") {
        bool    en  = (_parseInt(next(), _clutter_en ? 1 : 0) != 0);
        int16_t spd = (int16_t)_parseInt(next(), _clutter_spd);
        int16_t dev = (int16_t)_parseInt(rest,   _clutter_dev);
        if (kw307_set_advanced_clutter_rejection(en, spd, dev) == KW307_OK) {
            _clutter_en = en; _clutter_spd = spd; _clutter_dev = dev;
        }
    }

    /* Tracker state advanced mode (PID 0x6B): SDK 2.17.6 withdrew it, and the
     * dashboard with it. The _saw_adv latch still draws a module that was left
     * in that form. */

    /* ---- Advanced Settings: one verb per SDK setter --------------------
     * Each takes its setter's own arguments, in its units, and /get?t=adv
     * answers with the matching getter under the same name - so the page
     * compares what it sent with what came back without converting. */
    /* Update Rate: the page's 20 Hz / 1 Hz / 0.5 Hz as the cycles
     * kw307_set_update_rate(total_frames, measured_frames) takes since SDK
     * 2.17.6 - (0, 0), (20, 4), (40, 4). Anything else is not sent. Not saved.
     *
     * The setter sends only the cycle; the moving tracker is fitted to it
     * here, after it, in User Guide 4.11's order:
     *   kw307_set_motion_threshold()    (2, 250, 500, 500) in a cycle - 4
     *                                   measured frames take confirm 2 at most -
     *                                   the defaults (8, 250, 500, 2000) at 20 Hz
     *   kw307_set_moving_tracker_cnt()  display_th 1 in a cycle, its default 8
     *                                   at 20 Hz; cnt_limit as it is, display_th
     *                                   no more than it
     * 20 Hz on a module already at 20 Hz writes neither, so tuning made there
     * stays - unless the moving threshold still holds the cycle's four: the two
     * are saved and the rate is not, so a module saved at a cycle powers up at
     * 20 Hz with them, and the guide says to set the 20 Hz values back. 20 Hz
     * needs the read to tell, and sends nothing without it.
     *
     * The rate the module already runs is not sent again: the firmware turns
     * the PA back on and restarts the cycle count on every CMD 0x05
     * (KW_Cycle_PS_Off()), so an ENTER that only repeats the rate would undo a
     * PA Off. The same cycle writes nothing at all; 20 Hz on 20 Hz goes on to
     * the threshold check above. */
    else if (verb == "urate") {
        static const uint8_t UR[3][2] = { { 0u, 0u }, { 20u, 4u }, { 40u, 4u } };
        const int r = _parseInt(rest, -1);
        if (r < 0 || r > 2) return;
        uint8_t wt = 0, wm = 0;
        const bool was = kw307_get_update_rate(&wt, &wm) == KW307_OK;
        if (r == 0 && !was) return;
        const bool same = was && wt == UR[r][0] && (r == 0 || wm == UR[r][1]);
        if (same && r != 0) return;
        if (!same && kw307_set_update_rate(UR[r][0], UR[r][1]) != KW307_OK) return;
        if (r == 0 && wt == 0) {
            uint8_t cf = 0; int16_t sp = 0, cl = 0, su = 0;
            if (kw307_get_motion_threshold(&cf, &sp, &cl, &su) != KW307_OK ||
                !(cf == 2u && sp == 250 && cl == 500 && su == 500)) return;
        }
        if (r != 0) kw307_set_motion_threshold(2u, 250, 500, 500);
        else        kw307_set_motion_threshold(8u, 250, 500, 2000);
        uint8_t mx = 0, sh = 0;
        if (kw307_get_moving_tracker_cnt(&mx, &sh) == KW307_OK) {
            const uint8_t th = (r != 0) ? 1u : (mx < 8u ? mx : 8u);
            if (kw307_set_moving_tracker_cnt(mx, th) == KW307_OK) {
                _mov_trk_max = mx; _mov_trk_show = th;
            }
        }
    }
    else if (verb == "mhpf" || verb == "shpf") {
        const int c = _parseInt(next(), -1), nd = _parseInt(rest, -1);
        if (c < 0 || nd < 0) return;
        if (verb == "mhpf") kw307_set_moving_hpf_corner((uint8_t)c, (uint8_t)nd);
        else                kw307_set_stationary_hpf_corner((uint8_t)c, (uint8_t)nd);
    }
    else if (verb == "mlpf" || verb == "slpf") {
        const int c = _parseInt(rest, -1);
        if (c < 0 || c > 255) return;
        if (verb == "mlpf") kw307_set_moving_lpf_corner((uint8_t)c);
        else                kw307_set_stationary_lpf_corner((uint8_t)c);
    }
    /* HW Antenna and Mag type wait for FW 3.4.5, as their reads do: below it,
     * or before Check FW, neither is written. */
    else if ((verb == "antenna" || verb == "magtype") && !(_fw_major != 0xFFu &&
        ((uint32_t)_fw_major * 10000u + (uint32_t)_fw_minor * 100u + _fw_patch) >= 30405u)) {
        _cmdRc = KW307_ERR_NOT_SUPPORTED;
    }
    /* tx_mode 0~1, rx_mode 0~7, with_angle - the SDK checks the ranges. */
    else if (verb == "antenna") {
        const int tx = _parseInt(next(), -1), rx = _parseInt(next(), -1);
        const int wa = _parseInt(rest, -1);
        _cmdRc = (tx >= 0 && rx >= 0 && wa >= 0)
               ? kw307_set_antenna((uint8_t)tx, (uint8_t)rx, wa != 0) : KW307_ERR_PARAM;
    }
    /* Mag type, PID 0x53: its own setter - kw307_set_antenna() leaves it. */
    else if (verb == "magtype") {
        const int m = _parseInt(rest, -1);
        if (m >= 0) kw307_set_mag_type((uint8_t)m);
    }
    /* The first-stage background switches, both bands (SDK 2.17.1 7839f7c
     * renamed them from analog_hpf / analog_hpf_swcap; the verbs keep their
     * names). */
    else if (verb == "ahpf")    { kw307_en_background_subtraction(_parseInt(rest, 1) != 0); }
    else if (verb == "ahpfsw")  { kw307_en_background_update(_parseInt(rest, 1) != 0); }
    else if (verb == "eqgain")  { const int g = _parseInt(rest, -1); if (g >= 0) kw307_set_equalizer_gain((uint8_t)g); }
    else if (verb == "bbatt")   { const int g = _parseInt(rest, -1); if (g >= 0) kw307_set_baseband_att((uint8_t)g); }
    else if (verb == "fs")      { const int s = _parseInt(rest, -1); if (s >= 0) kw307_set_sample_rate((uint8_t)s); }
    else if (verb == "mbg")     { kw307_set_moving_background(_parseInt(rest, 1) != 0); }
    else if (verb == "sbg")     { kw307_set_stationary_background(_parseInt(rest, 1) != 0); }
    else if (verb == "menv")    { kw307_en_moving_env_subtraction(_parseInt(rest, 1) != 0); }
    else if (verb == "senv")    { kw307_en_stationary_env_subtraction(_parseInt(rest, 1) != 0); }
    else if (verb == "eqorder") { const int o = _parseInt(rest, -1); if (o >= 0) kw307_set_equalizer_order((uint8_t)o); }
    else if (verb == "dspgain") {
        const int g = _parseInt(rest, 0);
        if (g >= 0 && g <= 3) kw307_set_digital_gain((uint8_t)g);
    }

    /* ---- Tracker Optimization: one verb per SDK setter -----------------
     * los is above, shared with the settings that use it. */
    else if (verb == "stopt") {
        const int cf = _parseInt(next(), -1), sm = _parseInt(next(), -1);
        const int dc = _parseInt(rest, -1);
        if (cf >= 0 && sm >= 0 && dc >= 0)
            kw307_set_stationary_tracker_opt((uint8_t)cf, (int16_t)sm, (uint16_t)dc);
    }
    else if (verb == "mthr") {
        const int cf = _parseInt(next(), -1);
        const int sp = _parseInt(next(), 0), cl = _parseInt(next(), 0), su = _parseInt(rest, 0);
        if (cf >= 0) kw307_set_motion_threshold((uint8_t)cf, (int16_t)sp, (int16_t)cl, (int16_t)su);
    }
    else if (verb == "msmooth") {
        const int sm = _parseInt(rest, -1);
        if (sm >= 0) kw307_set_moving_smoothing((int16_t)sm);
    }
    else if (verb == "fovmin") {
        const int d = _parseInt(rest, -1);
        if (d >= 0) kw307_set_fov_min((int16_t)d);
    }

}

/* ============================================================
   The dashboard page

   One page. The header carries the PROGMEM definition of _dashboardHtml itself,
   which is why the include sits exactly where that definition used to.
   ============================================================ */
#include "dashboard_expert.h"
