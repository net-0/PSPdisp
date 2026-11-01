/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * vhid.c - Virtual gamepad using a HID driver (Win) *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * The virtual gamepad is made by the PSPdisp virtual HID gamepad driver (`vhid/driver.c`), which must be installed first (see the header of `vhid/pspdisp-vhid.inf`).
 *
 * This gamepad only talks to the driver: it opens the control collection of the driver among the HID devices, and writes each state to its output report (see `vhid/protocol.h`).
 * The gamepad has the layout of an Xbox controller under DirectInput (its buttons in the same order, the directional pad as a hat), so the games that map it by its buttons know it.
 *
 * The driver's gamepad exists while the driver is installed, not only while it's plugged here: it's set at rest when it's unplugged.
 * And as a HID gamepad, the games that only read the Xbox controllers through XInput don't see it.
 */

#pragma once

#include <winsock2.h> // Before <windows.h>, or its <winsock.h> clashes with the <winsock2.h> of GIO (e.g. <gio/gnetworking.h> in wlan.c)
#include <windows.h>
#include <cfgmgr32.h>
#include <hidsdi.h>
#include <hidpi.h>
#include "./vhid/protocol.h"
#include "../gamepad.c" // Its declarations (when `gamepad.c` includes this file, it already has them and this include is skipped)

#define PSPDISP_GAMEPAD_NAME "Virtual HID" // User friendly name of this virtual gamepad

/**
 * @brief Virtual gamepad using the PSPdisp virtual HID gamepad driver, created by `pspdisp_gamepad_new()`.
 */
struct PSPdispGamepad {
	HANDLE device; // The control collection of the driver, opened for writing
	PSPdispVhidReport report; // The report the driver has, to send only what changes
};

/**
 * @brief The buttons of the driver, with the gamepad button each one shows (0 for one the PSP doesn't have).
 */
const struct {
	uint16_t bit; // The bit of the button in the report, `PSPDISP_VHID_BUTTON_*`
	PSPdispGamepadButton button; // The button of the gamepad
} pspdisp_gamepad_vhid_buttons[] = {
	{ PSPDISP_VHID_BUTTON_A,     PSPDISP_GAMEPAD_BUTTON_SOUTH },
	{ PSPDISP_VHID_BUTTON_B,     PSPDISP_GAMEPAD_BUTTON_EAST },
	{ PSPDISP_VHID_BUTTON_X,     PSPDISP_GAMEPAD_BUTTON_WEST },
	{ PSPDISP_VHID_BUTTON_Y,     PSPDISP_GAMEPAD_BUTTON_NORTH },
	{ PSPDISP_VHID_BUTTON_LB,    PSPDISP_GAMEPAD_BUTTON_LEFT_SHOULDER },
	{ PSPDISP_VHID_BUTTON_RB,    PSPDISP_GAMEPAD_BUTTON_RIGHT_SHOULDER },
	{ PSPDISP_VHID_BUTTON_BACK,  PSPDISP_GAMEPAD_BUTTON_BACK },
	{ PSPDISP_VHID_BUTTON_START, PSPDISP_GAMEPAD_BUTTON_START },
	{ PSPDISP_VHID_BUTTON_LS,    0 }, // The left stick button
	{ PSPDISP_VHID_BUTTON_RS,    0 }, // The right stick button
};

/**
 * @brief Get the position of the hat from the directional pad: 0 (up) to 7 clockwise by 45 degrees, or released.
 *
 * @param buttons The pressed buttons, `PSPdispGamepadButton` bits.
 * @return The position of the hat, `PSPDISP_VHID_HAT_CENTERED` when released (or both directions of an axis are pressed).
 */
uint8_t pspdisp_gamepad_vhid_hat(uint16_t buttons) {
	int x = ((buttons & PSPDISP_GAMEPAD_BUTTON_DPAD_RIGHT) != 0) - ((buttons & PSPDISP_GAMEPAD_BUTTON_DPAD_LEFT) != 0);
	int y = ((buttons & PSPDISP_GAMEPAD_BUTTON_DPAD_DOWN) != 0) - ((buttons & PSPDISP_GAMEPAD_BUTTON_DPAD_UP) != 0);

	// The positions by (y + 1) * 3 + (x + 1), from the top left to the bottom right
	const uint8_t positions[9] = { 7, 0, 1, 6, PSPDISP_VHID_HAT_CENTERED, 2, 5, 4, 3 };
	return positions[(y + 1) * 3 + (x + 1)];
}

/**
 * @brief Build the report of the driver for a state of the gamepad.
 *
 * @param state The state of the gamepad.
 * @param report Where to write the report.
 */
void pspdisp_gamepad_vhid_report(const PSPdispGamepadState *state, PSPdispVhidReport *report) {
	*report = (PSPdispVhidReport) { .report_id = PSPDISP_VHID_CONTROL_REPORT_ID };
	report->state.x = state->left_x;
	report->state.y = state->left_y;
	report->state.hat = pspdisp_gamepad_vhid_hat(state->buttons);

	for (size_t i = 0; i < G_N_ELEMENTS(pspdisp_gamepad_vhid_buttons); i++) {
		if ((state->buttons & pspdisp_gamepad_vhid_buttons[i].button) != 0 && pspdisp_gamepad_vhid_buttons[i].button != 0)
			report->state.buttons |= pspdisp_gamepad_vhid_buttons[i].bit;
	}
}

/**
 * @brief Tell if a HID device is the control collection of the driver: its vendor and product IDs, and its usage.
 *
 * @param device The HID device, opened (even without access).
 * @return `true` if it's the control collection.
 */
bool pspdisp_gamepad_vhid_is_control(HANDLE device) {
	HIDD_ATTRIBUTES attributes = { .Size = sizeof(attributes) };
	PHIDP_PREPARSED_DATA preparsed = NULL;
	HIDP_CAPS caps = {0};

	if (!HidD_GetAttributes(device, &attributes) || attributes.VendorID != PSPDISP_VHID_VENDOR_ID || attributes.ProductID != PSPDISP_VHID_PRODUCT_ID) return false;
	if (!HidD_GetPreparsedData(device, &preparsed)) return false;

	bool control = HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS && caps.UsagePage == PSPDISP_VHID_CONTROL_USAGE_PAGE && caps.Usage == PSPDISP_VHID_CONTROL_USAGE;
	HidD_FreePreparsedData(preparsed);
	return control;
}

/**
 * @brief Open the control collection of the driver, among the HID devices.
 *
 * @return The control collection, opened for writing, or `INVALID_HANDLE_VALUE` if it wasn't found or failed to open.
 */
HANDLE pspdisp_gamepad_vhid_open() {
	GUID guid;
	ULONG length = 0;
	HANDLE found = INVALID_HANDLE_VALUE;
	HidD_GetHidGuid(&guid);

	// A list of device interface paths, each terminated by a null character, and the list by another one
	if (CM_Get_Device_Interface_List_SizeW(&length, &guid, NULL, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS || length <= 1) return INVALID_HANDLE_VALUE;

	WCHAR *paths = g_new0(WCHAR, length);
	if (CM_Get_Device_Interface_ListW(&guid, NULL, paths, length, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) == CR_SUCCESS) {
		for (WCHAR *path = paths; *path != L'\0' && found == INVALID_HANDLE_VALUE; path += wcslen(path) + 1) {
			// Without access first, enough to read its attributes, and allowed even for the devices the system keeps (e.g. the keyboards)
			HANDLE device = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
			if (device == INVALID_HANDLE_VALUE) continue;

			bool control = pspdisp_gamepad_vhid_is_control(device);
			CloseHandle(device);
			if (control)
				found = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		}
	}

	g_free(paths);
	return found;
}

/**
 * @brief Write a report to the driver.
 *
 * @param device The control collection of the driver.
 * @param report The report.
 * @return `true` if the driver took it.
 */
bool pspdisp_gamepad_vhid_write(HANDLE device, const PSPdispVhidReport *report) {
	DWORD written = 0;
	return WriteFile(device, report, sizeof(*report), &written, NULL) && written == sizeof(*report);
}

/**
 * @brief Plug the virtual gamepad: open the control collection of the driver, and set the gamepad at rest.
 *
 * @param name Unused, the name the computer would show for it: the driver names it "PSPdisp Virtual Gamepad".
 * @param error Where to tell why it failed.
 * @return The gamepad, or `NULL` if it failed.
 */
PSPdispGamepad *pspdisp_gamepad_new(const char *name, GError **error) {
	(void) name;

	HANDLE device = pspdisp_gamepad_vhid_open();
	if (device == INVALID_HANDLE_VALUE) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "the PSPdisp virtual gamepad driver is not installed (see pspdisp-vhid.inf), install it or choose a control file instead");
		return NULL;
	}

	PSPdispGamepad *gamepad = g_new0(PSPdispGamepad, 1);
	gamepad->device = device;
	pspdisp_gamepad_vhid_report(&(PSPdispGamepadState) {0}, &gamepad->report);

	if (!pspdisp_gamepad_vhid_write(device, &gamepad->report)) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "the PSPdisp virtual gamepad driver refused the state of the gamepad (error %lu)", GetLastError());
		pspdisp_gamepad_destroy(gamepad);
		return NULL;
	}
	return gamepad;
}

/**
 * @brief Send the state of the gamepad to the driver, if it changed.
 *
 * @param gamepad The gamepad.
 * @param state The new state.
 * @return `true` if the driver took it.
 */
bool pspdisp_gamepad_update(PSPdispGamepad *gamepad, const PSPdispGamepadState *state) {
	PSPdispVhidReport report;
	pspdisp_gamepad_vhid_report(state, &report);

	if (memcmp(&report, &gamepad->report, sizeof(report)) == 0) return true;

	if (!pspdisp_gamepad_vhid_write(gamepad->device, &report)) {
		g_warning("%s(): failed to send the state of the gamepad (error %lu)", __FUNCTION__, GetLastError());
		return false;
	}

	gamepad->report = report;
	return true;
}

/**
 * @brief Unplug the virtual gamepad and free it: the driver's gamepad stays, set at rest.
 *
 * @param gamepad The gamepad to destroy.
 */
void pspdisp_gamepad_destroy(PSPdispGamepad *gamepad) {
	PSPdispVhidReport rest;
	pspdisp_gamepad_vhid_report(&(PSPdispGamepadState) {0}, &rest);

	pspdisp_gamepad_vhid_write(gamepad->device, &rest);
	CloseHandle(gamepad->device);
	g_free(gamepad);
}
