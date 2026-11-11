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
 * KW307 SDK - Arduino / ESP32 HardwareSerial Port
 * File: Arduino/port_arduino.h
 *
 * Implements KW307_port.h over Arduino HardwareSerial. Tested on
 * Any Arduino core exposing a
 * HardwareSerial that implements Stream.
 *
 * USAGE (in your .ino):
 *
 *   #include "port_arduino.h"
 *
 *   void setup() {
 *       // Bind SDK to a HardwareSerial AND start it in one call.
 *       // ESP32: pick pins for RX / TX.
 *       kw307_port_arduino_begin(Serial1, 18, 17);
 *
 *       kw307_init();
 *       // Scene preset goes here - copy the switch{} from
 *       // example_platform/Nuvoton_M251/main.c (or another port).
 *       kw307_on_frame(on_radar);           // MCU auto-streams frames at power-on
 *       // ...
 *   }
 *
 * On other Arduino cores that don't accept RX/TX pins in Serial.begin(),
 * call Serial1.begin(115200) yourself and then kw307_port_arduino_bind(Serial1).
 */

#ifndef PORT_ARDUINO_H
#define PORT_ARDUINO_H

#include "KW307_port.h"

#ifdef __cplusplus
#include <HardwareSerial.h>

/**
 * Bind the SDK to an already-configured HardwareSerial.
 * Call BEFORE kw307_init(). Serial.begin(...) must be done by you.
 */
void kw307_port_arduino_bind(HardwareSerial& serial);

/**
 * Convenience helper: bind + begin in one call. The pin arguments are
 * used on ESP32 cores, whose begin() takes them; elsewhere the pins are
 * whatever the core has already assigned to that HardwareSerial.
 * Uses baud = KW307_ARDUINO_UART_BAUD (see user_config.h).
 * rx_pin / tx_pin are passed to HardwareSerial::begin().
 */
void kw307_port_arduino_begin(HardwareSerial& serial, int rx_pin, int tx_pin);

#endif /* __cplusplus */

#endif /* PORT_ARDUINO_H */
