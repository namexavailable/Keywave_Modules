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
 * KW307 SDK - Linux Platform Implementation (POSIX termios)
 * File: example_platform/pc_linux/port_linux.c
 *
 * Implements the 5 functions declared in core/KW307_port.h, so every other SDK
 * layer compiles unchanged. Device and baud come from user_config.h
 * (KW307_DEFAULT_PORT_NAME / KW307_DEFAULT_BAUD_RATE); arguments passed to
 * kw307_port_open() override them, and kw307_example_set_port() overrides at
 * runtime — the same three-level resolution the Windows port uses.
 *
 * Portable POSIX: PC / IPC / embedded x86, Raspberry Pi, Jetson, BeagleBone.
 * Works on a USB-serial adapter (/dev/ttyUSB0, /dev/ttyACM0) and on an SoC's own
 * UART pins (/dev/serial0, /dev/ttyAMA0) without changes.
 *
 * Build: any GCC/Clang, C99, nothing beyond libc.
 */

/* Feature macros FIRST — before any header.
 *
 * CRTSCTS (and cfmakeraw, which this file deliberately avoids) are BSD/GNU
 * extensions, not POSIX. Building with -std=c99 makes glibc define
 * __STRICT_ANSI__, which hides them and leaves CRTSCTS undeclared. Asking for
 * the default feature set brings them back without giving up -std=c99. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1     /* glibc >= 2.19 */
#endif
#ifndef _BSD_SOURCE
#define _BSD_SOURCE 1         /* older glibc */
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <poll.h>
#include <dirent.h>

#include "KW307_port.h"              /* add <sdk>/core to the include path */
#include "kw307_example_linux.h"     /* the two host helpers below         */
#include "user_config.h"             /* your per-project defaults          */

/* Some libcs only expose these under wider feature sets. Absent means there is
 * no such bit to clear, so a 0 fallback is correct rather than a workaround. */
#ifndef CRTSCTS
#define CRTSCTS 0
#endif
#ifndef IXANY
#define IXANY 0
#endif

/* ================================================================
   Internal state
   ================================================================ */
static int s_fd = -1;

/* ================================================================
   Host helpers — OUTSIDE the KW307_port.h HAL.
   Declared in kw307_example_linux.h. A host app with a device picker wants
   them; an MCU port never does.
   ================================================================ */

static char s_sel_port[128] = "";
static int  s_sel_baud      = 0;

void kw307_example_set_port(const char* name, int baud)
{
    if (name && name[0]) {
        strncpy(s_sel_port, name, sizeof(s_sel_port) - 1);
        s_sel_port[sizeof(s_sel_port) - 1] = '\0';
    } else {
        s_sel_port[0] = '\0';
    }
    s_sel_baud = baud;
}

int kw307_example_list_ports(char out[][KW307_EXAMPLE_PORT_NAME_MAX], int max)
{
    /* Serial devices carry no single naming rule on Linux, so match the prefixes
     * that actually appear, most-likely first: the distro-maintained symlink,
     * then USB bridges, then CDC-ACM, then an SoC's own UARTs. Scanning per
     * prefix (rather than sorting afterwards) makes the order deterministic, so
     * a picker's index means the same thing between runs. */
    static const char* const prefixes[] = { "serial", "ttyUSB", "ttyACM", "ttyAMA" };
    int n = 0;
    size_t p;

    if (!out || max <= 0) return 0;

    for (p = 0; p < sizeof(prefixes) / sizeof(prefixes[0]) && n < max; p++) {
        DIR* d = opendir("/dev");
        struct dirent* e;
        if (!d) return n;                   /* no /dev: nothing to report */

        while ((e = readdir(d)) != NULL && n < max) {
            size_t plen = strlen(prefixes[p]);
            if (strncmp(e->d_name, prefixes[p], plen) != 0) continue;
            if (e->d_name[plen] == '\0') continue;      /* want a numbered suffix */
            /* "/dev/" + name + NUL must fit the caller's slot. */
            if (5u + strlen(e->d_name) + 1u > (size_t)KW307_EXAMPLE_PORT_NAME_MAX)
                continue;
            snprintf(out[n], KW307_EXAMPLE_PORT_NAME_MAX, "/dev/%s", e->d_name);
            n++;
        }
        closedir(d);
    }
    return n;
}

/* Map an integer baud to its termios constant. Only the rates the module
 * supports are listed; anything else is rejected rather than silently running at
 * the wrong speed. */
static int baud_to_speed(int baud, speed_t* out)
{
    switch (baud) {
        case 9600:   *out = B9600;   return 0;
        case 19200:  *out = B19200;  return 0;
        case 38400:  *out = B38400;  return 0;
        case 57600:  *out = B57600;  return 0;
        case 115200: *out = B115200; return 0;
        case 230400: *out = B230400; return 0;
        case 460800: *out = B460800; return 0;
        case 921600: *out = B921600; return 0;
        default:     return -1;
    }
}

/* A transport failure that will not fix itself (adapter unplugged, tty revoked).
 * The HAL contract requires the port to close itself then, so
 * kw307_port_is_open() reports reality and the SDK does not loop on a dead
 * handle. EAGAIN/EINTR are normal and must NOT close anything. */
static int fatal_errno(int e)
{
    return (e == EIO || e == ENXIO || e == ENODEV || e == EBADF || e == ENOTTY);
}

/* ================================================================
   1. kw307_port_open
   ================================================================ */
int kw307_port_open(const char* port_name, int baud_rate)
{
    struct termios tio;
    speed_t        speed;

    if (s_fd >= 0) kw307_port_close();   /* re-open is allowed */

    if (!port_name || port_name[0] == '\0')
        port_name = (s_sel_port[0] ? s_sel_port : KW307_DEFAULT_PORT_NAME);
    if (baud_rate <= 0)
        baud_rate = (s_sel_baud > 0 ? s_sel_baud : KW307_DEFAULT_BAUD_RATE);

    if (baud_to_speed(baud_rate, &speed) != 0) {
        fprintf(stderr, "kw307: unsupported baud rate %d\n", baud_rate);
        return -2;
    }

    /* O_NOCTTY: never let this UART become our controlling terminal — without
     * it, opening an SoC's own UART can hand the process a tty that then
     * delivers signals on line events. O_NONBLOCK: do not wait for DCD. */
    s_fd = open(port_name, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (s_fd < 0) {
        fprintf(stderr, "kw307: cannot open %s: %s\n", port_name, strerror(errno));
        return -1;
    }

    if (tcgetattr(s_fd, &tio) != 0) {
        fprintf(stderr, "kw307: tcgetattr(%s): %s\n", port_name, strerror(errno));
        close(s_fd); s_fd = -1;
        return -3;
    }

    /* Raw 8N1, no flow control, no line discipline of any kind. This matters:
     * the KW307 wire protocol is binary and contains 0x00, 0x0A, 0x0D, 0x11 and
     * 0x13 bytes, so canonical mode, NL/CR translation and XON/XOFF would all
     * corrupt frames.
     *
     * Set by hand rather than with cfmakeraw(): that function is a BSD/GNU
     * extension, so this file stays buildable under a strict -std= with any
     * libc. These are the same bits cfmakeraw() clears, plus IXOFF/IXANY —
     * cfmakeraw only clears IXON, which would leave the module able to pause OUR
     * transmitter with a 0x13 byte inside its own payload. */
    tio.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP |
                     INLCR | IGNCR | ICRNL |
                     IXON | IXOFF | IXANY);
    tio.c_oflag &= ~OPOST;
    tio.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tio.c_cflag |=  (CLOCAL | CREAD);    /* ignore modem lines, enable receiver */
    tio.c_cflag &= ~CRTSCTS;             /* no hardware flow control            */
    tio.c_cflag &= ~CSTOPB;              /* 1 stop bit                          */
    tio.c_cflag &= ~PARENB;              /* no parity                           */
    tio.c_cflag = (tio.c_cflag & ~CSIZE) | CS8;
    tio.c_cc[VMIN]  = 0;                 /* read() returns immediately;          */
    tio.c_cc[VTIME] = 0;                 /* poll() below does the waiting        */

    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);

    if (tcsetattr(s_fd, TCSANOW, &tio) != 0) {
        fprintf(stderr, "kw307: tcsetattr(%s): %s\n", port_name, strerror(errno));
        close(s_fd); s_fd = -1;
        return -4;
    }

    /* Drop whatever the module streamed before we were listening, so the parser
     * starts on a frame boundary instead of mid-frame. */
    tcflush(s_fd, TCIOFLUSH);
    return 0;
}

/* ================================================================
   2. kw307_port_close
   ================================================================ */
void kw307_port_close(void)
{
    if (s_fd >= 0) {
        close(s_fd);
        s_fd = -1;
    }
}

/* ================================================================
   3. kw307_port_send
   ================================================================ */
int kw307_port_send(const uint8_t* buf, int len)
{
    int sent = 0;

    if (s_fd < 0 || !buf || len <= 0) return -1;

    /* Loop: a write() to a tty may accept fewer bytes than asked. A partial
     * frame fails CRC at the module, so keep going until it is all out. */
    while (sent < len) {
        ssize_t n = write(s_fd, buf + sent, (size_t)(len - sent));
        if (n > 0) { sent += (int)n; continue; }

        if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
            /* Output buffer full — wait for room rather than spinning. */
            struct pollfd p; p.fd = s_fd; p.events = POLLOUT;
            if (poll(&p, 1, 100) <= 0) break;
            continue;
        }
        if (n < 0 && fatal_errno(errno)) kw307_port_close();
        return (sent > 0) ? sent : -1;
    }
    return sent;
}

/* ================================================================
   4. kw307_port_recv
      timeout_ms = 0 : return whatever is available now
      timeout_ms > 0 : wait up to timeout_ms for at least 1 byte
   ================================================================ */
int kw307_port_recv(uint8_t* buf, int max_len, int timeout_ms)
{
    ssize_t n;

    if (s_fd < 0 || !buf || max_len <= 0) return -1;

    if (timeout_ms > 0) {
        struct pollfd p; p.fd = s_fd; p.events = POLLIN;
        int pr = poll(&p, 1, timeout_ms);
        if (pr == 0) return 0;                       /* timeout, not an error */
        if (pr < 0)  return (errno == EINTR) ? 0 : -1;
        /* POLLERR/POLLHUP/POLLNVAL: the device went away under us. */
        if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) { kw307_port_close(); return -1; }
    }

    n = read(s_fd, buf, (size_t)max_len);
    if (n > 0)  return (int)n;
    if (n == 0) return 0;                            /* nothing pending */
    if (errno == EAGAIN || errno == EINTR) return 0;
    if (fatal_errno(errno)) kw307_port_close();
    return -1;
}

/* ================================================================
   5. kw307_port_is_open
   ================================================================ */
int kw307_port_is_open(void)
{
    return (s_fd >= 0) ? 1 : 0;
}
