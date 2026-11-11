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
 * KW307 SDK - Windows PC Platform Implementation (Win32 COM)
 * File: example_platform/pc_win32/port_win32.c
 *
 * Implements the 5 global functions declared in core/KW307_port.h.
 * COM port name and baud rate are read from user_config.h
 * (KW307_DEFAULT_PORT_NAME / KW307_DEFAULT_BAUD_RATE). Parameters passed
 * to kw307_port_open() override these when non-NULL / non-zero; otherwise
 * the user_config.h defaults apply.
 *
 * Compatible with: MSVC, MinGW, C++ Builder (Embarcadero RAD Studio),
 * Clang/LLVM Windows. No C++ syntax — pure C against Win32 SDK.
 */

#include <windows.h>
#include <string.h>
#include "KW307_port.h"     /* add KW307_SDK core to include path */
#include "user_config.h"
#include "kw307_example_win32.h"  /* declarations for the two PC helpers below */

static HANDLE s_hComm = INVALID_HANDLE_VALUE;

/* ================================================================
   Optional PC helpers — NOT part of the KW307_port.h HAL (the 5
   required functions). Convenience for PC host apps with a COM
   picker UI. MCU/other ports do not implement these.
   ================================================================ */

/* Runtime port/baud selection. Set this (before kw307_init()) from a UI combo;
   kw307_port_open(NULL,0) then uses it, falling back to user_config.h. */
static char s_sel_port[32] = "";
static int  s_sel_baud     = 0;

void kw307_example_set_port(const char* name, int baud)
{
    if (name && name[0]) { strncpy(s_sel_port, name, sizeof(s_sel_port) - 1);
                           s_sel_port[sizeof(s_sel_port) - 1] = '\0'; }
    else                 { s_sel_port[0] = '\0'; }
    s_sel_baud = baud;
}

/* ================================================================
   kw307_port_open
   ================================================================ */
int kw307_port_open(const char* port_name, int baud_rate)
{
    DCB          dcb      = {0};
    COMMTIMEOUTS timeouts = {0};

    /* Resolve port/baud: explicit arg > UI runtime selection (kw307_example_set_port)
     * > user_config.h default. kw307_init() calls this with NULL/0. */
    if (!port_name || port_name[0] == '\0')
        port_name = (s_sel_port[0] ? s_sel_port : KW307_DEFAULT_PORT_NAME);
    if (baud_rate <= 0)
        baud_rate = (s_sel_baud > 0 ? s_sel_baud : KW307_DEFAULT_BAUD_RATE);

    /* Convert char* -> wchar_t* and prepend \\.\ device path prefix.
     * "COM1"~"COM9" work without it, but COM10+ require it.
     * Using \\.\ always is safe and covers all port numbers. */
    wchar_t wport[72] = {0};
    if (port_name[0] != '\\') {
        wchar_t tmp[32] = {0};
        MultiByteToWideChar(CP_ACP, 0, port_name, -1, tmp, 32);
        wsprintfW(wport, L"\\\\.\\%s", tmp);
    } else {
        MultiByteToWideChar(CP_ACP, 0, port_name, -1, wport, 72);
    }

    s_hComm = CreateFileW(
        wport,
        GENERIC_READ | GENERIC_WRITE,
        0,              /* exclusive access */
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    if (s_hComm == INVALID_HANDLE_VALUE)
        return -1;

    /* DCB: 8N1, no flow control */
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(s_hComm, &dcb)) goto fail;

    dcb.BaudRate     = (DWORD)baud_rate;
    dcb.ByteSize     = 8;
    dcb.StopBits     = ONESTOPBIT;
    dcb.Parity       = NOPARITY;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fRtsControl  = RTS_CONTROL_DISABLE;

    if (!SetCommState(s_hComm, &dcb)) goto fail;

    /* Timeouts: non-blocking ReadFile (return immediately with whatever bytes
     * are available, 0 if none). Blocking waits are paced in kw307_port_recv()
     * by Sleep(timeout_ms), honouring the KW307_port.h timeout_ms contract. */
    timeouts.ReadIntervalTimeout         = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier  = 0;
    timeouts.ReadTotalTimeoutConstant    = 0;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant   = 100;
    if (!SetCommTimeouts(s_hComm, &timeouts)) goto fail;

    PurgeComm(s_hComm, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return 0;

fail:
    CloseHandle(s_hComm);
    s_hComm = INVALID_HANDLE_VALUE;
    return -1;
}

/* ================================================================
   kw307_port_close
   ================================================================ */
void kw307_port_close(void)
{
    if (s_hComm != INVALID_HANDLE_VALUE && s_hComm != NULL) {
        PurgeComm(s_hComm, PURGE_RXCLEAR | PURGE_TXCLEAR);
        CloseHandle(s_hComm);
        s_hComm = INVALID_HANDLE_VALUE;
    }
}

/* ================================================================
   kw307_port_send
   ================================================================ */
int kw307_port_send(const uint8_t* buf, int len)
{
    DWORD bw = 0;

    if (s_hComm == INVALID_HANDLE_VALUE) return -1;
    if (len <= 0) return 0;

    if (!WriteFile(s_hComm, buf, (DWORD)len, &bw, NULL)) {
        /* USB-serial unplug / device removal / driver tear-down -> the handle
         * is now stale. Close it and mark the port closed so kw307_is_open()
         * tells the truth and the app can reconnect via kw307_init(). */
        CloseHandle(s_hComm);
        s_hComm = INVALID_HANDLE_VALUE;
        return -1;
    }

    return (int)bw;
}

/* ================================================================
   kw307_port_recv  — honour the KW307_port.h contract on both max_len
   and timeout_ms. COMMTIMEOUTS are set non-blocking, so ReadFile returns
   instantly with whatever bytes are available (0 if none). Drain up to
   max_len bytes per call so a single poll can swallow a whole frame —
   the framing layer in KW307_uart.c passes a 64-byte buffer expecting
   the contract to be honoured. Pacing:
     timeout_ms == 0 : non-blocking pump (used by kw307_update() in main
                       loops / worker threads). Returns immediately, no sleep.
     timeout_ms  > 0 : block up to timeout_ms ms (used by
                       kw307_read_blocking's iteration-paced wait loop —
                       gives the MCU response wall-clock time to arrive).
   ================================================================ */
int kw307_port_recv(uint8_t* buf, int max_len, int timeout_ms)
{
    DWORD br = 0;

    if (s_hComm == INVALID_HANDLE_VALUE) return -1;
    if (max_len <= 0) return 0;

    if (!ReadFile(s_hComm, buf, (DWORD)max_len, &br, NULL)) {
        /* Same disconnect handling as kw307_port_send — invalidate the handle
         * so the open-state reflects reality and the caller can reconnect. */
        CloseHandle(s_hComm);
        s_hComm = INVALID_HANDLE_VALUE;
        return -1;
    }

    if (br == 0 && timeout_ms > 0)
        Sleep((DWORD)timeout_ms);

    return (int)br;
}

/* ================================================================
   kw307_port_is_open
   ================================================================ */
int kw307_port_is_open(void)
{
    return (s_hComm != INVALID_HANDLE_VALUE && s_hComm != NULL) ? 1 : 0;
}

/* ================================================================
   kw307_example_list_ports  — optional PC helper (not HAL).
   Enumerate the COM ports currently present from
   HKLM\HARDWARE\DEVICEMAP\SERIALCOMM (value DATA = "COMx").
   Fills out[0..n-1] with NUL-terminated "COMx" strings; returns count.
   Pure Win32 (no VCL) so console and GUI examples can both use it.
   ================================================================ */
int kw307_example_list_ports(char out[][16], int max)
{
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "HARDWARE\\DEVICEMAP\\SERIALCOMM",
                      0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return 0;

    int   count = 0;
    DWORD idx   = 0;
    for (;;) {
        char  valName[256];
        BYTE  valData[256];
        DWORD nName = sizeof(valName);
        DWORD nData = sizeof(valData);
        DWORD type  = 0;
        LONG  r = RegEnumValueA(hKey, idx++, valName, &nName, NULL, &type,
                                valData, &nData);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS)       continue;
        if (type == REG_SZ && count < max) {
            strncpy(out[count], (const char*)valData, 15);
            out[count][15] = '\0';
            count++;
        }
    }
    RegCloseKey(hKey);
    return count;
}
