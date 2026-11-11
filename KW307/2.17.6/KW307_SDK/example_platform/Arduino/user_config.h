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
 * KW307 SDK - Arduino User Configuration
 * File: Arduino/user_config.h
 *
 * * Edit this file in your sketch folder to match your wiring. *
 *
 * Default baud rate is 115200 (matches KW307 UART0 factory default).
 * The HardwareSerial instance and pin assignment are selected in your
 * .ino file via kw307_port_arduino_bind(Serial1, RX, TX)
 * (declared in port_arduino.h).
 */

#ifndef KW307_USER_CONFIG_ARDUINO_H
#define KW307_USER_CONFIG_ARDUINO_H

/* Baud rate must match the KW307 firmware setting (default 115200). */
#define KW307_ARDUINO_UART_BAUD   115200UL

#endif /* KW307_USER_CONFIG_ARDUINO_H */
