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
 * KW307 SDK - Arduino HardwareSerial Port Implementation
 * File: Arduino/port_arduino.cpp
 *
 * Single-instance static state - matches SDK singleton design.
 */

#include "port_arduino.h"
#include "user_config.h"
#include <Arduino.h>

static HardwareSerial* s_serial = nullptr;
static bool            s_open   = false;

/* ================================================================
   C++ binding helpers
   ================================================================ */

void kw307_port_arduino_bind(HardwareSerial& serial)
{
    s_serial = &serial;
}

void kw307_port_arduino_begin(HardwareSerial& serial, int rx_pin, int tx_pin)
{
#if defined(ESP32)
    /* ESP32 core accepts pin mapping in begin(). */
    serial.begin(KW307_ARDUINO_UART_BAUD, SERIAL_8N1, rx_pin, tx_pin);
#else
    (void)rx_pin; (void)tx_pin;
    serial.begin(KW307_ARDUINO_UART_BAUD);
#endif
    kw307_port_arduino_bind(serial);
}

/* ================================================================
   C interface expected by kw307_uart layer
   ================================================================ */

extern "C" {

int kw307_port_open(const char* port_name, int baud_rate)
{
    (void)port_name;   /* Arduino port picks HardwareSerial via bind(). */
    (void)baud_rate;   /* Baud is applied by begin() / user. */

    if (!s_serial) return -1;
    s_open = true;
    return 0;
}

void kw307_port_close(void)
{
    /* Don't call Serial.end() - the user may still want debug output
     * on another Serial. Close is a logical state flip only. */
    s_open = false;
}

int kw307_port_send(const uint8_t* buf, int len)
{
    if (!s_open || !s_serial || len <= 0 || !buf) return -1;

    size_t written = s_serial->write(buf, (size_t)len);
    s_serial->flush();   /* drain TX FIFO so frame egress is complete */
    return (int)written;
}

int kw307_port_recv(uint8_t* buf, int max_len, int timeout_ms)
{
    if (!s_open || !s_serial || max_len <= 0 || !buf) return -1;

    int      count = 0;
    uint32_t start = millis();

    /* Wait up to timeout_ms for the first byte. */
    if (timeout_ms > 0) {
        while (s_serial->available() == 0) {
            if ((millis() - start) >= (uint32_t)timeout_ms) break;
            delay(1);
        }
    }

    /* Drain whatever is available, up to max_len. */
    while (count < max_len && s_serial->available() > 0) {
        int b = s_serial->read();
        if (b < 0) break;
        buf[count++] = (uint8_t)b;
    }
    return count;
}

int kw307_port_is_open(void)
{
    return s_open ? 1 : 0;
}

} /* extern "C" */
