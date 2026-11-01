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
 * The driver is a HID device with two top-level collections, which Windows opens as two HID devices:
 *   - the gamepad (report ID `PSPDISP_VHID_GAMEPAD_REPORT_ID`), an input report the games read;
 *   - the control (report ID `PSPDISP_VHID_CONTROL_REPORT_ID`, vendor usage page), an output report the daemon writes the state of the gamepad to (`WriteFile()`).
 * So the daemon needs no interface of its own: Windows lets the users write to a HID collection, as it lets them read a gamepad.
 *
 * It's plain C17 on purpose, without `shared.h`, because MSVC builds the driver and supports neither its C23 enums nor its GCC pragmas.
 */

#pragma once

#include <stdint.h>

#define PSPDISP_VHID_VENDOR_ID  0x1209 // pid.codes, the vendor ID of the open source projects
#define PSPDISP_VHID_PRODUCT_ID 0x0001 // A test product ID of pid.codes, to be replaced by one requested for PSPdisp before a release
#define PSPDISP_VHID_VERSION    0x0100 // Version of the device, 1.0

#define PSPDISP_VHID_GAMEPAD_REPORT_ID 1 // Report ID of the input report of the gamepad
#define PSPDISP_VHID_CONTROL_REPORT_ID 2 // Report ID of the output report of the control collection

#define PSPDISP_VHID_CONTROL_USAGE_PAGE 0xFF00 // Usage page of the control collection (vendor defined), to find it among the HID devices
#define PSPDISP_VHID_CONTROL_USAGE      0x0001 // Usage of the control collection

#define PSPDISP_VHID_HAT_CENTERED 8 // `PSPdispVhidState.hat` when the directional pad is released, out of the 0-7 range (a null state)

/**
 * @brief Buttons of the gamepad, in the order of an Xbox controller under DirectInput, so the games that map it by its buttons know them.
 */
#define PSPDISP_VHID_BUTTON_A     (1 << 0) // Bottom face button
#define PSPDISP_VHID_BUTTON_B     (1 << 1) // Right face button
#define PSPDISP_VHID_BUTTON_X     (1 << 2) // Left face button
#define PSPDISP_VHID_BUTTON_Y     (1 << 3) // Top face button
#define PSPDISP_VHID_BUTTON_LB    (1 << 4) // Left shoulder button
#define PSPDISP_VHID_BUTTON_RB    (1 << 5) // Right shoulder button
#define PSPDISP_VHID_BUTTON_BACK  (1 << 6) // Back
#define PSPDISP_VHID_BUTTON_START (1 << 7) // Start
#define PSPDISP_VHID_BUTTON_LS    (1 << 8) // Left stick button
#define PSPDISP_VHID_BUTTON_RS    (1 << 9) // Right stick button

#pragma pack(push, 1)

/**
 * @brief State of the gamepad, the data of both reports, little-endian and without padding (as the report descriptor of the driver describes it).
 */
typedef struct {
	int16_t x;        // Left stick, from the left (-32768) to the right (32767)
	int16_t y;        // Left stick, from the top (-32768) to the bottom (32767)
	int16_t rx;       // Right stick, from the left (-32768) to the right (32767)
	int16_t ry;       // Right stick, from the top (-32768) to the bottom (32767)
	uint8_t z;        // Left trigger, from released (0) to pressed (255)
	uint8_t rz;       // Right trigger, from released (0) to pressed (255)
	uint8_t hat;      // Directional pad, 0 (up) to 7 clockwise by 45 degrees, `PSPDISP_VHID_HAT_CENTERED` when released
	uint16_t buttons; // The pressed buttons, `PSPDISP_VHID_BUTTON_*` bits
} PSPdispVhidState;

/**
 * @brief A report of the driver: the input report of the gamepad, or the output report of the control collection (they only differ by their ID).
 */
typedef struct {
	uint8_t report_id;      // `PSPDISP_VHID_GAMEPAD_REPORT_ID` or `PSPDISP_VHID_CONTROL_REPORT_ID`
	PSPdispVhidState state; // The state of the gamepad
} PSPdispVhidReport;

#pragma pack(pop)
