/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * gamepad.c - Virtual gamepad of the platform       *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * A virtual gamepad that the computer sees as a plugged DualShock 4 (the PlayStation 4 controller, see `gamepad/ds4.c`), so the games know it, and show the symbols of the PSP buttons.
 *
 * How it's plugged varies with the platform, chosen at compile time: uhid on Linux (`gamepad/uhid.c`), the PSPdisp virtual HID gamepad driver on Windows (`gamepad/vhid.c`).
 */

#pragma once

#include <glib.h>
#include "./gamepad/ds4.c"

/**
 * @brief Virtual gamepad.
 *
 * Its platform implementation (included below, e.g. `gamepad/uhid.c`) declares this struct with the data it needs, and defines `PSPDISP_GAMEPAD_NAME`.
 */
typedef struct PSPdispGamepad PSPdispGamepad;

/**
 * @brief Plug a virtual gamepad into the computer, at rest, implemented by the platform.
 *
 * @param name The name the computer shows for it.
 * @param error Where to tell why it failed, ready to be shown to the user (e.g. the permission it needs).
 * @return The gamepad, unplug it with `pspdisp_gamepad_destroy()`, or `NULL` if it failed.
 */
PSPdispGamepad *pspdisp_gamepad_new(const char *name, GError **error);

/**
 * @brief Set the state of the virtual gamepad, implemented by the platform.
 *
 * It can be called with the same state again and again.
 *
 * @param gamepad The gamepad.
 * @param state The new state.
 * @return `true` if the computer took it.
 */
bool pspdisp_gamepad_update(PSPdispGamepad *gamepad, const PSPdispDS4State *state);

/**
 * @brief Unplug the virtual gamepad and free it, implemented by the platform.
 *
 * @param gamepad The gamepad to destroy.
 */
void pspdisp_gamepad_destroy(PSPdispGamepad *gamepad);

// The virtual gamepad of this platform, it declares `PSPdispGamepad`, implements its functions and defines `PSPDISP_GAMEPAD_NAME`
#if defined(_WIN32)
	#include "./gamepad/vhid.c"
#elif defined(__linux__)
	#include "./gamepad/uhid.c"
#else
	#error "Unsupported platform: there's no virtual gamepad for it yet"
#endif
