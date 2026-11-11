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
 * KW307 SDK - STM32 HAL User Configuration
 * File: example_platform/STM32_HAL/user_config.h
 *
 * Edit this file to match your STM32 project. Works with any STM32 family
 * (F0/F1/F3/F4/F7/G0/G4/H7/L0/L4/U5) as long as the project is generated
 * with CubeMX and uses the standard HAL UART driver.
 */

#ifndef KW307_USER_CONFIG_STM32_HAL_H
#define KW307_USER_CONFIG_STM32_HAL_H

/* ----------------------------------------------------------------
   Bring in UART_HandleTypeDef + the huartX instance from your project.
   "main.h" is the standard CubeMX-generated header that transitively
   includes the right stm32XXxx_hal.h for your family and declares the
   huartX globals.
   ---------------------------------------------------------------- */
#include "main.h"

/* ----------------------------------------------------------------
   UART peripheral handle pointer
   Change &huart1 to the HAL instance connected to the KW307 module.
   Typical CubeMX-generated names: huart1 / huart2 / huart3 / huart6
   ---------------------------------------------------------------- */
extern UART_HandleTypeDef KW307_STM32_UART_NAME;
#define KW307_STM32_UART_HANDLE   (&KW307_STM32_UART_NAME)

/* Replace `huart1` below with the actual CubeMX handle name. */
#define KW307_STM32_UART_NAME     huart1

/* ----------------------------------------------------------------
   Baud rate (must match KW307 firmware: 115200)
   CubeMX must configure the UART at the same baud.
   ---------------------------------------------------------------- */
#define KW307_STM32_UART_BAUD     115200

/* ----------------------------------------------------------------
   RX ring buffer size
   MUST be a power of 2: 64 / 128 / 256 / 512 / ...
   128 is sufficient for the default radar frame.
   ---------------------------------------------------------------- */
#define KW307_UART_RX_BUFSIZE     128u

#endif /* KW307_USER_CONFIG_STM32_HAL_H */
