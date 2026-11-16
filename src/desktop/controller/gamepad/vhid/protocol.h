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
 * The driver is a DualShock 4 (the PlayStation 4 controller) plugged by USB: the same identity, the same report descriptor and the same reports, so Steam and SDL know it as one, with nothing to configure.
 * As a real one, it has a single top-level collection (the gamepad), with:
 *   - the input report of the DualShock 4 (`PSPDISP_VHID_DS4_INPUT_REPORT_ID`), which the games read;
 *   - the output report of the DualShock 4 (`PSPDISP_VHID_DS4_OUTPUT_REPORT_ID`, the rumble and the light bar), which the driver takes and ignores;
 *   - the feature reports the games ask a DualShock 4 for (e.g. its calibration, and its MAC address, by which SDL knows it's plugged by USB);
 *   - and one output report of its own (`PSPDISP_VHID_CONTROL_REPORT_ID`, which no DualShock 4 has), the daemon writes the state of the gamepad to it (`WriteFile()`).
 * So the daemon needs no interface of its own: Windows lets the users write to a HID gamepad, as it lets them read it.
 * It tells the driver from a real DualShock 4 by its product string (`PSPDISP_VHID_PRODUCT_STRING`).
 *
 * It's plain C17 on purpose, without `shared.h`, because MSVC builds the driver and supports neither its C23 enums nor its GCC pragmas.
 */

#pragma once

#include <stdint.h>

#define PSPDISP_VHID_VENDOR_ID  0x054C // Sony, as a DualShock 4
#define PSPDISP_VHID_PRODUCT_ID 0x05C4 // DualShock 4 (the first one)
#define PSPDISP_VHID_VERSION    0x0100 // Version of the device, 1.0

#define PSPDISP_VHID_PRODUCT_STRING L"PSPdisp Virtual Gamepad" // Product string of the driver (a DualShock 4 says "Wireless Controller"), to tell it from a real one

#define PSPDISP_VHID_DS4_INPUT_REPORT_ID  0x01 // Input report of the DualShock 4, the state of the gamepad
#define PSPDISP_VHID_DS4_OUTPUT_REPORT_ID 0x05 // Output report of the DualShock 4, the rumble and the light bar (ignored)
#define PSPDISP_VHID_CONTROL_REPORT_ID    0x50 // Output report of the daemon, the state of the gamepad (not used by the DualShock 4)

#define PSPDISP_VHID_DS4_INPUT_REPORT_SIZE  64 // Size of the input report of the DualShock 4 by USB, with its ID
#define PSPDISP_VHID_OUTPUT_REPORT_SIZE     32 // Size of the output reports, with their ID: the one of the DualShock 4, so the control report has it too (Windows writes the longest one)

#define PSPDISP_VHID_STICK_CENTERED 0x80 // Position of a centered stick, on each axis
#define PSPDISP_VHID_HAT_CENTERED   8    // `PSPdispVhidState.hat` when the directional pad is released, out of the 0-7 range (a null state)

/**
 * @brief Buttons of the gamepad, in the order of the DualShock 4 (its HID buttons 1 to 14).
 */
#define PSPDISP_VHID_BUTTON_SQUARE   (1 << 0)  // Left face button
#define PSPDISP_VHID_BUTTON_CROSS    (1 << 1)  // Bottom face button
#define PSPDISP_VHID_BUTTON_CIRCLE   (1 << 2)  // Right face button
#define PSPDISP_VHID_BUTTON_TRIANGLE (1 << 3)  // Top face button
#define PSPDISP_VHID_BUTTON_L1       (1 << 4)  // Left shoulder button
#define PSPDISP_VHID_BUTTON_R1       (1 << 5)  // Right shoulder button
#define PSPDISP_VHID_BUTTON_L2       (1 << 6)  // Left trigger, pressed
#define PSPDISP_VHID_BUTTON_R2       (1 << 7)  // Right trigger, pressed
#define PSPDISP_VHID_BUTTON_SHARE    (1 << 8)  // Share
#define PSPDISP_VHID_BUTTON_OPTIONS  (1 << 9)  // Options
#define PSPDISP_VHID_BUTTON_L3       (1 << 10) // Left stick button
#define PSPDISP_VHID_BUTTON_R3       (1 << 11) // Right stick button
#define PSPDISP_VHID_BUTTON_PS       (1 << 12) // PS button
#define PSPDISP_VHID_BUTTON_TOUCHPAD (1 << 13) // Touchpad click

#pragma pack(push, 1)

/**
 * @brief State of the gamepad, which the daemon writes and the driver turns into the input report of the DualShock 4, little-endian and without padding.
 */
typedef struct {
	uint8_t lx;       // Left stick, from the left (0) to the right (255), `PSPDISP_VHID_STICK_CENTERED` when centered
	uint8_t ly;       // Left stick, from the top (0) to the bottom (255)
	uint8_t rx;       // Right stick, from the left (0) to the right (255)
	uint8_t ry;       // Right stick, from the top (0) to the bottom (255)
	uint8_t l2;       // Left trigger, from released (0) to pressed (255)
	uint8_t r2;       // Right trigger, from released (0) to pressed (255)
	uint8_t hat;      // Directional pad, 0 (up) to 7 clockwise by 45 degrees, `PSPDISP_VHID_HAT_CENTERED` when released
	uint16_t buttons; // The pressed buttons, `PSPDISP_VHID_BUTTON_*` bits
} PSPdispVhidState;

/**
 * @brief The output report of the daemon (`PSPDISP_VHID_CONTROL_REPORT_ID`), as long as the output report of the DualShock 4.
 */
typedef struct {
	uint8_t report_id;      // `PSPDISP_VHID_CONTROL_REPORT_ID`
	PSPdispVhidState state; // The state of the gamepad
	uint8_t reserved[PSPDISP_VHID_OUTPUT_REPORT_SIZE - 1 - sizeof(PSPdispVhidState)]; // 0
} PSPdispVhidReport;

#pragma pack(pop)
