/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * protocol.h - HID gamepad driver and its connector *
 *                                                   *
 *  This file shall not define symbols.              *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * Protocol between the PSPdisp virtual HID gamepad driver (`driver.c`, built with MSVC and the WDK) and the virtual gamepad of the daemon (`../vhid.c`, built with MinGW).
 *
 * The driver is the DualShock 4 of the virtual gamepad (see `../ds4.c`), plugged by USB: its identity, its report descriptor and its reports, so Steam and SDL know it as one, with nothing to configure.
 * As a real one, it has a single top-level collection (the gamepad), with:
 *   - the input report of the DualShock 4, which the games read;
 *   - the output report of the DualShock 4 (the rumble and the light bar), which the driver takes and ignores;
 *   - the feature reports the games ask a DualShock 4 for (e.g. its calibration, and its MAC address, by which SDL knows it's plugged by USB);
 *   - and the control report of PSPdisp (`PSPdispDS4ControlReport`, which no DualShock 4 has), the daemon writes the state of the gamepad to it (`WriteFile()`).
 * So the daemon needs no interface of its own: Windows lets the users write to a HID gamepad, as it lets them read it.
 * It tells the driver from a real DualShock 4 by its product string (`PSPDISP_VHID_PRODUCT_STRING`).
 *
 * It's plain C17 on purpose, without `shared.h`, because MSVC builds the driver and supports neither its C23 enums nor its GCC pragmas.
 */

#pragma once

#define PSPDISP_VHID_VERSION 0x0100 // Version of the device, 1.0

#define PSPDISP_VHID_PRODUCT_STRING L"PSPdisp Virtual Gamepad" // Product string of the driver (a DualShock 4 says "Wireless Controller"), to tell it from a real one
