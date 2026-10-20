/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * sendinput.c - Keyboard and mouse (Windows)        *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * Sends the keys and the mouse with `SendInput()`, as the old desktop app did: no device is plugged, the events go in the input of the session as the ones of a real keyboard and mouse.
 *
 * The keys are sent as scan codes, which Windows takes as they are, with the 0xE0 of the extended keys as `KEYEVENTF_EXTENDEDKEY`.
 * Windows doesn't repeat the keys it's sent, so a key held is pressed once, unless its button is typematic (pressed again with each frame, as a keyboard repeats a key).
 * A character is sent as Unicode (`KEYEVENTF_UNICODE`), which Windows types whatever the keyboard layout, but the apps that read the keys themselves (e.g. games with raw input) don't get it.
 *
 * The events don't reach the windows of an elevated app unless the daemon is elevated too, nor the secure desktop (UAC prompts, the lock screen).
 * Untested: it was only compiled with MinGW.
 */

#pragma once

#include <windows.h>
#include "../input.c" // Its declarations (when `input.c` includes this file, it already has them and this include is skipped)

#define PSPDISP_INPUT_NAME "SendInput" // User friendly name of this virtual keyboard and mouse

#define PSPDISP_INPUT_SENDINPUT_EXTENDED 0xE000 // The 0xE0 before the scan code of an extended key
#define PSPDISP_INPUT_SENDINPUT_KEY_COUNT 0x100 // The scan codes of the keys: below 0x80 (the releases of the keys are above), without and with 0xE0

/**
 * @brief Virtual keyboard and mouse using SendInput, created by `pspdisp_input_new()`.
 */
struct PSPdispInput {
	uint8_t pressed_keys[PSPDISP_INPUT_SENDINPUT_KEY_COUNT / 8]; // The keys pressed (one bit per key, see `pspdisp_input_sendinput_index()`), released when destroyed
	uint8_t pressed_buttons; // The mouse buttons pressed (one bit per `PSPdispInputMouseButton`), released when destroyed
};

/**
 * @brief The flags of the mouse buttons pressed and released, by `PSPdispInputMouseButton`.
 */
const struct {
	DWORD down; // The flag of the button pressed
	DWORD up; // The flag of the button released
} pspdisp_input_sendinput_buttons[] = {
	[PSPDISP_INPUT_MOUSE_LEFT] = { MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP },
	[PSPDISP_INPUT_MOUSE_RIGHT] = { MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP },
	[PSPDISP_INPUT_MOUSE_MIDDLE] = { MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP },
};

/**
 * @brief Tell if a scan code is a key of the virtual keyboard: the keyboard layout has a virtual key for it.
 *
 * @param scancode The scan code (e.g. 0x1E, or 0xE048 for an extended key).
 * @return `true` if it can be pressed.
 */
bool pspdisp_input_has_key(uint16_t scancode) {
	const uint16_t prefix = scancode & 0xFF00;
	const uint8_t code = scancode & 0xFF;
	if ((prefix != 0 && prefix != PSPDISP_INPUT_SENDINPUT_EXTENDED) || code == 0 || code >= 0x80) return false;

	return MapVirtualKeyW(scancode, MAPVK_VSC_TO_VK_EX) != 0;
}

/**
 * @brief Get the index of a key in the bits of the pressed keys.
 *
 * @param scancode The scan code of the key.
 * @return The index, below `PSPDISP_INPUT_SENDINPUT_KEY_COUNT`.
 */
size_t pspdisp_input_sendinput_index(uint16_t scancode) {
	return ((scancode & PSPDISP_INPUT_SENDINPUT_EXTENDED) ? 0x80 : 0) | (scancode & 0x7F);
}

/**
 * @brief Get the scan code of a key pressed, from its hardware key code, which is its virtual key on Windows.
 *
 * @param keycode The hardware key code given by the toolkit.
 * @return The scan code, or 0 if it has none.
 */
uint16_t pspdisp_input_scancode_of_key(uint32_t keycode) {
	if (keycode == 0 || keycode > 0xFF) return 0;

	// With 0xE0 for an extended key, and the left key for a virtual key of both sides (e.g. `VK_SHIFT`)
	const uint16_t scancode = (uint16_t) MapVirtualKeyW(keycode, MAPVK_VK_TO_VSC_EX);
	return pspdisp_input_has_key(scancode) ? scancode : 0;
}

/**
 * @brief Send input events, which Windows takes together, without other input between them.
 *
 * @param events The events.
 * @param count The number of events.
 * @return `true` if they were sent.
 */
bool pspdisp_input_sendinput_send(INPUT *events, UINT count) {
	if (SendInput(count, events, sizeof(INPUT)) != count) {
		g_warning("%s(): failed to send input events (error %lu)", __FUNCTION__, GetLastError());
		return false;
	}

	return true;
}

/**
 * @brief Make the event of a key pressed or released.
 *
 * @param scancode The scan code of the key.
 * @param pressed If it's pressed (`true`) or released (`false`).
 * @return The event.
 */
INPUT pspdisp_input_sendinput_key_event(uint16_t scancode, bool pressed) {
	DWORD flags = KEYEVENTF_SCANCODE;
	if (scancode & PSPDISP_INPUT_SENDINPUT_EXTENDED)
		flags |= KEYEVENTF_EXTENDEDKEY;
	if (!pressed)
		flags |= KEYEVENTF_KEYUP;

	return (INPUT) { .type = INPUT_KEYBOARD, .ki = { .wScan = scancode & 0xFF, .dwFlags = flags } };
}

/**
 * @brief Make a virtual keyboard and a virtual mouse, which `SendInput()` needs no device for.
 *
 * @param name Unused, Windows shows no device for them.
 * @param error Unused, it can't fail.
 * @return The devices.
 */
PSPdispInput *pspdisp_input_new(const char *name, GError **error) {
	(void) name;
	(void) error;
	return g_new0(PSPdispInput, 1);
}

/**
 * @brief Release what's still pressed, then free the virtual keyboard and mouse.
 *
 * @param input The devices to destroy.
 */
void pspdisp_input_destroy(PSPdispInput *input) {
	for (size_t i = 0; i < PSPDISP_INPUT_SENDINPUT_KEY_COUNT; i++) {
		if (input->pressed_keys[i / 8] & (1 << (i % 8)))
			pspdisp_input_key(input, ((i & 0x80) ? PSPDISP_INPUT_SENDINPUT_EXTENDED : 0) | (i & 0x7F), false);
	}
	for (size_t i = 0; i < G_N_ELEMENTS(pspdisp_input_sendinput_buttons); i++) {
		if (input->pressed_buttons & (1 << i))
			pspdisp_input_mouse_button(input, i, false);
	}

	g_free(input);
}

/**
 * @brief Press or release a key of the virtual keyboard.
 *
 * A key pressed again (e.g. by a typematic button) is sent again, as a keyboard repeats a key held.
 *
 * @param input The devices.
 * @param scancode The scan code of the key.
 * @param pressed If it's pressed (`true`) or released (`false`).
 * @return `true` if the computer took it.
 */
bool pspdisp_input_key(PSPdispInput *input, uint16_t scancode, bool pressed) {
	if (!pspdisp_input_has_key(scancode)) return false;

	const size_t index = pspdisp_input_sendinput_index(scancode);
	uint8_t *byte = &input->pressed_keys[index / 8]; // The byte of the key, in the bits of the pressed keys
	const uint8_t bit = 1 << (index % 8);

	if (!pressed && !(*byte & bit)) return true;

	if (pressed)
		*byte |= bit;
	else
		*byte &= ~bit;

	INPUT event = pspdisp_input_sendinput_key_event(scancode, pressed);
	return pspdisp_input_sendinput_send(&event, 1);
}

/**
 * @brief Type a character, sent as Unicode, which Windows types whatever the keyboard layout.
 *
 * @param input Unused, the devices.
 * @param character The character, a Unicode code point.
 * @return `true` if the computer took it.
 */
bool pspdisp_input_type(PSPdispInput *input, uint32_t character) {
	(void) input;
	if (!g_unichar_validate(character)) return false;

	// Its UTF-16 code units: a surrogate pair beyond 0xFFFF
	WCHAR units[2] = { (WCHAR) character };
	size_t count = 1;

	if (character > 0xFFFF) {
		const uint32_t offset = character - 0x10000;
		units[0] = 0xD800 + (offset >> 10);
		units[1] = 0xDC00 + (offset & 0x3FF);
		count = 2;
	}

	// Each code unit pressed and released
	INPUT events[4];
	for (size_t i = 0; i < count; i++) {
		events[i * 2] = (INPUT) { .type = INPUT_KEYBOARD, .ki = { .wScan = units[i], .dwFlags = KEYEVENTF_UNICODE } };
		events[i * 2 + 1] = (INPUT) { .type = INPUT_KEYBOARD, .ki = { .wScan = units[i], .dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP } };
	}

	return pspdisp_input_sendinput_send(events, count * 2);
}

/**
 * @brief Press or release a button of the virtual mouse.
 *
 * @param input The devices.
 * @param button The button.
 * @param pressed If it's pressed (`true`) or released (`false`).
 * @return `true` if the computer took it.
 */
bool pspdisp_input_mouse_button(PSPdispInput *input, PSPdispInputMouseButton button, bool pressed) {
	if (pressed)
		input->pressed_buttons |= 1 << button;
	else
		input->pressed_buttons &= ~(1 << button);

	INPUT event = { .type = INPUT_MOUSE, .mi = { .dwFlags = pressed ? pspdisp_input_sendinput_buttons[button].down : pspdisp_input_sendinput_buttons[button].up } };
	return pspdisp_input_sendinput_send(&event, 1);
}

/**
 * @brief Move the virtual mouse.
 *
 * @param input Unused, the devices.
 * @param x Pixels to the right (negative to the left).
 * @param y Pixels down (negative up).
 * @return `true` if the computer took it.
 */
bool pspdisp_input_mouse_move(PSPdispInput *input, int32_t x, int32_t y) {
	(void) input;

	// Relative, so the pointer speed and acceleration of Windows apply, as to a real mouse
	INPUT event = { .type = INPUT_MOUSE, .mi = { .dx = x, .dy = y, .dwFlags = MOUSEEVENTF_MOVE } };
	return (x == 0 && y == 0) || pspdisp_input_sendinput_send(&event, 1);
}

/**
 * @brief Turn the wheel of the virtual mouse.
 *
 * @param input Unused, the devices.
 * @param steps Steps away from the user, scrolling up (negative towards the user, scrolling down).
 * @return `true` if the computer took it.
 */
bool pspdisp_input_mouse_wheel(PSPdispInput *input, int32_t steps) {
	(void) input;

	// 120 (`WHEEL_DELTA`) per step, as a real wheel
	INPUT event = { .type = INPUT_MOUSE, .mi = { .mouseData = (DWORD) (steps * WHEEL_DELTA), .dwFlags = MOUSEEVENTF_WHEEL } };
	return steps == 0 || pspdisp_input_sendinput_send(&event, 1);
}
