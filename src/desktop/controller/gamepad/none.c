/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * none.c - No virtual gamepad (Windows)             *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * The virtual gamepad of the platforms that don't have one yet: it always fails to plug, telling the user to pick a control file instead.
 *
 * Windows needs a driver to show a gamepad (e.g. ViGEmBus, or a Virtual HID Framework driver of PSPdisp, as the IddCx one), see the TODO.
 */

#pragma once

#include <gio/gio.h>
#include "../gamepad.c" // Its declarations (when `gamepad.c` includes this file, it already has them and this include is skipped)

#define PSPDISP_GAMEPAD_NAME "none" // User friendly name of this virtual gamepad

/**
 * @brief Virtual gamepad that never exists, as `pspdisp_gamepad_new()` always fails.
 */
struct PSPdispGamepad {
	PSPdispGamepadState state; // Unused, a struct needs a member
};

/**
 * @brief Fail to plug a virtual gamepad, there's none on this platform yet.
 *
 * @param name Unused, the name the computer would show for it.
 * @param error Where to tell why it failed.
 * @return Always `NULL`.
 */
PSPdispGamepad *pspdisp_gamepad_new(const char *name, GError **error) {
	(void) name;
	g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "there's no virtual gamepad on this system yet, choose a control file instead");
	return NULL;
}

/**
 * @brief Never called, as no gamepad is ever plugged.
 *
 * @param gamepad Unused, the gamepad.
 * @param state Unused, the new state.
 * @return Always `false`.
 */
bool pspdisp_gamepad_update(PSPdispGamepad *gamepad, const PSPdispGamepadState *state) {
	(void) gamepad;
	(void) state;
	return false;
}

/**
 * @brief Free the gamepad, never called, as no gamepad is ever plugged.
 *
 * @param gamepad The gamepad to destroy.
 */
void pspdisp_gamepad_destroy(PSPdispGamepad *gamepad) {
	g_free(gamepad);
}
