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
 * KW307 SDK - PC Win32 User Configuration
 * File: example_platform/pc_win32/user_config.h
 *
 * *** Edit this file to match your hardware. ***
 *
 * Usage:
 *   kw307_init();   // automatically reads KW307_DEFAULT_PORT_NAME / BAUD_RATE
 *
 * To override at runtime, pass non-NULL/non-zero arguments to the lower-level
 * kw307_port_open() — the values here are only the fallback defaults.
 */

#ifndef KW307_USER_CONFIG_PC_WIN32_H
#define KW307_USER_CONFIG_PC_WIN32_H

/* ----------------------------------------------------------------
   COM port name
   COM1~COM9 : "COM3"
   COM10+    : "\\\\.\\COM10"     (the port_win32.c implementation
                                   adds the \\.\ prefix automatically,
                                   so plain "COM10" also works.)
   ---------------------------------------------------------------- */
#define KW307_DEFAULT_PORT_NAME   "COM3"

/* ----------------------------------------------------------------
   Baud rate (must match KW307 MCU firmware setting, typically 115200)
   ---------------------------------------------------------------- */
#define KW307_DEFAULT_BAUD_RATE   115200

#endif /* KW307_USER_CONFIG_PC_WIN32_H */
