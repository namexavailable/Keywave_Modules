============================================================
 KW007 SDK v1.2
 Keywave KW007 5.8 GHz Doppler Radar Motion Sensor
 Release Date: 2026-08-24
============================================================

PACKAGE CONTENTS
----------------
  SDK\           Sensor driver source. Add these files to your
                 project.

  Examples\      A complete, compilable example — motion-triggered
                 LED output.

  Docs\          Cross-platform HTML API reference, the KW007
                 Programming Guide PDF, and optional legacy CHM help.
                 Open Docs\README.txt first; the recommended help entry
                 point is Docs\html\index.html.


GETTING STARTED
---------------
  1. Add to your project:

        SDK\KW007_api.c
        SDK\kw007_reg.c
        SDK\User_platform\   — the HAL for your platform

  2. Add SDK\ to the include path.

  3. Write your application against KW007_api.h:

        KW007_Init();                 /* apply start-up settings */
        KW007_SetDetectionRange(4);   /* ~3 m detection          */
        KW007_Data d = KW007_Read();

     Examples\kw007_example_led.c is a working program you can
     build as-is.

  Ready-made HAL files in SDK\User_platform\ :

        kw007_hal_arduino.cpp    Arduino / ESP32
        kw007_hal_stm32.c        STM32 (STM32 HAL library)
        kw007_hal_ftdi.cpp       Windows + FTDI USB-I²C
        kw007_hal_user.c         Template for any other platform


BEFORE APPLYING POWER
---------------------
  The Range1 pin must be tied to GND through a protection
  resistor. Without this the sensor stays in hardware
  (pin-select) mode and ignores all settings sent over I²C.

  Use either the 3.3 V or the 5 V input, never both, and never
  apply 5 V to the 3.3 V input — doing so will damage the module.

  Wiring details are in the KW007 Programming Guide.


REQUIREMENTS
------------
  Any C99 compiler. The driver uses only stdint.h, stdbool.h
  and string.h — no dynamic memory and no floating point.


SUPPORT
-------
  For technical inquiries regarding the KW007 sensor or SDK,
  please contact your Keywave representative.

------------------------------------------------------------
 © 2026 Keywave Technology. All product names and trademarks
 are the property of their respective owners.
============================================================

LICENSING
---------
  Copyright 2026 Keywave Technology.
  The first-party contents of this package, including SDK source,
  examples, README files, the programming-guide PDF, and generated
  HTML and CHM documentation, are licensed under the Apache License,
  Version 2.0 (SPDX: Apache-2.0). See LICENSE for the complete text.

  Bundled third-party documentation assets retain their original
  licenses. See THIRD_PARTY_NOTICES.txt and the notices in those files.
  External platform libraries referenced by the HAL implementations
  are not included in, or relicensed by, this SDK.
