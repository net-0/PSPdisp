/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * controller.c - PSP as a gamepad of the computer   *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * The virtual controller: what the buttons and the analog stick of the PSP drive on the computer, a virtual gamepad, or a virtual keyboard and mouse pressed as a control file says (see `control_file.c`).
 *
 * The PSP buttons go to the buttons at the same place on the gamepad, a DualShock 4 (see `gamepad/ds4.c`), which have the same symbols: cross, circle, square and triangle.
 * L and R are L1 and R1, select and start are share and options, the directional pad is the directional pad, and the analog stick is the left stick.
 * The analog stick of the PSP rests a bit off its center, by a different amount on each PSP, so a dead zone around the center reads as centered (see `PSPDISP_CONTROLLER_DEAD_ZONE`).
 *
 * The virtual gamepad itself varies with the platform (see `gamepad.c`), as the virtual keyboard and mouse (see `input.c`).
 */

#pragma once

#include <math.h>
#include <stdint.h>
#include <glib.h>
#include "../../shared.h"
#include "./gamepad.c"
#include "./control_file.c"

#define PSPDISP_CONTROLLER_NAME      "PSPdisp virtual gamepad" // Name the computer shows for the gamepad
#define PSPDISP_CONTROLLER_CENTER    127.5                     // Center of the analog stick of the PSP, on each axis (from 0 to 255)
#define PSPDISP_CONTROLLER_DEAD_ZONE 32.0                      // Distance from the center under which the analog stick reads as centered, from 0 to 127.5 (a quarter of the way, as the dead zone XInput advises)

/**
 * @brief The PSP buttons, with the button of the gamepad each one drives (the directional pad drives its hat instead, see `pspdisp_controller_map()`).
 */
const struct {
	PSPdispButton psp; // The PSP button
	uint16_t gamepad; // The button of the gamepad, `PSPDISP_DS4_BUTTON_*`
} pspdisp_controller_buttons[] = {
	{ PSPDISP_BUTTON_CROSS,    PSPDISP_DS4_BUTTON_CROSS },
	{ PSPDISP_BUTTON_CIRCLE,   PSPDISP_DS4_BUTTON_CIRCLE },
	{ PSPDISP_BUTTON_SQUARE,   PSPDISP_DS4_BUTTON_SQUARE },
	{ PSPDISP_BUTTON_TRIANGLE, PSPDISP_DS4_BUTTON_TRIANGLE },
	{ PSPDISP_BUTTON_L,        PSPDISP_DS4_BUTTON_L1 },
	{ PSPDISP_BUTTON_R,        PSPDISP_DS4_BUTTON_R1 },
	{ PSPDISP_BUTTON_SELECT,   PSPDISP_DS4_BUTTON_SHARE },
	{ PSPDISP_BUTTON_START,    PSPDISP_DS4_BUTTON_OPTIONS },
};

/**
 * @brief The virtual controller, driven by the PSP: a gamepad, or a keyboard and a mouse pressed as a control file says.
 */
typedef struct {
	PSPdispGamepad *gamepad; // The virtual gamepad of the platform (`NULL` with a control file)
	PSPdispControlFile *control_file; // The control file, with its virtual keyboard and mouse (`NULL` with the gamepad)
} PSPdispController;

/**
 * @brief Plug the virtual gamepad into the computer, at rest.
 *
 * @param error Where to tell why it failed, ready to be shown to the user (e.g. the permission it needs).
 * @return The controller, free it with `pspdisp_controller_destroy()`, or `NULL` if it failed.
 */
PSPdispController *pspdisp_controller_new(GError **error) {
	PSPdispGamepad *gamepad = pspdisp_gamepad_new(PSPDISP_CONTROLLER_NAME, error);
	if (gamepad == NULL) return NULL;

	PSPdispController *controller = g_new0(PSPdispController, 1);
	controller->gamepad = gamepad;
	return controller;
}

/**
 * @brief Load a control file, and plug the virtual keyboard and mouse it presses into the computer.
 *
 * @param path The control file.
 * @param error Where to tell why it failed, ready to be shown to the user (e.g. the permission it needs).
 * @return The controller, free it with `pspdisp_controller_destroy()`, or `NULL` if it failed.
 */
PSPdispController *pspdisp_controller_new_control_file(const char *path, GError **error) {
	PSPdispControlFile *control_file = pspdisp_control_file_new(path, error);
	if (control_file == NULL) return NULL;

	PSPdispController *controller = g_new0(PSPdispController, 1);
	controller->control_file = control_file;
	return controller;
}

/**
 * @brief Unplug the virtual devices and free the controller.
 *
 * @param controller The controller to destroy.
 */
void pspdisp_controller_destroy(PSPdispController *controller) {
	if (controller->gamepad != NULL)
		pspdisp_gamepad_destroy(controller->gamepad);
	if (controller->control_file != NULL)
		pspdisp_control_file_destroy(controller->control_file);
	g_free(controller);
}

/**
 * @brief Tell if the PSP may open its on-screen keyboard, to type with it (`EnableOSK` of the control file).
 *
 * @param controller The controller.
 * @return `true` with a control file that enables it.
 */
bool pspdisp_controller_enables_osk(PSPdispController *controller) {
	return controller->control_file != NULL && controller->control_file->enable_osk;
}

/**
 * @brief Get how many times the PSP asked to open the menu of PSPdisp (the `OpenMenu` action of a control file).
 *
 * @param controller The controller.
 * @return The count, since the controller was created.
 */
uint32_t pspdisp_controller_open_menu_requests(PSPdispController *controller) {
	return controller->control_file != NULL ? controller->control_file->open_menu_requests : 0;
}

/**
 * @brief Turn the state of the PSP controls into the state of the gamepad.
 *
 * The dead zone is round, and the stick is scaled from its edge to the edge of the analog stick, so the whole range of the gamepad is still reached.
 * A stick pushed into a corner is brought back to the edge of the circle, as on an Xbox controller.
 *
 * @param buttons The pressed buttons of the PSP, `PSPdispButton` bits.
 * @param analog_x The analog stick of the PSP, from the left (0) to the right (255).
 * @param analog_y The analog stick of the PSP, from the top (0) to the bottom (255).
 * @return The state of the gamepad.
 */
PSPdispDS4State pspdisp_controller_map(uint32_t buttons, uint8_t analog_x, uint8_t analog_y) {
	PSPdispDS4State state = pspdisp_ds4_rest_state;

	for (size_t i = 0; i < G_N_ELEMENTS(pspdisp_controller_buttons); i++)
		if (buttons & pspdisp_controller_buttons[i].psp)
			state.buttons |= pspdisp_controller_buttons[i].gamepad;

	// The directional pad, by the direction pressed on each axis (none when both are)
	const int dpad_x = ((buttons & PSPDISP_BUTTON_RIGHT) != 0) - ((buttons & PSPDISP_BUTTON_LEFT) != 0);
	const int dpad_y = ((buttons & PSPDISP_BUTTON_DOWN) != 0) - ((buttons & PSPDISP_BUTTON_UP) != 0);
	state.hat = pspdisp_ds4_hat(dpad_x, dpad_y);

	double x = analog_x - PSPDISP_CONTROLLER_CENTER;
	double y = analog_y - PSPDISP_CONTROLLER_CENTER;
	double distance = sqrt(x * x + y * y);

	if (distance > PSPDISP_CONTROLLER_DEAD_ZONE) {
		double scale = MIN((distance - PSPDISP_CONTROLLER_DEAD_ZONE) / (PSPDISP_CONTROLLER_CENTER - PSPDISP_CONTROLLER_DEAD_ZONE), 1.0) / distance; // To a distance from 0 to 1
		// To the range of the left stick, the one of the analog stick of the PSP (from 0 to 255)
		state.lx = (uint8_t) lround(PSPDISP_CONTROLLER_CENTER * (1 + x * scale));
		state.ly = (uint8_t) lround(PSPDISP_CONTROLLER_CENTER * (1 + y * scale));
	}

	return state;
}

/**
 * @brief Drive the virtual devices with the state of the PSP controls.
 *
 * Only what changed is sent to the computer, so it can be called with each response of the PSP.
 *
 * @param controller The controller.
 * @param buttons The pressed buttons of the PSP, `PSPdispButton` bits, with a character of the on-screen keyboard in the highest byte.
 * @param analog_x The analog stick of the PSP, from the left (0) to the right (255).
 * @param analog_y The analog stick of the PSP, from the top (0) to the bottom (255).
 * @return `true` if the computer took it.
 */
bool pspdisp_controller_update(PSPdispController *controller, uint32_t buttons, uint8_t analog_x, uint8_t analog_y) {
	if (controller->control_file != NULL) {
		pspdisp_control_file_update(controller->control_file, buttons, analog_x, analog_y);
		return true;
	}

	PSPdispDS4State state = pspdisp_controller_map(buttons, analog_x, analog_y);
	return pspdisp_gamepad_update(controller->gamepad, &state);
}
