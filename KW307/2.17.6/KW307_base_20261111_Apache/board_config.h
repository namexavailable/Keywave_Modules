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
 * board_config.h — which carrier board this sketch is wired to
 *
 * Pick your board on the ONE line marked below. Nothing else in the sketch
 * needs touching when you swap boards.
 *
 *===========================================================================
 * WHAT THIS FILE CAN AND CANNOT DO — read this once and the split stops
 * feeling arbitrary
 *===========================================================================
 * Two kinds of setting decide whether a board works, and only one of them can
 * live in source code:
 *
 *   IN HERE, automatic once you pick the board:
 *       which HardwareSerial, which two pins, and whether the PA needs capping.
 *       These are things the program does at run time, so the program can hold
 *       them.
 *
 *   IN THE ARDUINO IDE, manual every time — Tools menu:
 *       Flash Mode, Flash Size, PSRAM, Partition Scheme, USB CDC On Boot.
 *       These choose how the binary is BUILT and how the bootloader is told to
 *       read the flash. They are decided before a line of this file compiles,
 *       so no #define here can reach them. The table below is the whole list;
 *       there is nothing hidden.
 *
 * So: one line here, plus one pass through the Tools menu. That is the entire
 * per-board procedure.
 *
 *===========================================================================
 * THE TABLE — Arduino IDE ▸ Tools, per board
 *===========================================================================
 *
 *   KW307_BOARD           Tools ▸ Board        Flash  Size  PSRAM  CDC   USB Mode
 *   ------------------------------------------------------------------------
 *   1 S3_SUPERMINI        ESP32S3 Dev Module    QIO    4MB   QSPI   On    HW CDC+JTAG
 *   2 C3_LUATOS           ESP32C3 Dev Module   *DIO*   4MB    -     On       -
 *   3 C3_SUPERMINI        Nologo ESP32C3 SM     QIO    4MB    -     On       -
 *   4 WROOM32E            ESP32 Dev Module      QIO    4MB   Off     -       -
 *   5 C3_DEVKITM1         ESP32C3 Dev Module    QIO    4MB    -   *Off*      -
 *   6 NODEMCU32S          ESP32 Dev Module      QIO    4MB   Off     -       -
 *
 *   Partition Scheme: Huge APP (3MB No OTA / 1MB SPIFFS) on ALL of them.
 *   "-" means the menu does not exist for that chip. "CDC" is USB CDC On Boot.
 *
 *   The three marked * are the ones that bite:
 *     - C3 LuatOS must be DIO. On QIO it does not run at all.
 *     - C3 DevKitM-1 must have CDC On Boot OFF. Its USB socket goes through a
 *       USB-UART bridge into UART0; the chip's own USB pins (18/19) reach only
 *       the header. Turn CDC on and Serial talks to a port nothing is plugged
 *       into - the sketch runs fine and the Serial Monitor stays empty.
 *     - S3 Super Mini must be QSPI PSRAM and QIO, never OPI. Its eFuses read
 *       back "quad (4 data lines)" and "AP_3v3"; OPI is a different part.
 *   Partition Scheme is not optional either — the default 1.2MB APP is ~97%
 *   full with this build.
 *
 *===========================================================================
 * A board named here that does not match the chip selected in the IDE is a
 * compile error rather than a runtime mystery. The guard can only see the chip,
 * though — it cannot tell the two C3 boards apart, and their pin maps differ.
 * That is exactly why the choice below is explicit and has no default.
 */

#ifndef KW307_BOARD_CONFIG_H
#define KW307_BOARD_CONFIG_H

#ifdef __cplusplus
#include <Arduino.h>   /* brings in CONFIG_IDF_TARGET_* from sdkconfig.h */
#endif

/* ----- The boards this sketch knows ------------------------------------ */
#define KW307_BOARD_ESP32S3_SUPERMINI   1
#define KW307_BOARD_ESP32C3_LUATOS      2
#define KW307_BOARD_ESP32C3_SUPERMINI   3
#define KW307_BOARD_ESP32_WROOM32E      4
#define KW307_BOARD_ESP32C3_DEVKITM1    5
#define KW307_BOARD_NODEMCU32S          6

/*===========================================================================
 *   >>>  PICK YOUR BOARD HERE  <<<   (this line, and the Tools menu above)
 *===========================================================================*/
#ifndef KW307_BOARD
#define KW307_BOARD   KW307_BOARD_ESP32S3_SUPERMINI
#endif


/*===========================================================================
 * 1  ESP32-S3 Super Mini
 *===========================================================================
 * Right-hand header, read from the top:  5V  GND  3V3  13  12  11  10  9
 * 13/12 sit directly under the 3V3/GND pair, so all four radar wires land on
 * one edge of the board with no jumper crossing to the other side.
 *
 * Other pins on this chip: avoid 26~32 (SPI flash), 33~37 (octal PSRAM),
 * 0 / 3 / 45 / 46 (strapping) and 19 / 20 (native USB).
 */
#if   KW307_BOARD == KW307_BOARD_ESP32S3_SUPERMINI

  #if !defined(CONFIG_IDF_TARGET_ESP32S3)
    #error "KW307_BOARD says ESP32-S3 Super Mini, but the IDE is not building for an ESP32-S3."
  #endif

  #define KW307_BOARD_NAME   "ESP32-S3 Super Mini"
  #define KW307_BOARD_TAG    "s3mini"   /* radio name + hostname suffix */
  #define KW307_AP_CHANNEL   1          /* 1 / 6 / 11 do not overlap */
  #define RADAR_SERIAL       Serial1
  #define RADAR_RX_PIN       12    /* GPIO receiving from module TX */
  #define RADAR_TX_PIN       13    /* GPIO driving module RX        */
  /* Antenna is fine at stock power — no KW307_AP_TX_POWER. */

/*===========================================================================
 * 2  ESP32-C3 LuatOS  (合宙 ESP32C3-CORE)
 *===========================================================================
 * GPIO0/GPIO1 are this board's labelled UART1 pair and both sit on the
 * vendor's recommended list. Neither is a strapping pin — on the C3 those are
 * 2, 8 and 9, and 9 is also the BOOT button here.
 *
 * Do NOT reuse the S3 Super Mini's 12/13: on this board they drive the on-board
 * LEDs D4/D5. Also spoken for: 14~17 SPI flash, 18/19 native USB, 20/21 UART0
 * console, 11 flash VDD.
 *
 * FLASH MODE MUST BE DIO for this board. That is an IDE setting, not something
 * this file can enforce — see the table at the top. On QIO it will not run.
 *
 * Full TX power. The PA cap belongs to the C3 SuperMini below, not to this
 * board; capping here would only throw away range.
 */
#elif KW307_BOARD == KW307_BOARD_ESP32C3_LUATOS

  #if !defined(CONFIG_IDF_TARGET_ESP32C3)
    #error "KW307_BOARD says ESP32-C3 LuatOS, but the IDE is not building for an ESP32-C3."
  #endif

  #define KW307_BOARD_NAME   "ESP32-C3 LuatOS"
  #define KW307_BOARD_TAG    "c3luat"   /* radio name + hostname suffix */
  #define KW307_AP_CHANNEL   11         /* 1 / 6 / 11 do not overlap */
  #define RADAR_SERIAL       Serial1
  #define RADAR_RX_PIN       1     /* GPIO receiving from module TX (U1RX) */
  #define RADAR_TX_PIN       0     /* GPIO driving module RX        (U1TX) */

/*===========================================================================
 * 3  ESP32-C3 SuperMini
 *===========================================================================
 * Radar on GPIO20 / GPIO21. Those are the ESP32-C3's UART0 pins, and they are
 * free to take because this build puts the console on the chip's native USB
 * (CDCOnBoot=cdc in the FQBN), so nothing else drives them once the sketch is
 * running. The ROM still prints its boot banner on them at reset -- the module
 * sees a few bytes of noise then, and discards them for want of a valid frame.
 *
 * GPIO4 / GPIO3 were used until 2026-09-29. They sit next to GND / 3V3 on the
 * left header, which made a tidy four-pin cable, but that is a convenience and
 * the board on the bench is wired to 20 / 21.
 *
 * THIS is the board with the antenna defect. softAP() returns OK, the console
 * prints an IP, the AP interface comes up — and no phone in the room can see
 * the SSID. The radio is transmitting; it is transmitting badly. These clones
 * pair a small ceramic antenna with too little ground clearance, and at the
 * stock ~20 dBm the PA is driven harder than the match can carry, so the beacon
 * goes out distorted rather than loud. 8.5 dBm trades range for a beacon that
 * is actually decodable.
 *
 * Applied AFTER softAP() — softAP() resets TX power to maximum, so setting it
 * earlier is silently undone. See connectAP() in KW307WiFi.cpp.
 */
#elif KW307_BOARD == KW307_BOARD_ESP32C3_SUPERMINI

  #if !defined(CONFIG_IDF_TARGET_ESP32C3)
    #error "KW307_BOARD says ESP32-C3 SuperMini, but the IDE is not building for an ESP32-C3."
  #endif

  #define KW307_BOARD_NAME   "ESP32-C3 SuperMini"
  #define KW307_BOARD_TAG    "c3mini"   /* radio name + hostname suffix */
  #define KW307_AP_CHANNEL   11         /* 1 / 6 / 11 do not overlap */
  #define RADAR_SERIAL       Serial1
  #define RADAR_RX_PIN       20    /* GPIO receiving from module TX (U0RXD pin) */
  #define RADAR_TX_PIN       21    /* GPIO driving module RX        (U0TXD pin) */
  #define KW307_AP_TX_POWER  WIFI_POWER_8_5dBm

/*===========================================================================
 * 4  ESP32 WROOM-32E
 *===========================================================================
 * The original ESP32 has three UART controllers, so the radar gets UART2 on
 * its default pins rather than UART1 — note this is Serial2, the only board
 * here that is not Serial1.
 *
 * Console is a real USB-UART bridge on UART0, so there is no USB CDC setting
 * to get wrong. Avoid 6~11 (SPI flash) and 34~39 (input only, no pull-ups).
 */
#elif KW307_BOARD == KW307_BOARD_ESP32_WROOM32E

  #if !defined(CONFIG_IDF_TARGET_ESP32)
    #error "KW307_BOARD says ESP32 WROOM-32E, but the IDE is not building for a plain ESP32."
  #endif

  #define KW307_BOARD_NAME   "ESP32 WROOM-32E"
  #define KW307_BOARD_TAG    "wroom"    /* radio name + hostname suffix */
  #define KW307_AP_CHANNEL   1          /* 1 / 6 / 11 do not overlap */
  #define RADAR_SERIAL       Serial2
  #define RADAR_RX_PIN       16    /* UART2 RX2, receives from module TX */
  #define RADAR_TX_PIN       17    /* UART2 TX2, drives module RX        */

/*===========================================================================
 * 5  ESP32-C3-DevKitM-1  (Espressif official, ESP32-C3-MINI-1 module)
 *===========================================================================
 * Same two pins as the LuatOS board — GPIO0/GPIO1 are free on the header here
 * too, and neither is a strapping pin. What differs is entirely in the IDE.
 *
 * THE ONE THAT CATCHES PEOPLE: USB CDC On Boot must be DISABLED. Unlike every
 * other board in this file, the USB socket is not the chip's own USB — it is a
 * USB-UART bridge wired to UART0 (GPIO20/21). The chip's native USB pins,
 * 18/19, are brought out to the header and go nowhere near the connector.
 * Enable CDC and Serial gets pointed at those unconnected pins: the sketch
 * runs, the radar works, and the Serial Monitor prints nothing at all.
 *
 * The bridge also means the COM port is held by a separate chip, so it stays
 * put even while the ESP32-C3 reboots — no port flicker when firmware is bad.
 *
 * Also spoken for: 8 is the RGB LED, 9 the BOOT button, 20/21 the console.
 */
#elif KW307_BOARD == KW307_BOARD_ESP32C3_DEVKITM1

  #if !defined(CONFIG_IDF_TARGET_ESP32C3)
    #error "KW307_BOARD says ESP32-C3-DevKitM-1, but the IDE is not building for an ESP32-C3."
  #endif

  #define KW307_BOARD_NAME   "ESP32-C3-DevKitM-1"
  #define KW307_BOARD_TAG    "c3dev"    /* radio name + hostname suffix */
  #define KW307_AP_CHANNEL   11         /* 1 / 6 / 11 do not overlap */
  #define RADAR_SERIAL       Serial1
  #define RADAR_RX_PIN       1     /* GPIO receiving from module TX (U1RX) */
  #define RADAR_TX_PIN       0     /* GPIO driving module RX        (U1TX) */

/*===========================================================================
 * 6  NodeMCU-32S  (Ai-Thinker, ESP32-WROOM-32 module)
 *===========================================================================
 * The same chip and the same two pins as the WROOM-32E above -- UART2's own
 * RX2/TX2, which every plain ESP32 brings out. It is a separate entry rather
 * than a second name for that one because each board here owns an SSID and an
 * mDNS name, and two boards answering to "KW307-wroom" on one bench is exactly
 * the confusion the tag exists to prevent.
 *
 * TOOLS -> BOARD IS "ESP32 Dev Module", NOT "NodeMCU-32S". The core does carry
 * a NodeMCU-32S entry, and it is the wrong one to pick: it offers no Partition
 * Scheme menu, so the sketch is stuck on the default 1.2 MB app, which the
 * expert build overflows. ESP32 Dev Module is the same chip with the menus
 * present, and Huge APP is not optional here. flash.ps1 spells the same FQBN.
 *
 * Other pins on this chip: 6~11 are the SPI flash, 34~39 are input-only, and
 * 0 / 2 / 12 / 15 are strapping pins.
 */
#elif KW307_BOARD == KW307_BOARD_NODEMCU32S

  #if !defined(CONFIG_IDF_TARGET_ESP32)
    #error "KW307_BOARD says NodeMCU-32S, but the IDE is not building for a plain ESP32."
  #endif

  #define KW307_BOARD_NAME   "NodeMCU-32S"
  #define KW307_BOARD_TAG    "nodemcu"  /* radio name + hostname suffix */
  #define KW307_AP_CHANNEL   11         /* 1 / 6 / 11 do not overlap */
  #define RADAR_SERIAL       Serial2
  #define RADAR_RX_PIN       16    /* UART2 RX2, receives from module TX */
  #define RADAR_TX_PIN       17    /* UART2 TX2, drives module RX        */

#else
  #error "KW307_BOARD is set to a value board_config.h does not know."
#endif


/*===========================================================================
 * Derived — one radio per board, so the bench can run several at once
 *===========================================================================
 * Every board gets its own SSID and its own http://<name>.local, both built
 * from the one KW307_BOARD_TAG above so they cannot drift apart.
 *
 *     KW307-s3mini   http://kw307-s3mini.local    channel 1
 *     KW307-c3luat   http://kw307-c3luat.local    channel 11
 *     KW307-c3mini   http://kw307-c3mini.local    channel 11
 *     KW307-wroom    http://kw307-wroom.local     channel 1
 *     KW307-c3dev    http://kw307-c3dev.local     channel 11
 *     KW307-nodemcu  http://kw307-nodemcu.local   channel 11
 *
 * The hostname matters as much as the SSID: mDNS hands a name to whichever
 * board answers first, so two boards both called kw307.local means the link
 * opens one of them at random and looks perfectly normal doing it.
 *
 * Channels are spaced 1 / 6 / 11 because those are the only three 2.4 GHz
 * channels that do not overlap. Boards sharing a channel still work — they
 * just share airtime, which shows up as a lower push rate on the status bar
 * rather than as an error.
 *
 * Password is shared on purpose: one thing to remember, and the AP is a bench
 * tool. Override it before this leaves the bench.
 */
#define KW307_AP_SSID   "KW307-" KW307_BOARD_TAG

#ifndef KW307_AP_PASS
  #define KW307_AP_PASS "12345678"
#endif

#endif /* KW307_BOARD_CONFIG_H */
