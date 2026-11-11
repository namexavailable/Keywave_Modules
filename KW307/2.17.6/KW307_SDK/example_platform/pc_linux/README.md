# `pc_linux/` — Linux port

Reference port for Linux hosts: PC / IPC / embedded x86, Raspberry Pi, Jetson,
BeagleBone. Talks to the KW307 over any serial device — a USB-serial bridge
(`/dev/ttyUSB0`), a USB CDC device (`/dev/ttyACM0`), or an SoC's own UART pins
(`/dev/serial0`). Standard POSIX termios, no driver and no library beyond libc.

Builds with **GCC** or **Clang**, C99.

## Files in this folder

| File | What it is | Edit? |
|---|---|---|
| `port_linux.c` | The 5 port functions on POSIX `open` / `read` / `write` / `termios`, plus the two host helpers | No — works as-is |
| `user_config.h` | Device path + baud defaults | **Yes — set your device** |
| `kw307_example_linux.h` | Declares `kw307_example_set_port()` / `kw307_example_list_ports()` | No |
| `main_demo.c` | Minimal console demo (prints presence transitions) | Use as the starting point for your app |
| `README.md` | This file | — |

## Quick start

### 1. Find your device

```bash
ls -l /dev/serial* /dev/ttyUSB* /dev/ttyACM*
dmesg -w                # then plug the adapter in, and watch
```

Set it in `user_config.h`, or pass it on the command line (the demo forwards
`argv[1]` to `kw307_example_set_port()`):

```c
#define KW307_DEFAULT_PORT_NAME   "/dev/ttyUSB0"
#define KW307_DEFAULT_BAUD_RATE   115200
```

### 2. Permission — join `dialout`

```bash
sudo usermod -aG dialout $USER      # then log out and back in
groups                              # should list: dialout
```

Without this, `open()` fails with `EACCES` even though the device exists. Do not
work around it with `sudo`.

### 3. Build

```bash
cc -std=c99 -O2 -Wall -I core -I example_platform/pc_linux \
   core/KW307_io.c core/KW307_api.c core/KW307_cmd.c \
   core/KW307_api_advanced.c core/KW307_api_tracker_optimize.c \
   core/KW307_output.c \
   core/KW307_protocol.c core/KW307_uart.c \
   example_platform/pc_linux/port_linux.c \
   example_platform/pc_linux/main_demo.c -o kw307_demo
```

Include paths: `core/` and `example_platform/pc_linux/`.

### 4. Run

```
$ ./kw307_demo /dev/serial0
KW307 SDK Linux demo
  device: /dev/serial0
  module firmware v2.11.0
  streaming — Ctrl-C to exit

[   0.4s] PRESENT  dist= 187 cm  angle=+12.4 deg  mag= 512   (fc=8)
[   3.1s] ABSENT                                           (fc=62)
```

`fc` is the frame's `frame_index`.

## Using an SoC's own UART pins

On a Raspberry Pi (and most SBCs) the header UART is used by a **serial login
console** out of the box, which competes with the SDK for the port. Free it first:

```bash
sudo raspi-config       # 3 Interface Options -> I6 Serial Port
                        #   login shell over serial?  No
                        #   serial hardware enabled?  Yes
```

On Pi 3/4/5 also assign the full PL011 UART to the header — by default it is used
by Bluetooth, leaving the pins on a mini-UART whose baud drifts with the CPU clock. In
`/boot/firmware/config.txt`:

```
enable_uart=1
dtoverlay=disable-bt
```

Then reboot. Use `/dev/serial0`: it is a symlink the OS keeps pointing at
whichever UART is primary, so the same name works on every model.

Wiring is four lines, and **TX goes to RX**: the Pi's TXD (header pin 8) to the
module's RXD, the Pi's RXD (pin 10) to the module's TXD, plus 5V (pin 4) and GND
(pin 6). Straight-through wiring opens the port perfectly and never delivers a
frame — the most common first-time fault.

Pi GPIO is 3.3 V logic while the module is powered from 5 V; confirm the module's
TXD idles at 3.3 V before connecting pin 10 on a new build variant.

## How the port works

The 5 functions in `core/KW307_port.h` are the only thing the SDK calls into the
platform; every other layer is portable C. This port maps them onto POSIX:

| `KW307_port.h` function | POSIX implementation |
|---|---|
| `kw307_port_open(name, baud)` | `open(O_RDWR\|O_NOCTTY\|O_NONBLOCK)` → `tcgetattr` → raw 8N1, no flow control → `cfsetispeed/ospeed` → `tcsetattr` → `tcflush` |
| `kw307_port_close()` | `close()` |
| `kw307_port_send(buf, len)` | `write()` in a loop until every byte is out; `poll(POLLOUT)` when the buffer is full |
| `kw307_port_recv(buf, max, ms)` | `poll(POLLIN, ms)` when `ms > 0`, then one `read()` of up to `max` bytes |
| `kw307_port_is_open()` | the cached file descriptor is >= 0 |

Two details worth knowing:

- **Raw mode is set by hand, not with `cfmakeraw()`.** That function is a BSD/GNU
  extension, so a strict `-std=c99` hides it (and `CRTSCTS`). The file requests
  `_DEFAULT_SOURCE` for `CRTSCTS` and clears the raw-mode bits explicitly, so it
  builds under any `-std=` on glibc or musl.
- **`IXOFF` and `IXANY` are cleared too**, which `cfmakeraw()` does not do. The
  KW307 protocol is binary and does contain `0x11`/`0x13`, so leaving software
  flow control armed would let the module's own payload pause our transmitter.

## Notes

- **One device per process.** The port keeps a single static file descriptor, so
  this port drives one KW307 at a time. Open / close to switch — not concurrently.
- **Single-threaded.** `kw307_update()` is polling-driven. If a GUI
  must stay responsive, run the pump in a worker thread and post results to the UI
  thread — do not call SDK functions from two threads.
- **Fault detection.** A permanent transport error (adapter unplugged, tty
  revoked) closes the descriptor, so `kw307_port_is_open()` reports the actual state. Poll
  it in your loop to notice a device disappearing. `EAGAIN` / `EINTR` are normal
  and do not close anything.
- **Baud rate** must match the module's firmware (typically 115200). Wrong baud →
  `kw307_check_fw_version()` reports a transport error.
- **Python?** The same C builds into a shared library that Python can drive with
  `ctypes`, which keeps one implementation of the protocol instead of a second one
  in Python. See the Raspberry Pi example project.

## See also

- `example_platform/pc_win32/` — the equivalent Windows COM port
- `example_platform/Nuvoton_M251/` — MCU bare-metal, ISR-driven RX
- `example_platform/STM32_HAL/` — STM32 via Cube/HAL UART
- `example_platform/Arduino/` — Arduino HardwareSerial
- `example_platform/port_template/` — empty skeleton for a new platform
- `doc/AGENTS.md` — rules for an AI coding assistant working against this SDK
