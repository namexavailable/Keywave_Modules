# KW307 SDK User Guide

24 GHz presence radar over UART — how it works, and the call that changes each part of it

**SDK 2.17.6** · module firmware **2.11.0** and newer · 2026-11-10

Start at **Quick start**: the smallest program that works. The chapters after it follow the signal from the antenna to the target list, one section per block of the diagram, each ending with the calls that change that block. The **UART protocol** chapter at the end gives the bytes, for a host that does not run the SDK.

## Contents

1. [Quick start](#quick-start)
    1.1 [Build integration](#build-integration)
    1.2 [UART port functions](#uart-port-functions)
    1.3 [Minimal example program](#minimal-example-program)
    1.4 [Frame contents](#frame-contents)
2. [Output modes](#output-modes)
    2.1 [Output mode setup](#output-mode-setup)
    2.2 [Switching modes while running](#switching-modes-while-running)
3. [Error handling](#error-handling)
4. [Signal path](#signal-path)
    4.1 [Antenna Configuration](#antenna-configuration)
    4.2 [Analog Front End](#analog-front-end)
        4.2.1 [PLL Calibration](#pll-calibration)
        4.2.2 [EQ](#eq)
        4.2.3 [BB](#bb)
    4.3 [Doppler Signal Processing](#doppler-signal-processing)
        4.3.1 [Background Calibration](#background-calibration)
        4.3.2 [Doppler Filter Corner](#doppler-filter-corner)
        4.3.3 [Digital Gain](#digital-gain)
        4.3.4 [IIR Filter](#iir-filter)
    4.4 [Parameters Translation](#parameters-translation)
    4.5 [Tracking Algorithm](#tracking-algorithm)
        4.5.1 [Detection Range & FOV](#detection-range--fov)
        4.5.2 [Magnitude Threshold](#magnitude-threshold)
        4.5.3 [Speed Threshold](#speed-threshold)
        4.5.4 [Moving Tracking](#moving-tracking)
        4.5.5 [Stationary Tracking](#stationary-tracking)
        4.5.6 [Line of Sight Engine](#line-of-sight-engine)
        4.5.7 [Sensitivity Level Preset](#sensitivity-level-preset)
    4.6 [Presence Flag (Optional)](#presence-flag-optional)
5. [Application examples](#application-examples)
6. [Function reference](#function-reference)
    6.1 [Session](#session)
    6.2 [Front end and power](#front-end-and-power)
    6.3 [Filters](#filters)
    6.4 [Detection and reporting](#detection-and-reporting)
    6.5 [Trackers](#trackers)
    6.6 [Custom frames](#custom-frames)
    6.7 [PLL calibration](#pll-calibration-1)
7. [UART protocol](#uart-protocol)
    7.1 [UART link settings](#uart-link-settings)
    7.2 [UART frame format](#uart-frame-format)
    7.3 [Write and read requests](#write-and-read-requests)
    7.4 [Request timing](#request-timing)
    7.5 [Output frame layouts](#output-frame-layouts)

## Quick start

Settings take effect only once the module is running and reporting. Getting there takes five functions that you write, three calls that the SDK provides, and a main loop.

![The top and bottom boxes are host code. Everything between them ships as C source.](guide/img/sdk_layers.svg)

### Build integration

Add every `.c` file under `core/` and every `.h` file beside them. There is no build system to adopt and nothing to configure at compile time. Add the files, add `core/` to your include path. The result is a C99 library with no dependency beyond the standard headers `stdint.h`, `stdbool.h`, `stddef.h` and `string.h`.

### UART port functions

The SDK never touches the hardware. It calls out to these; point them at the board's UART:

```
int  kw307_port_open (const char* port_name, int baud_rate);
void kw307_port_close(void);
int  kw307_port_send (const uint8_t* buf, int len);
int  kw307_port_recv (uint8_t* buf, int max_len, int timeout_ms);
int  kw307_port_is_open(void);
```

`example_platform/port_template/` is a working skeleton with those five left as `TODO`. If your host is close to `STM32_HAL/`, `Arduino/`, `RPi_Pico/`, `pc_win32/` or `pc_linux/`, start from that one instead. Those are finished ports, not a list of what is supported.

The port they open is an ordinary UART at **115200 baud, 8 data bits, no parity, one stop bit, no flow control**, at **3.3 V TTL**. The baud rate is set in `user_config.h` next to the port you are writing, and has to match the module's — 115200 is what it ships with.

> **Note.** `kw307_port_recv()` must not block past `timeout_ms`, and must return what it has rather than waiting for a full buffer. Everything the SDK does on top — every read-back, every blocking command — is built on that one promise.

Every read-back waits about 200 ms (`KW307_READ_TIMEOUT_MS`) for the module's answer and then returns `KW307_ERR_TIMEOUT`, so no call can hang.

### Minimal example program

```
#include "KW307_api.h"

static void on_radar(const KW307Frame_t* f)
{
    set_led(f->presence_flag);                  /* 1 while a target is detected */
}

int main(void)
{
    board_init();                            /* your clocks, pins, UART */

    if (kw307_init() != KW307_OK) return 1;  /* opens the port, starts the link */
    kw307_on_frame(on_radar);                /* your callback, one per frame */

    for (;;) kw307_update();                 /* reads the UART, runs callbacks */
}
```

`kw307_update()` does not block. Call it as often as your loop runs; the module sends a frame about twenty times a second and `on_radar` fires once per frame.

That program never configures the module, and it works: the module boots with the settings in its own flash and starts streaming. Configuration is what the rest of this guide is about, and every call in it is in scope from `KW307_api.h` alone.

> **Note.** Call `kw307_check_fw_version()` once after `kw307_init()`. It returns `0` when the firmware is compatible, `1` when it is older than this SDK needs, and a negative code if the module did not answer. On older firmware, the calls it does not have return `KW307_ERR_NOT_SUPPORTED`.

### Frame contents

```
typedef struct {
    uint16_t       frame_index;   /* frame counter */
    uint8_t        presence_flag;    /* 1 = presence this frame */
    KW307Tracker_t moving[2];     /* walking, gesturing */
    KW307Tracker_t stationary[1]; /* a stationary target, micro-motion */
} KW307Frame_t;
```

The pointer is valid for the length of the callback. Copy out what you need.

> **Use a slot only when its `state` says Valid.** Valid is `1`, in `moving[]` and `stationary[]` alike; an Empty slot is `0`. Test `state == 1`, not `state != 0`: on module firmware older than 2.7.6, a moving target is Valid at `3`, and `1` and `2` are not yet confirmed. Never decide on `mag`: it is the magnitude, not a presence flag. `core/KW307_protocol.h` lists the state values.

Each slot also carries `counter`, which rises while the target keeps being seen and falls when it is missed. It is useful as a confidence or fade level.

## Output modes

The quick start showed one output. There are three, and the difference between them is only **how much each frame carries**. Every one of them reports presence, two moving targets and one stationary target — that part never changes. What changes is what is added on top. `KW307_OUTPUT_OFF` sends nothing.

| `mode` | Contents | Length | Receive with | Struct |
|---|---|---|---|---|
| `KW307_OUTPUT_NORMAL` (default) | Presence, 2 moving and 1 stationary target. | 27 bytes | `kw307_on_frame()` | `KW307Frame_t` |
| `KW307_OUTPUT_NORMAL_WITH_SPD` | Plus the speed of each moving target. | 34 bytes | `kw307_on_frame_spd()` | `KW307FrameSpd_t` |
| `KW307_OUTPUT_NORMAL_WITH_SPD_RG` | Plus the stationary band's raw readings of range gates 0~3: distance, angle, magnitude and speed. | 66 bytes | `kw307_on_frame_spd_rg()` | `KW307FrameSpdRg_t` |

| `mode` | Contents |
|---|---|
| `KW307_OUTPUT_OFF` | Nothing; the module sends no frames. |

The three structs are in **`core/KW307_protocol.h`**, field by field: what each one means, what `state` is allowed to be and which value means a real target, what the speed units are, and what the range gates are and are not.

### Output mode setup

Whichever mode you pick, the two steps are the same — register the callback, then select the mode:

```
kw307_on_frame_spd(on_radar);                     /* 1. where frames go   */
kw307_set_output(KW307_OUTPUT_NORMAL_WITH_SPD);   /* 2. what to send      */

while (1) { kw307_update(); }                     /* 3. read the UART     */
```

**Register before you select.** Frames start arriving as soon as the module is told to send them. If the callback is not in place yet, the first few frames are lost.

`kw307_update()` reads the UART and calls your callback. If it is not called regularly, no frames are delivered, so check this first when no frames arrive.

### Switching modes while running

You can change mode at any time — a calibration typically goes into `KW307_OUTPUT_NORMAL_WITH_SPD_RG`, takes its reading, and returns to `KW307_OUTPUT_NORMAL_WITH_SPD`:

```
kw307_on_frame_spd(on_normal);        /* register both, once, at start-up */
kw307_on_frame_spd_rg(on_gates);

...

kw307_set_output(KW307_OUTPUT_NORMAL_WITH_SPD_RG);   /* measure   */
...
kw307_set_output(KW307_OUTPUT_NORMAL_WITH_SPD);      /* and back  */
```

**Register every callback you might use, once, at start-up.** There is nothing to unregister when you switch: only the selected mode's frames are sent, so only its callback fires. Frames still in flight from the previous mode are matched to the right callback on the way in, not mis-read.

## Error handling

Every setter and getter returns `int`: `KW307_OK` (zero) on success, or a negative error code on failure. A single test detects every failure:

```
if (kw307_set_detect_range(30, 400) != KW307_OK) { /* handle it */ }
```

A setter returns `KW307_OK` once the command is sent; it does not wait for the module's reply. To confirm a value, read it back with its getter.

Test for a specific error code only where the application must respond differently. The error codes are defined in `KW307_io.h`.

| Error code | Value | Description | Recommended action |
|---|---|---|---|
| `KW307_OK` | 0 | The call succeeded. | None. |
| `KW307_ERR_PORT` | -1 | The UART failed, or the module's reply did not match the request. A setter reports every UART failure with this code. | Check the UART connection. The call may be retried. |
| `KW307_ERR_PARAM` | -2 | An argument is outside the range the call accepts. No command was sent to the module. | Correct the argument to within the range documented for the call. |
| `KW307_ERR_NOT_SUPPORTED` | -4 | The module firmware does not support this call. Reported once `kw307_check_fw_version()` has run. | This is not an error condition. Do not retry the call, and do not use this feature with this module. |
| `KW307_ERR_TIMEOUT` | -5 | The module did not reply within the timeout. Returned by getters and blocking calls. | The call may be retried. |
| `KW307_ERR_PORT_OPEN` | -10 | `kw307_init()` could not open the UART. | Check the port name and baud rate in `user_config.h`. |
| `KW307_ERR_PORT_CLOSED` | -11 | The UART is not open: `kw307_init()` has not been called, or the port was lost. `kw307_is_open()` reports this without sending a command. | Stop sending commands and call `kw307_init()` to reopen the UART. |
| `KW307_ERR_PORT_SEND` | -12 | The UART did not accept the complete command. | Check the `kw307_port_send()` implementation in the port layer. |
| `KW307_ERR_NACK` | -20 | The module refused the request. Returned by getters and blocking calls. | Correct the request before sending it again. |
| `KW307_ERR_RESP_LEN` | -21 | The module's reply was shorter than expected. | Handle as a communication error. The call may be retried. |

> **A timeout does not always indicate a module fault.** A blocking call waits about 200 ms (`KW307_READ_TIMEOUT_MS`) for a reply, which arrives only if the application is reading the port. A stalled main loop, a disabled interrupt or a receive buffer that is never emptied causes every read to time out, even though the module is responding normally.

## Signal path

In the KW307 radar system, targets are categorised into two types:

- **Moving targets** — macro-movements such as walking or running.
- **Stationary targets with micro-Doppler signature** — micro-movements such as breathing, heartbeat, or subtle body shifts.

The radar hardware processes incoming signals through two parallel bands: the **moving band** and the **stationary band**. Both bands share a similar architecture and the same configurable parameters.

![The moving band takes 20 to 160 Hz, the stationary band 0.31 to 10 Hz.](guide/img/signal_path.svg)

Every value the module reports comes out of those two bands, and every setting in this guide changes one block in one of them. Left to right:

| Block | Description | Configurable |
|---|---|---|
| **Analog front end** | Handles RF transmit and receive, range equalization and the baseband gain. | Power mode, PLL calibration, the equalizer and the baseband gain. |
| **Complex data per range gate** | Outputs the raw IQ data of each range gate. | Which range gates are in play, through the detect range and FOV. |
| **Smooth average** | Averages samples to reduce the noise before filtering. | Nothing. It is fixed. |
| **Doppler high-pass** | Removes stationary clutter, such as floors, walls and the TX-to-RX crosstalk, with switch-controlled filter modes. | The corner frequency, and the filter switches. |
| **Doppler low-pass** | Attenuates the high-Doppler components that are outside the band. | The corner frequency. |
| **Digital gain** | Applies a gain after the clutter is removed, to lift weak targets. | The gain step, 1x to 8x. |
| **IIR filter** | Filters the complex data of each range gate over frames. | The filter coefficient, 0~1000. |
| **Parameters translation** | Converts each range gate's signal into magnitude, distance, angle and speed. | Nothing. It is fixed. |
| **Magnitude threshold** | Selects candidate targets by magnitude. | The magnitude threshold, per band. |
| **Tracking** | Associates targets across frames and confirms them. | The thresholds, the tracker counters and smoothing. |

The detect range and FOV, and the presence flag, apply to both bands:

- **Detect range and FOV:** sets which range gates and angles are in play.
- **Presence flag:** the final binary presence state.

<!-- -->
- [4.1 Antenna Configuration](#antenna-configuration)
- [4.2 Analog Front End](#analog-front-end)
    - [4.2.1 PLL Calibration](#pll-calibration)
    - [4.2.2 EQ](#eq)
    - [4.2.3 BB](#bb)
- [4.3 Doppler Signal Processing](#doppler-signal-processing)
    - [4.3.1 Background Calibration](#background-calibration)
    - [4.3.2 Doppler Filter Corner](#doppler-filter-corner)
    - [4.3.3 Digital Gain](#digital-gain)
    - [4.3.4 IIR Filter](#iir-filter)
- [4.4 Parameters Translation](#parameters-translation)
- [4.5 Tracking Algorithm](#tracking-algorithm)
    - [4.5.1 Detection Range & FOV](#detection-range--fov)
    - [4.5.2 Magnitude Threshold](#magnitude-threshold)
    - [4.5.3 Speed Threshold](#speed-threshold)
    - [4.5.4 Moving Tracking](#moving-tracking)
    - [4.5.5 Stationary Tracking](#stationary-tracking)
    - [4.5.6 Line of Sight Engine](#line-of-sight-engine)
    - [4.5.7 Sensitivity Level Preset](#sensitivity-level-preset)
- [4.6 Presence Flag (Optional)](#presence-flag-optional)

### Antenna Configuration

Configures active transmitter (TX) and receiver (RX) hardware modes, magnitude algorithms, and angle estimation processing based on module topology.

![The antenna configurations, and the arguments for each.](guide/img/antenna_configs.svg)

```
kw307_set_antenna(tx_mode, rx_mode, with_angle);   /* default 0 / 1 / true */
```

Sets the RF channel topology and angle processing flag based on the selected hardware configuration in the mapping table.

- `tx_mode` — transmitter mode setting, configured according to the module topology.
- `rx_mode` — receiver mode setting, configured according to the module topology.
- `with_angle` — angle estimation toggle (`true`: enabled, `false`: disabled).

```
kw307_set_mag_type(mag_type);   /* default 1 */
```

Selects the signal magnitude algorithm based on angle estimation mode.

- `mag_type` — magnitude algorithm type. Set to 0 when `with_angle` = false, and set to 1 when `with_angle` = true.

##### Implementation note on 1T1R (configuration 2)

Unlike configuration 1, RX1 and RX3 remain powered in configuration 2. It does not support angle estimation, but improves SNR at the cost of higher power consumption.

### Analog Front End

```
kw307_set_power_mode(mode);   /* default KW307_POWER_NORMAL */
```

The power mode, by `mode`:

| `mode` | Description |
|---|---|
| `KW307_POWER_DOWN` | Lowest current; sensing and frame output stop. Return to `KW307_POWER_NORMAL` to resume. |
| `KW307_POWER_NORMAL` (default) | Sensing. |
| `KW307_POWER_PA_OFF` | Transmit off: the module receives and answers, and detects nothing. Return to `KW307_POWER_NORMAL` to resume. |

#### PLL Calibration

By default, the module automatically performs self-calibration upon every power-on sequence. This process takes approximately 17 ms before the first valid frame is generated. No explicit API call is required to initiate this calibration.

The calibration process measures the radar's transmitted frequency sweep across three distinct phases:

| Phase | Description |
|---|---|
| **F0** | Defines the starting frequency of the sweep. |
| **AVG** | Determines the sweep slope, indicating the rate of frequency increase from the starting point. |
| **ERR** | Evaluates sweep linearity, applying corrections to ensure the frequency climb is a straight line rather than a curve. |

Alternatively, all three parameters can be measured once and loaded from the flash memory during subsequent power-on sequences. See **One-time calibration** below.

##### One-time calibration

```
kw307_en_pll_cal_on_boot(false);   /* load the saved calibration at power-on */
kw307_save_settings();             /* save the currently running calibration */
```

- **Default state (`true`):** all three calibration phases are executed at every power-on, and no calibration data is saved.
- **Disabled state (`false`):** the module skips the calibration phases at power-on. Instead, it loads the calibration data previously saved via `kw307_save_settings()`. This eliminates the 17 ms calibration delay, which is highly beneficial for products that frequently power-cycle the module.

**Note:** the calibration data saved is the data currently being used by the module at the time `kw307_save_settings()` is called, and it is only saved if all three phases have successfully converged. If convergence fails, the module will perform a full calibration on the next power-on and save the new result automatically.

##### Periodic re-calibration

Periodic re-calibration is disabled by default. It is highly recommended to enable this feature if the module is deployed in environments subject to significant temperature fluctuations.

![Each phase takes one frame, and the calibration repeats every `interval` frames.](guide/img/pll_online_cal.svg)

Each calibration phase requires one complete frame, and the calibration sequence repeats continuously based on a user-defined frame interval.

```
kw307_en_auto_pll_cal(true);             /* enable periodic PLL calibration */
kw307_set_auto_pll_cal_interval(6000);   /* calibration interval, 6000 frames (5 minutes at 20 Hz) */
```

Operational details:

- **Data validity:** each calibration phase (F0, AVG, and optionally ERR) consumes one frame. During a calibration frame, the transmitter is disabled for 2 to 9 ms; therefore, these frames do not carry valid measurement data.
- **Interval configuration:** the interval takes 1~32767 frames.
- **ERR phase exclusion:** by default, the ERR (straightness) phase is excluded from periodic re-calibration. This is controlled by `kw307_en_auto_pll_err_cal()`. The default exclusion is by design: sweep linearity remains relatively stable across temperature variations, and the ERR phase requires significantly more processing time than the F0 and AVG phases.

#### EQ

```
kw307_set_equalizer_gain(index);                           /* default 1 (+6 dB)          */
kw307_set_equalizer_order(order);                          /* default 2                  */
kw307_set_equalizer_cmd(response, gain_set1, gain_set2);   /* default 0x0B / 0xAA / 0x00 */
```

- `kw307_set_equalizer_gain(index)` — the equalizer gain, in the analog receive chain. `index` 0~3: 0 / +6 / +9 / +12 dB. Default 1 (+6 dB).
- `kw307_set_equalizer_order(order)` — the equalizer order. `order` 1 or 2. Default 2.
- `kw307_set_equalizer_cmd(response, gain_set1, gain_set2)` — the equalizer as its three bytes. `response` 0x00~0x0F, default 0x0B; `gain_set1` default 0xAA; `gain_set2` default 0x00.

#### BB

```
kw307_set_gain(index);           /* default 5 (+12 dB) */
kw307_set_baseband_att(index);   /* default 0 (0 dB)   */
```

- `kw307_set_gain(index)` — the baseband gain, ahead of both bands. `index` 1~8: 0 / +3 / +6 / +9 / +12 / +15 / +18 / +21 dB. Default 5 (+12 dB).
- `kw307_set_baseband_att(index)` — the baseband attenuator, both bands. `index` 0~2: 0 / -6 / -12 dB. Default 0.

### Doppler Signal Processing

#### Background Calibration

Static clutter — floors, walls, furniture and the TX-to-RX crosstalk — appears at DC (0 Hz) and must be rejected to isolate target motion.

- Micro-movements, such as respiration, typically begin around **0.3 Hz**.
- Macro-movements, such as walking, typically begin around **20 Hz**.

The static clutter rejection is controlled in two stages. The **first stage** acts before the signal is split into the two bands, so its switches act on both bands. The **second stage** has a switch on each band. Each stage has a background update switch and a subtraction switch.

##### Background update control

The background update switches control background adaptation:

- **Update** (the default) — operates as a standard Doppler high-pass, continuously updating the static clutter estimate.
- **Freeze** — freezes background adaptation. This effectively extends the high-pass corner frequency down to DC (0 Hz), so a newly added stationary object is detected against the stored background.

```
kw307_en_background_update(on);                 /* first stage, both bands; default true */
kw307_set_moving_background(update);            /* second stage, moving band; true = keep adapting; default true */
kw307_set_stationary_background(update);        /* second stage, stationary band; default true */
```

##### Environment subtraction switch

The high-pass filtering subtracts the low-pass background estimate from the raw signal. The environment subtraction switch controls that subtraction:

- **On** (the default) — the subtraction is enabled, and the output is the high-passed signal with the static clutter removed.
- **Off** — the subtraction is bypassed, and the output is the raw signal with the background clutter still in it, for measuring the environment baseline.

```
kw307_en_background_subtraction(on);            /* first stage, both bands; default true */
kw307_en_moving_env_subtraction(on);            /* second stage, moving band; default true */
kw307_en_stationary_env_subtraction(on);        /* second stage, stationary band; default true */
```

##### Stationary tracker result hold

```
kw307_en_stationary_hold(on);     /* true = hold the stationary result; default false */
```

This holds the stationary tracker result, so a change of filter state does not reach what you read. Every stationary slot keeps the distance, angle, magnitude and state it had when the hold went on. The moving band is not held.

Use the hold while you change the environment subtraction switch or either first-stage switch. Changing them makes the signal jump suddenly, and it needs seconds to settle. The second-stage background switches do not need the hold — they change what the filter learns, not where the signal goes.

The recommended order:

- Turn the hold on.
- Change the environment subtraction switch or a first-stage switch.
- Wait 2~3 s for the signal to settle.
- Turn the hold off.

#### Doppler Filter Corner

The two Doppler filters:

- **Doppler high-pass filter:** lowering the corner frequency enables the detection of micro-movements, such as respiration, but increases the susceptibility to environmental vibration.
- **Doppler low-pass filter:** lowering the corner frequency filters out fast-moving targets, keeping slower targets and reducing the environmental noise.

These calls set the Doppler high-pass and low-pass corner frequencies directly when the predefined sensitivity levels (**Sensitivity Level Preset**) do not meet the application's requirements.

```
kw307_set_moving_hpf_corner(corner, nd);       /* moving     high-pass, corner 0~7, nd 0~3; default 3 / 0 */
kw307_set_moving_lpf_corner(corner);           /* moving     low-pass, 0~255; default 128              */
kw307_set_stationary_hpf_corner(corner, nd);   /* stationary high-pass; default 7 / 2                     */
kw307_set_stationary_lpf_corner(corner);       /* stationary low-pass; default 8                          */
```

```
kw307_set_sample_rate(sel);   /* default 1 (2 kHz) */
```

- `kw307_set_sample_rate(sel)` — the receiver sampling rate. `sel` 0~3: 4 / 2 / 1.6 / 1.3 kHz. Default 1 (2 kHz).

At the default 2 kHz sampling rate:

```
high-pass   2000 / 2^corner / 2^(nd + 1) / 2π   Hz
low-pass    2000 * corner / 256 / 2π            Hz
```

The high-pass corner frequency halves with each increment of `corner`, whereas the low-pass corner frequency scales linearly.

High-pass corner frequency tuning:

- **Corner frequency:** set by `corner`, 0~7, and `nd`, 0~3.
- **Downsample factor:** `nd` is the downsampling factor, 0~3.
- **Tuning sequence:** adjust `corner` first, 0~7. Increase `nd` only when `corner` reaches 7 and further downsampling is required.

| corner | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 7 | 7 | 7 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| nd | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 2 | 3 |
| high-pass, Hz | 160 | 80 | 40 | 20 | 10 | 5 | 2.5 | 1.2 | 0.62 | 0.31 | 0.16 |

> **Direct corner configuration overrides the predefined levels.** `kw307_get_motion_sensitivity()` and `kw307_get_stationary_sensitivity()` can then return `KW307_LEVEL_NONE` when the values match no level. This is expected behaviour, not an error.

##### Default filter configuration and passband overview

An overview of the default filter passbands of the moving and stationary bands, with their signal characteristics:

| Band | High-pass | Low-pass | Applications |
|---|---|---|---|
| Moving | 20 Hz (corner 3, nd 0) | 160 Hz (corner 128) | Walking, hand gestures, approach motion. |
| Stationary | 0.31 Hz (corner 7, nd 2) | 10 Hz (corner 8) | Breathing detection, subtle body shifts. |

#### Digital Gain

```
kw307_set_digital_gain(index);   /* default 0 (1x) */
```

- `kw307_set_digital_gain(index)` — the digital gain, both bands, after the filters. `index` 0~3: 1x / 2x / 4x / 8x. Default 0.

#### IIR Filter

Ahead of **Parameters Translation**, the range-gate IIR, one per band, averages each gate's complex data over frames:

```
kw307_set_RangeGate_IIR(moving, stationary);     /* 0~1000; default 150 / 150 (0.15) */
```

The value is the weight given to each new measurement, in thousandths:

```
output = k/1000 × new measurement + (1 − k/1000) × previous output
```

where `k` is the value set for the band, 0~1000.

So the default 150 counts a new reading as 0.15 and what it already had as 0.85. Higher follows the measurement more closely. Lower suppresses more noise and holds the previous value harder. 1000 is no averaging at all.

### Parameters Translation

This block turns each range gate's complex (I/Q) data into a magnitude, a distance, an angle and a speed.

The range-gate IIR ahead of this block (**IIR Filter**) uses one coefficient for all four quantities: magnitude, distance, angle and speed. The tracker has its own smoothing, in **Tracking Algorithm** below.

### Tracking Algorithm

The tracker suppresses the impulse noise in the range-gate data and keeps each target's trajectory stable.

#### Detection Range & FOV

Only targets inside the detect range and the field of view are tracked and reported.

```
kw307_set_detect_range(min_cm, max_cm);          /* the room, in cm; default 50 / 1000 */
kw307_set_detect_fov(fov_deg, shrink_rate_deg);  /* the field of view, and how it narrows with distance; default 120 / 8 */
kw307_set_fov_min(fov_deg);                      /* the narrowest the FOV can become; default 20 */
```

- `min_cm` — the near limit of the detect range, in cm.
- `max_cm` — the far limit of the detect range, in cm.
- `fov_deg` (`kw307_set_detect_fov()`) — the FOV at the module, in degrees.
- `shrink_rate_deg` — how many degrees the FOV narrows per metre of range.
- `fov_deg` (`kw307_set_fov_min()`) — the lower limit of that narrowing, so the FOV does not become too narrow at long range. Default 20°.

The FOV narrows linearly with range, so the far end of the zone fits the room. With the defaults, 120° and a shrink of 8° per metre:

- At the module: 120°.
- At 5 m: 80°.
- At 10 m: 40°.

![The detection cone with the default FOV and shrink.](guide/img/detection_cone.svg)

> **Note.** A KW307 1T1R module does not support angle estimation.

#### Magnitude Threshold

Each range gate has to exceed the magnitude threshold of its band to become a candidate target:

```
kw307_set_mag_threshold(moving, stationary);   /* default 350 / 100 */
```

- **Tuning:** the default suits typical indoor environments. Raise the threshold in noisier conditions, after evaluating the noise floor with the report below.

##### Noise Floor Report

Reports the peak noise magnitude of the empty room in the target slots, to help set the threshold:

```
kw307_en_noise_floor_report(on);     /* default false */
```

With it on, an empty target slot reports the room's peak magnitude in its `mag`.

- **Activation:** it reports only while no target is detected (`presence_flag` = 0).
- **Slots:** only the first moving slot and the nearest stationary slot carry it; the second slots stay 0.
- **Value:** it is the room's peak magnitude, not its average.

> **Note.** Use `state` to tell whether a slot holds a target, never `mag`. With this on an empty slot carries a non-zero magnitude, and code that treats `mag != 0` as a target will report a target in an empty room.

#### Speed Threshold

The speed accumulation and clipping that decide whether a moving target is reported:

```
kw307_set_motion_threshold(confirm_frames, min_speed, speed_clip, min_speed_sum);   /* default 8 / 250 / 500 / 2000 */
```

- `confirm_frames` — the number of frames a new moving target's speed is added up over before it is reported, 1~32. Default 8.
- `min_speed` — the slowest movement that counts as a moving target. Default 250.
- `speed_clip` — each frame's speed is capped at this before it is added up, so one spike cannot create a target. 0 = no cap. Default 500.
- `min_speed_sum` — the total the added-up speed must exceed over `confirm_frames` for the target to be reported. 0 = no check. Default 2000.

Keep the four within these rules, or a moving target may never be reported:

```
speed_clip ≥ min_speed
```

```
min_speed_sum ≤ confirm_frames × speed_clip
```

#### Moving Tracking

##### Tracker counters

Frame counters in each target slot control when a track is created and how long it persists:

```
kw307_set_moving_tracker_cnt(cnt_limit, display_th);   /* default 32 / 8 */
```

- `cnt_limit` — the counter's upper limit, in frames: how long a target is kept when it is not detected. Default 32.
- `display_th` — the count at which a detection is reported as a target. Default 8.
- `display_th` must not exceed `cnt_limit`.

##### Trajectory smoothing

The reported target trajectory is smoothed by an exponential moving average. `smoothing` is its weight in thousandths, the same form as the range-gate IIR (**IIR Filter**), applied to the reported track instead of to the gate:

```
kw307_set_moving_smoothing(smoothing);   /* default 300 (0.3) */
```

##### Advanced clutter rejection

Rejects repetitive motion, such as air conditioners and machinery, so it is not reported as presence:

```
kw307_set_advanced_clutter_rejection(en, spd_th, dev_th);   /* default false / 300 / 20 */
```

- `en` — true: on; false: every moving target is reported. Default false.
- `spd_th` — the motion threshold, 0~1000; higher rejects more. Default 300.
- `dev_th` — the allowed drift, in cm; higher rejects more. Recommended 0~50. Default 20.

#### Stationary Tracking

##### Tracker counters

Frame counters in each target slot control when a track is created and how long it persists:

```
kw307_set_stationary_tracker_cnt(cnt_limit, display_th);   /* default 64 / 8 */
```

- `cnt_limit` — the counter's upper limit, in frames: how long a target is kept when it is not detected. Default 64.
- `display_th` — the count at which a detection is reported as a target. Default 8.
- `display_th` must not exceed `cnt_limit`.

##### Trajectory smoothing

The reported target trajectory is smoothed by an exponential moving average. `smoothing` is its weight in thousandths, the same form as the range-gate IIR (**IIR Filter**), applied to the reported track instead of to the gate:

```
kw307_set_stationary_tracker_opt(confirm_frames, smoothing, match_distance_cm);   /* default 3 / 60 (0.06) / 100 */
```

- `confirm_frames` — consecutive frames a stationary target must be seen in before it is reported, 1~255. Default 3.
- `match_distance_cm` — how far a new detection may be from an existing stationary track and still count as the same target. Default 100 cm.

#### Line of Sight Engine

Line-of-sight (LOS) filtering optimises the spatial target candidates, to improve the detection accuracy along overlapping vectors.

```
kw307_en_los(false);   /* report everything, hidden or not */
    /* ... read every candidate ... */
kw307_en_los(true);    /* back to normal; the default */
```

Default: on (`true`), which filters overlapping candidate targets.

##### Application considerations

- **Off (`false`):** every candidate target is reported, for when full visibility of the targets is required, such as system evaluation or a custom tracking algorithm.
- **On (`true`):** recommended for standard operation, for a stable output.

##### Module compatibility

> **Turn LOS off on a KW307 1T1R module.** A KW307 1T1R module does not support angle estimation.

#### Sensitivity Level Preset

A sensitivity level sets the Doppler corner frequencies and motion thresholds of each band:

```
kw307_set_motion_sensitivity(level);       /* 0~9, or -1 for any motion; default 2 */
kw307_set_stationary_sensitivity(level);   /* 0~9, stationary band; default 1      */
```

| Level | High-pass | Min speed | Confirm frames | Speed clip | Min speed sum |
|---|---|---|---|---|---|
| **-1** | 0.16 Hz *(corner 7, nd 3)* | 1 | 4 | 2 | 0 *(off)* |
| **0** | 10 Hz *(corner 4)* | 250 | 4 | 500 | 1000 |
| **1** | 20 Hz *(corner 3)* | 250 | 4 | 500 | 1000 |
| **2** | 20 Hz *(corner 3)* | 250 | 8 | 500 | 2000 |
| **3** | 20 Hz *(corner 3)* | 500 | 4 | 1000 | 2000 |
| **4** | 40 Hz *(corner 2)* | 500 | 4 | 1000 | 2000 |
| **5** | 40 Hz *(corner 2)* | 500 | 8 | 1000 | 4000 |
| **6** | 40 Hz *(corner 2)* | 1000 | 4 | 2000 | 4000 |
| **7** | 80 Hz *(corner 1)* | 500 | 8 | 1000 | 4000 |
| **8** | 80 Hz *(corner 1)* | 1000 | 4 | 2000 | 4000 |
| **9** | 80 Hz *(corner 1)* | 1000 | 8 | 2000 | 8000 |

Moving band notes:

- **Parameter profile:** each level defines a preset parameter profile, including the high-pass corner frequency and the motion thresholds.
- **Low-pass filter:** a level does not set the low-pass corner frequency; it stays at 160 Hz by default.
- **Filter and threshold scaling:** a higher high-pass corner frequency keeps only higher target speeds, so the min speed and the min speed sum tend to rise with it to suppress the background noise.
- **Level range:** from -1 (the highest sensitivity) to 9 (the lowest). `-1` has the lowest high-pass corner frequency and the lowest min speed, with the min speed sum disabled.

| Level | High-pass | Low-pass | Baseband attenuator |
|---|---|---|---|
| **0** | 0.16 Hz *(corner 7, nd 3)* | 10 Hz *(corner 8)* | 0 dB |
| **1** | 0.31 Hz *(corner 7, nd 2)* | 10 Hz *(corner 8)* | 0 dB |
| **2** | 0.62 Hz *(corner 7, nd 1)* | 10 Hz *(corner 8)* | 0 dB |
| **3** | 1.2 Hz *(corner 7, nd 0)* | 10 Hz *(corner 8)* | 0 dB |
| **4** | 2.5 Hz *(corner 6, nd 0)* | 10 Hz *(corner 8)* | 0 dB |
| **5** | 5 Hz *(corner 5, nd 0)* | 10 Hz *(corner 8)* | 0 dB |
| **6** | 10 Hz *(corner 4, nd 0)* | 20 Hz *(corner 16)* | 0 dB |
| **7** | 10 Hz *(corner 4, nd 0)* | 20 Hz *(corner 16)* | -6 dB |
| **8** | 20 Hz *(corner 3, nd 0)* | 40 Hz *(corner 32)* | -6 dB |
| **9** | 40 Hz *(corner 2, nd 0)* | 80 Hz *(corner 64)* | -6 dB |

Stationary band notes:

- **Filter corners and attenuation:** each level sets both the high-pass and the low-pass corner frequencies, and the baseband attenuator: 0 dB for levels 0~6, -6 dB for levels 7~9. The -6 dB attenuation affects both bands.
- **Level 6 and level 7:** the two share the same filter corner frequencies and differ only in the 6 dB attenuation.

> **Sensitivity levels set these too.** A motion sensitivity level sets the values of `kw307_set_motion_threshold()`. If they are changed manually to a combination that is not in the level table, `kw307_get_motion_sensitivity()` returns `KW307_LEVEL_NONE`.

### Presence Flag (Optional)

The presence flag is a simple on/off indication. Any target that shows macro- or micro-Doppler signatures inside the detect range and FOV sets it. It sets as soon as a target is confirmed, and a hold time keeps it from flickering: after the last detection, the flag stays set for the hold time, in frames.

```
kw307_set_presence_hold_time(frames);                     /* default 220 = 11 s       */
kw307_set_presence_hold_zone(on, distance_cm, fov_deg);   /* default true / 150 / 40 */
kw307_en_gpio_presence_flag(on);                          /* default true            */
```

Normal operation is 20 Hz, so 1 s = 20 frames; `kw307_set_stationary_release_time()` counts in frames the same way.

- `kw307_set_presence_hold_time(frames)` — the hold time after the last detection. `frames` 1~32767 (0 acts as 1); 32767 is about 1638 s (27 min). Default 220 (11 s).
- `kw307_set_presence_hold_zone(on, distance_cm, fov_deg)` — holds the flag set while a stationary target stays in a near forward zone. `distance_cm` 0~3276, the zone depth; `fov_deg` 0~180, the zone width (60 = ±30°). Default true, 150, 40. It only adds presence, and also reports the target in an empty stationary slot.
- `kw307_en_gpio_presence_flag(on)` — the presence output pin. `on` true: the pin follows the presence flag; false: the pin is held low. Default true. Not kept by `kw307_save_settings()`.

##### Implementation note

- **Presence:** `presence_flag` gives the status, 1 or 0, inside the detect range and FOV only.
- **Target data:** the target slots or the range-gate data give each target's distance, angle, speed and magnitude, at 20 Hz.

## Application examples

The SDK ships five example configurations, one per `DEMO_SCENE_*` in the main program of every port under `example_platform/`. No single call selects a scene: each is a block of the calls this guide describes, tuned for one installation.

| Scene | Description |
|---|---|
| `DEMO_SCENE_OUTDOOR` | A yard or doorway: long range, frequent movement. |
| `DEMO_SCENE_OFFICE` | An office: targets that stay still for long periods. |
| `DEMO_SCENE_DESK` | A desk: one target at close range. |
| `DEMO_SCENE_URINAL` | A narrow bay: one target, frequent turnover. |
| `DEMO_SCENE_HAND_DRYER` | A hand under the outlet, in a narrow zone. |

Each scene calls `kw307_set_output(KW307_OUTPUT_NORMAL)`, then the calls below with these values:

| Call | Outdoor | Office | Desk | Urinal | Hand dryer |
|---|---|---|---|---|---|
| `kw307_set_gain(index)` | 5 | 5 | 3 | 3 | 5 |
| `kw307_set_detect_range(min_cm, max_cm)` | 50, 1200 | 50, 1000 | 50, 200 | 10, 150 | 10, 100 |
| `kw307_set_detect_fov(fov_deg, shrink_rate_deg)` | 120, 8 | 120, 8 | 120, 8 | 60, 5 | 80, 5 |
| `kw307_set_motion_sensitivity(level)` * | 6 | 2 | 0 | 1 | -1 |
| `kw307_set_stationary_sensitivity(level)` * | 3 | 1 | 0 | 1 | 1 |
| `kw307_set_presence_hold_time(frames)` | 100 | 220 | 220 | 20 | 20 |
| `kw307_set_mag_threshold(moving, stationary)` | 350, 100 | 350, 100 | 350, 100 | 350, 100 | 200, 5000 |
| `kw307_set_moving_tracker_cnt(cnt_limit, display_th)` | 32, 8 | 32, 8 | 32, 8 | 32, 8 | 20, 2 |
| `kw307_set_stationary_tracker_cnt(cnt_limit, display_th)` | 64, 8 | 64, 8 | 64, 8 | 64, 8 | 20, 2 |
| `kw307_set_presence_hold_zone(on, distance_cm, fov_deg)` | ✗, 150, 40 | ✓, 150, 40 | ✓, 150, 40 | ✓, 150, 40 | ✗, 150, 40 |
| `kw307_set_stationary_release_time(enable, frames)` | ✗, 100 | ✗, 400 | ✗, 2400 | ✗, 1200 | ✗, 200 |
| `kw307_set_advanced_clutter_rejection(en, spd_th, dev_th)` | ✗, 300, 20 | ✗, 300, 20 | ✗, 300, 20 | ✗, 300, 20 | ✗, 300, 20 |

\* Preset: one call that sets several values through other calls. The function reference lists the calls each preset makes. ✓ and ✗ are on and off.

`DEMO_SCENE_HAND_DRYER` sets the stationary threshold to 5000, out of reach, so it reports the moving band only.

Every value in the `DEMO_SCENE_OFFICE` column is the module default, so a module starts in that configuration.

To configure a product, start from the scene closest to the installation: copy its block from any port's main program and adjust the values.

## Function reference

The calls are grouped by where each acts on the signal path. Every setter except the custom-frame calls has a matching `kw307_get_*` that reads the value back through pointers to the same parameters; the getters are not listed.

A preset is one call that sets several values through other calls. Each preset, and the calls it makes in order:

| Preset | Calls, in order |
|---|---|
| `kw307_set_motion_sensitivity(level)` | `kw307_set_moving_hpf_corner(corner, nd)` |
| | `kw307_set_motion_threshold(confirm_frames, min_speed, speed_clip, min_speed_sum)` |
| `kw307_set_stationary_sensitivity(level)` | `kw307_set_stationary_hpf_corner(corner, nd)` |
| | `kw307_set_stationary_lpf_corner(corner)` |
| | `kw307_set_baseband_att(index)` |
| `kw307_set_equalizer_order(order)` | `kw307_set_equalizer_cmd(response, gain_set1, gain_set2)` |
| `kw307_set_output_rg(start_rg, end_rg, en_moving_trk, en_stationary_trk)` | `kw307_out_clear(c)` |
| | `kw307_out_rg(c, start_rg, end_rg, true, true, false)` |
| | `kw307_out_moving_trk(c, en_moving_trk, true, false)` |
| | `kw307_out_stationary_trk(c, en_stationary_trk, true)` |
| | `kw307_set_output_config(c)` |

`kw307_set_output(mode)` is also a preset for its two 0xF3 modes. Each mode is a fixed selection of the blocks a custom 0xF3 frame can carry:

| Call | `NORMAL` | `NORMAL_WITH_SPD` | `NORMAL_WITH_SPD_RG` |
|---|---|---|---|
| `kw307_out_presence(c, on)` | ✓ | ✓ | ✓ |
| `kw307_out_moving_trk(c, on, full, speed)` | (✓, ✓, ✗) | (✓, ✓, ✓) | (✓, ✓, ✓) |
| `kw307_out_stationary(c, nearest, second)` | (✓, ✗) | (✓, ✗) | (✓, ✗) |
| `kw307_out_stationary_trk(c, on, full)` | ✗ | ✗ | ✗ |
| `kw307_out_room_max(c, on)` | ✗ | ✗ | ✗ |
| `kw307_out_rg(c, start_rg, end_rg, moving, stationary, mag_only)` | ✗ | ✗ | (0, 3, ✗, ✓, ✗) |
| Frame | `Normal`, 27 bytes | `0xF3`, 34 bytes | `0xF3`, 66 bytes |

Columns are the `KW307_OUTPUT_*` modes. In brackets: the arguments after `c`, ✓ for true and ✗ for false. ✓ or ✗ alone: the block is or is not sent. `NORMAL` is a separate `Normal` frame, not built from these calls; its column shows the equivalent content. The two 0xF3 modes are built by `kw307_out_normal_with_spd()` and `kw307_out_normal_with_spd_rg()` and sent by `kw307_set_output_config()`. `OFF` sends no frame.

- **Call the preset first, then fine-tune** with the direct calls. Calling a preset afterwards rewrites all of its values and replaces those set directly.
- **After a direct change, the preset reads back as none:** the two sensitivity getters report `KW307_LEVEL_NONE`, and `kw307_get_equalizer_order()` reports `KW307_VALUE_NONE`, unless the values still match one.

### Session

| Call | Description |
|---|---|
| `kw307_init()` / `kw307_close()` | Open and close the link. |
| `kw307_update()` | Read the UART and call the registered callback. Call it on every pass of your main loop. |
| `kw307_radar_init()` | Re-initialise the module. Settings return to the last save; unsaved changes are discarded. |
| `kw307_save_settings()` | Write the current settings to flash. Wait about 1 s before the next call. Call it only after settings have changed, not on every pass of a loop: a call that saves a change rewrites the flash. |
| `kw307_factory_reset()` | Return every setting, and the output mode, to the default. |
| `kw307_clear_state()` | Clear stale targets. Settings are unchanged. |
| `kw307_check_fw_version()` | Check the module firmware is compatible with this SDK: `0` compatible, `1` firmware older than this SDK needs, negative on a link error. |

### Front end and power

| Call | Saved | Description |
|---|---|---|
| `kw307_set_gain(index)` | ✓ | Baseband gain in the analog receive chain, 0 to +21 dB. |
| `kw307_set_equalizer_gain(index)` | ✓ | Equalizer gain in the analog receive chain, 0 to +12 dB. |
| `kw307_set_baseband_att(index)` | ✓ | Baseband attenuator, 0 to -12 dB. Every stationary level sets it too. |
| `kw307_set_equalizer_order(order)` | ✓ | Equalizer order. |
| `kw307_set_antenna(tx_mode, rx_mode, with_angle)` | ✓ | The RF channel topology, and whether the angle is estimated. |
| `kw307_set_mag_type(mag_type)` | ✓ | The magnitude algorithm: 0 without angle estimation, 1 with. |
| `kw307_set_power_mode(mode)` | ✗ | Power mode: DOWN, NORMAL or PA_OFF. Return to NORMAL to resume sensing. |

### Filters

| Call | Saved | Description |
|---|---|---|
| `kw307_set_sample_rate(sel)` | ✓ | Sampling rate. The filter equations assume the 2 kHz default. |
| `kw307_en_background_update(on)` | ✓ | First-stage background update, both bands. On by default. |
| `kw307_en_background_subtraction(on)` | ✓ | First-stage background subtraction, both bands. On by default; turn off both first-stage switches to see a target that never moves. |
| `kw307_set_digital_gain(index)` | ✓ | Digital gain. |
| `kw307_set_RangeGate_IIR(moving, stationary)` | ✓ | How much each gate's magnitude, distance, angle and speed are averaged over frames. |
| `kw307_set_motion_sensitivity(level)` | ✓ | Moving band as one level, 0~9, or -1 for any motion at all. |
| `kw307_set_moving_hpf_corner(corner, nd)` | ✓ | Moving high-pass, directly. |
| `kw307_set_moving_lpf_corner(corner)` | ✓ | Moving low-pass. |
| `kw307_set_moving_background(update)` | ✗ | Freeze or keep adapting, moving band. |
| `kw307_en_moving_env_subtraction(on)` | ✗ | Subtract the environment, or pass it through. |
| `kw307_set_stationary_sensitivity(level)` | ✓ | Stationary band as one level, 0~9. |
| `kw307_set_stationary_hpf_corner(corner, nd)` | ✓ | Stationary high-pass. |
| `kw307_set_stationary_lpf_corner(corner)` | ✓ | Stationary low-pass. |
| `kw307_set_stationary_background(update)` | ✗ | Freeze or keep adapting, stationary band. |
| `kw307_en_stationary_env_subtraction(on)` | ✗ | Subtract the environment, or pass it through, stationary band. |
| `kw307_en_stationary_hold(on)` | ✗ | Hold the stationary results while that band is reconfigured. |

### Detection and reporting

| Call | Saved | Description |
|---|---|---|
| `kw307_set_detect_range(min_cm, max_cm)` | ✓ | Detect range: the near and far limits, in cm. |
| `kw307_set_detect_fov(fov_deg, shrink_rate_deg)` | ✓ | Field of view, and how it narrows with distance. |
| `kw307_set_fov_min(fov_deg)` | ✓ | The narrowest the shrink can make the FOV. |
| `kw307_set_mag_threshold(moving, stationary)` | ✓ | The detection threshold per band. |
| `kw307_en_noise_floor_report(on)` | ✓ | Report the room's noise floor in an empty slot's magnitude. |
| `kw307_set_presence_hold_time(frames)` | ✓ | How long the presence flag holds after the last detection, in frames (20 = 1 s). |
| `kw307_set_presence_hold_zone(on, distance_cm, fov_deg)` | ✓ | Hold presence while a target stays still in a near forward zone. |
| `kw307_en_gpio_presence_flag(on)` | ✗ | Mirror the presence flag on the GPIO pin. |
| `kw307_set_stationary_release_time(enable, frames)` | ✓ | Drop a stationary target after this many frames (20 = 1 s). |
| `kw307_set_output(mode)` | ✗ | Which frame the module sends: `_NORMAL`, `_NORMAL_WITH_SPD`, `_NORMAL_WITH_SPD_RG`, or `_OFF`. |
| `kw307_on_frame(cb)` | — | Receive `KW307_OUTPUT_NORMAL` frames. |
| `kw307_on_frame_spd(cb)` | — | Receive `KW307_OUTPUT_NORMAL_WITH_SPD` frames. |
| `kw307_on_frame_spd_rg(cb)` | — | Receive `KW307_OUTPUT_NORMAL_WITH_SPD_RG` frames. |

### Trackers

| Call | Saved | Description |
|---|---|---|
| `kw307_en_los(on)` | ✓ | Suppress targets directly behind a nearer one. On by default; turn it off to report every candidate. |
| `kw307_set_moving_tracker_cnt(cnt_limit, display_th)` | ✓ | Moving counters, in frames. |
| `kw307_set_motion_threshold(confirm_frames, min_speed, speed_clip, min_speed_sum)` | ✓ | What counts as movement. A motion level sets these. |
| `kw307_set_moving_smoothing(smoothing)` | ✓ | Smoothing of the reported moving-target position. |
| `kw307_set_advanced_clutter_rejection(en, spd_th, dev_th)` | ✓ | Reject repetitive motion such as a fan. |
| `kw307_set_stationary_tracker_cnt(cnt_limit, display_th)` | ✓ | Stationary counters. |
| `kw307_set_stationary_tracker_opt(confirm_frames, smoothing, match_distance_cm)` | ✓ | Confirmation, smoothing and association distance for stationary targets. |

### Custom frames

| Call | Saved | Description |
|---|---|---|
| `kw307_out_clear(c)` | — | All blocks off: the starting point of a new shape. |
| `kw307_out_normal_with_spd(c)` / `kw307_out_normal_with_spd_rg(c)` | — | Start from the `KW307_OUTPUT_NORMAL_WITH_SPD` or `KW307_OUTPUT_NORMAL_WITH_SPD_RG` shape instead. |
| `kw307_out_presence(c, on)` | — | The presence flag. |
| `kw307_out_moving_trk(c, on, full, speed)` | — | The two moving trackers, whole or state only, with or without speed. |
| `kw307_out_stationary(c, nearest, second)` | — | The nearest and second-nearest stationary targets. |
| `kw307_out_stationary_trk(c, on, full)` | — | The four stationary-band trackers. |
| `kw307_out_room_max(c, on)` | — | The room's peak magnitude per band. |
| `kw307_out_rg(c, start_rg, end_rg, moving, stationary, mag_only)` | — | Range gates, per band. |
| `kw307_out_len(c)` | — | The frame size that shape will produce. |
| `kw307_set_output_config(c)` | ✗ | Send the shape. Nothing reaches the module before this. |
| `kw307_set_output_rg(start_rg, end_rg, en_moving_trk, en_stationary_trk)` | ✗ | The board-tuning shape in one call: both bands' gates, trackers optional. |
| `kw307_on_frame_f3(cb)` | — | Receive any shape, decoded into `KW307FrameF3_t`. |
| `kw307_dec_frame_f3(data, len, out)` | — | Decode a 0xF3 payload received over your own transport. |

### PLL calibration

| Call | Saved | Description |
|---|---|---|
| `kw307_en_pll_cal_on_boot(on)` | ✓ | Calibrate fully at every power-on (default), or load the calibration saved by `kw307_save_settings()`. |
| `kw307_en_auto_pll_cal(on)` | ✓ | Re-calibrate periodically while running. Off by default. |
| `kw307_set_auto_pll_cal_interval(frames)` | ✓ | How often that happens, in frames. Default 6000. |
| `kw307_en_auto_pll_err_cal(on)` | ✓ | Whether the periodic calibration includes ERR. Off by default. |

## UART protocol

Everything above is the SDK speaking this protocol. This section is the protocol itself. You need the bytes in two cases: a host that does not run the SDK, and a link that misbehaves and needs a logic analyser on it.

### UART link settings

| Setting | Value |
|---|---|
| Baud rate | 115200 |
| Data bits · parity · stop bits | 8 · none · 1 |
| Flow control | None |
| Logic level | 3.3 V TTL — low 0 V, high 3.3 V |
| Byte order | Big-endian, MSB first, payloads included |

### UART frame format

```
00 00 00 00 00   AA  55   CMD   LEN_H LEN_L   DATA ...   CRC_H CRC_L
└── wake-up ──┘  └SOF┘
```

| Field | Description |
|---|---|
| Wake-up | At least five `0x00` bytes before **every** host frame. The module enters an idle low-power state between commands; these bytes wake it and are discarded. Only `0x00` wakes the module. |
| SOF | `AA 55` to write or to stream, `AA 56` to read. The second byte is the only thing that distinguishes a write from a read request. |
| CMD | What the frame is about. The same CMD number is used for the write and the read of a setting. |
| LEN | Payload length, big-endian, not counting CRC. |
| CRC | CRC-16, polynomial `0x8005`, seed `0xFFFF`, big-endian, computed over CMD + LEN + DATA. The SOF and the wake-up bytes are outside it. |

> **Send the wake-up bytes and the frame as one stream.** A gap lets the module go back into that idle state before the command arrives. The command is then lost with no error on either side: the frame was never received, so there is nothing to NACK. This is a common cause when a host that does not use the SDK receives no reply.

### Write and read requests

A **write** is `AA 55 [CMD] …`. The module replies with an ACK frame on CMD `0xAC` whose payload carries the CMD it is answering and a status byte:

| Status | Description |
|---|---|
| `0x00` | Done. |
| `0x01` | The value was out of range. |
| `0x02` | The module does not accept that request for this CMD. |
| `0x03` | CRC mismatch; the frame arrived damaged. |
| `0x04` | The length field did not match the payload. |

A CMD the module does not recognise is dropped, with no reply at all.

A **read** is `AA 56 [CMD]` with an empty or selector-only payload. The module answers with the same ACK frame and, when the status is `0x00`, follows it with the current value on the same CMD.

### Request timing

![Writes against the radar frames, a read, and a gap after the wake-up bytes.](guide/img/uart_timing.svg)

Normal operation is 20 Hz: the module runs a radar frame and sends its output about every 50 ms. Every host frame begins with the five wake-up bytes, which take about 434 µs at 115200 baud.

- **Write between two radar frames**: the module executes the command between the two radar frames and replies with an ACK. The next radar frame runs on schedule.
- **Write overrunning the next frame**: the module completes the command first and skips the radar frame that falls within it, so that radar frame sends no output. `kw307_save_settings()` (up to about 1 s) and `kw307_radar_init()` (about 130 ms) take long enough to do this. Send nothing else until they have completed.
- **Write during a radar frame**: the command is held in the module's receive buffer until the radar frame and its output are complete, so the ACK arrives within one radar frame, about 50 ms.
- **Read**: the module replies with an ACK, followed by the data. Each getter waits up to about 200 ms (`KW307_READ_TIMEOUT_MS`) and then returns `KW307_ERR_TIMEOUT`.
- **A gap after the wake-up bytes**: a frame that starts more than about 5 ms after the wake-up bytes arrives after the module has returned to its idle state. The command is lost, with neither an ACK nor a NACK. Once `AA` has been received, a gap of about 100 ms between two bytes discards the partial frame.

A setter does not wait for the ACK unless `kw307_write_blocking()` or `kw307_set_write_verify()` is used.

Output frames streamed by the module (`0xF0`, `0xF3`) arrive without a request, with the same header and CRC but no wake-up bytes, and no ACK: they are output, not a reply.

### Output frame layouts

`core/KW307_protocol.h` says what every field means, including the `state` values and which ones mean a Valid target. That header is the reference; this is only the byte layout, which no header carries.

Each of the three output modes has **one fixed layout**. Every multi-byte value is 16-bit **big-endian**, and distances on the wire are in **millimetres** — the SDK converts them to centimetres, a host parsing the bytes does not.

A **tracker** is the same 8 bytes everywhere it appears.

Its offsets carry a `+`: they count from the tracker's first byte, wherever it sits in the frame. The frame tables below count from the first byte of the payload.

| Offset | Field | Type |
|---|---|---|
| +0~1 | `distance_mm` | uint16 |
| +2~3 | `angle_tenth` | int16, 0.1 degree |
| +4~5 | `mag` | uint16 |
| +6 | `state` | uint8 |
| +7 | `counter` | uint8 |

#### `KW307_OUTPUT_NORMAL`, CMD `0xF0`, 27 bytes

| Offset | Field |
|---|---|
| 0~1 | `frame_index` |
| 2 | `presence_flag` |
| 3~10 | moving target 1 |
| 11~18 | moving target 2 |
| 19~26 | stationary target |

#### `KW307_OUTPUT_NORMAL_WITH_SPD`, CMD `0xF3`, 34 bytes

| Offset | Field |
|---|---|
| 0~1 | `frame_index` |
| 2~4 | `B0 2C 09` — the shape bytes, always these three in this mode |
| 5 | `presence_flag` |
| 6~13 | moving target 1 |
| 14~21 | moving target 2 |
| 22~25 | the two moving targets' speeds, int16 each |
| 26~33 | stationary target |

#### `KW307_OUTPUT_NORMAL_WITH_SPD_RG`, CMD `0xF3`, 66 bytes

| Offset | Field |
|---|---|
| 0~1 | `frame_index` |
| 2~4 | `30 2E 09` — the shape bytes, always these three in this mode |
| 5 | `presence_flag` |
| 6~13 | moving target 1 |
| 14~21 | moving target 2 |
| 22~25 | the two moving targets' speeds, int16 each |
| 26~33 | stationary target |
| 34~41 | `distance_mm`, stationary-band gates 0, 1, 2, 3 |
| 42~49 | `angle_tenth`, gates 0~3 |
| 50~57 | `mag`, gates 0~3 |
| 58~65 | speed, gates 0~3 |

Those are the raw per-gate readings, before grouping and before any tracker. A gate with nothing in it reads as noise, not as zero; a gate beyond the detect range reads zero.

> **Tell the two `0xF3` modes apart by bytes 2~4, never by length.** The two differ in those three bytes. A length check cannot tell a truncated frame of one shape from a whole frame of the other. `KW307_OUTPUT_NORMAL` has no shape bytes — it is `0xF0`, and only ever 27 bytes.
