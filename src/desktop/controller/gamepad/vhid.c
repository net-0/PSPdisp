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
 * This gamepad only talks to the driver: it opens the driver among the HID devices, and writes each state to its control report (see `vhid/protocol.h`).
 * The driver is the DualShock 4 of the virtual gamepad (see `ds4.c`), so Steam and SDL know it as one, with nothing to configure.
 *
 * The driver's gamepad exists while the driver is installed, not only while it's plugged here: it's set at rest when it's unplugged.
 * And as a real DualShock 4, the games that only read the Xbox controllers through XInput don't see it, unless Steam Input stands for it.
 */

#pragma once

#include <winsock2.h> // Before <windows.h>, or its <winsock.h> clashes with the <winsock2.h> of GIO (e.g. <gio/gnetworking.h> in wlan.c)
#include <windows.h>
#include <cfgmgr32.h>
#include <hidsdi.h>
#include "./ds4.c"
#include "./vhid/protocol.h"
#include "../gamepad.c" // Its declarations (when `gamepad.c` includes this file, it already has them and this include is skipped)

#define PSPDISP_GAMEPAD_NAME "Virtual HID" // User friendly name of this virtual gamepad

/**
 * @brief Virtual gamepad using the PSPdisp virtual HID gamepad driver, created by `pspdisp_gamepad_new()`.
 */
struct PSPdispGamepad {
	HANDLE device; // The driver, opened for writing
	PSPdispDS4State state; // The state the driver has, to send only what changes
};

/**
 * @brief Tell if a HID device is the driver: the vendor and product IDs of a DualShock 4, with the product string of the driver.
 *
 * @param device The HID device, opened (even without access).
 * @return `true` if it's the driver.
 */
bool pspdisp_gamepad_vhid_is_driver(HANDLE device) {
	HIDD_ATTRIBUTES attributes = { .Size = sizeof(attributes) };
	WCHAR product[64] = {0};

	if (!HidD_GetAttributes(device, &attributes) || attributes.VendorID != PSPDISP_DS4_VENDOR_ID || attributes.ProductID != PSPDISP_DS4_PRODUCT_ID) return false;
	return HidD_GetProductString(device, product, sizeof(product) - sizeof(WCHAR)) && wcscmp(product, PSPDISP_VHID_PRODUCT_STRING) == 0;
}

/**
 * @brief Open the driver, among the HID devices.
 *
 * @return The driver, opened for writing, or `INVALID_HANDLE_VALUE` if it wasn't found or failed to open.
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

			bool driver = pspdisp_gamepad_vhid_is_driver(device);
			CloseHandle(device);
			if (driver)
				found = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		}
	}

	g_free(paths);
	return found;
}

/**
 * @brief Write a state of the gamepad to the driver, in its control report.
 *
 * @param device The driver.
 * @param state The state.
 * @return `true` if the driver took it.
 */
bool pspdisp_gamepad_vhid_write(HANDLE device, const PSPdispDS4State *state) {
	const PSPdispDS4ControlReport report = { .report_id = PSPDISP_DS4_CONTROL_REPORT_ID, .state = *state };
	DWORD written = 0;
	return WriteFile(device, &report, sizeof(report), &written, NULL) && written == sizeof(report);
}

/**
 * @brief Plug the virtual gamepad: open the driver, and set the gamepad at rest.
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
	gamepad->state = pspdisp_ds4_rest_state;

	if (!pspdisp_gamepad_vhid_write(device, &gamepad->state)) {
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
bool pspdisp_gamepad_update(PSPdispGamepad *gamepad, const PSPdispDS4State *state) {
	if (memcmp(state, &gamepad->state, sizeof(*state)) == 0) return true;

	if (!pspdisp_gamepad_vhid_write(gamepad->device, state)) {
		g_warning("%s(): failed to send the state of the gamepad (error %lu)", __FUNCTION__, GetLastError());
		return false;
	}

	gamepad->state = *state;
	return true;
}

/**
 * @brief Unplug the virtual gamepad and free it: the driver's gamepad stays, set at rest.
 *
 * @param gamepad The gamepad to destroy.
 */
void pspdisp_gamepad_destroy(PSPdispGamepad *gamepad) {
	pspdisp_gamepad_vhid_write(gamepad->device, &pspdisp_ds4_rest_state);
	CloseHandle(gamepad->device);
	g_free(gamepad);
}
