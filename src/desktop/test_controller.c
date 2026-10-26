/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * test_controller.c - Drive the virtual gamepad     *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * Test of the virtual controller, without the PSP: plugs the virtual gamepad, then plays the PSP controls until Ctrl+C.
 * It presses each PSP button in turn, then turns the analog stick around twice, and starts again.
 *
 * Watch it with a gamepad tester (e.g. `evtest`, `jstest-gtk`, or the controller settings of Steam): it shows up as an Xbox 360 controller.
 * It needs to write to `/dev/uinput`, which the udev rule of the daemon gives to the user at the computer (or the `input` group on some distributions).
 */

#include "controller/controller.c"
#include <glib-unix.h>
#include <signal.h>

#define TEST_CONTROLLER_STEP 50 // Time between two steps of the demo, in milliseconds

/**
 * @brief The PSP buttons pressed in turn, with their names.
 */
const struct {
	PSPdispButton button; // The button
	const char *name; // Its name, as on the PSP
} test_controller_buttons[] = {
	{ PSPDISP_BUTTON_CROSS, "cross" },
	{ PSPDISP_BUTTON_CIRCLE, "circle" },
	{ PSPDISP_BUTTON_SQUARE, "square" },
	{ PSPDISP_BUTTON_TRIANGLE, "triangle" },
	{ PSPDISP_BUTTON_L, "L" },
	{ PSPDISP_BUTTON_R, "R" },
	{ PSPDISP_BUTTON_SELECT, "select" },
	{ PSPDISP_BUTTON_START, "start" },
	{ PSPDISP_BUTTON_UP, "up" },
	{ PSPDISP_BUTTON_DOWN, "down" },
	{ PSPDISP_BUTTON_LEFT, "left" },
	{ PSPDISP_BUTTON_RIGHT, "right" },
};

#define TEST_CONTROLLER_BUTTON_STEPS 10 // Steps of each button: pressed for 8, released for 2
#define TEST_CONTROLLER_STICK_STEPS  80 // Steps of the analog stick: two turns of 40 steps (2 seconds each)

PSPdispController *test_controller = NULL; // The virtual controller
guint test_controller_step = 0; // Current step of the demo

/**
 * @brief Play the next step of the demo, called every `TEST_CONTROLLER_STEP`.
 *
 * @param data Unused.
 * @return Always `G_SOURCE_CONTINUE`.
 */
gboolean test_controller_on_step(gpointer data) {
	(void) data;
	guint button_steps = G_N_ELEMENTS(test_controller_buttons) * TEST_CONTROLLER_BUTTON_STEPS;
	guint step = test_controller_step++ % (button_steps + TEST_CONTROLLER_STICK_STEPS);
	uint32_t buttons = 0;
	uint8_t analog_x = 128, analog_y = 128;

	if (step < button_steps) {
		guint index = step / TEST_CONTROLLER_BUTTON_STEPS;
		const guint button_step = step % TEST_CONTROLLER_BUTTON_STEPS; // The step within the steps of the button

		if (button_step == 0)
			g_message("Pressing %s", test_controller_buttons[index].name);
		if (button_step < TEST_CONTROLLER_BUTTON_STEPS - 2)
			buttons = test_controller_buttons[index].button;
	} else {
		// Around the edge of the PSP stick, from the right, clockwise on the screen
		double angle = 2 * G_PI * (step - button_steps) / (TEST_CONTROLLER_STICK_STEPS / 2);

		if (step == button_steps)
			g_message("Turning the analog stick around");
		analog_x = (uint8_t) lround(127.5 + 127.5 * cos(angle));
		analog_y = (uint8_t) lround(127.5 + 127.5 * sin(angle));
	}

	pspdisp_controller_update(test_controller, buttons, analog_x, analog_y);
	return G_SOURCE_CONTINUE;
}

/**
 * @brief Quit the main loop, called on Ctrl+C (SIGINT) or SIGTERM.
 *
 * @param loop The main loop.
 * @return Always `G_SOURCE_CONTINUE`, the main loop ends anyway.
 */
gboolean test_controller_on_signal(GMainLoop *loop) {
	g_message("Quitting...");
	g_main_loop_quit(loop);
	return G_SOURCE_CONTINUE;
}

/**
 * @brief Press each PSP button in turn on the virtual gamepad, then turn the analog stick around, until Ctrl+C.
 *
 * @return `EXIT_SUCCESS` once it stopped, `EXIT_FAILURE` if it couldn't start.
 */
int main() {
	GError *error = NULL;
	test_controller = pspdisp_controller_new(&error);

	if (test_controller == NULL) {
		g_printerr("Failed to plug the virtual gamepad with %s: %s\n", PSPDISP_GAMEPAD_NAME, error->message);
		g_error_free(error);
		return EXIT_FAILURE;
	}

	g_message("Plugged \"%s\" with %s, playing the PSP controls (Ctrl+C to quit)", PSPDISP_CONTROLLER_NAME, PSPDISP_GAMEPAD_NAME);

	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	g_unix_signal_add(SIGINT, G_SOURCE_FUNC(test_controller_on_signal), loop);
	g_unix_signal_add(SIGTERM, G_SOURCE_FUNC(test_controller_on_signal), loop);
	g_timeout_add(TEST_CONTROLLER_STEP, test_controller_on_step, NULL);
	g_main_loop_run(loop);

	pspdisp_controller_destroy(test_controller); // Unplugs it
	g_main_loop_unref(loop);
	return EXIT_SUCCESS;
}
