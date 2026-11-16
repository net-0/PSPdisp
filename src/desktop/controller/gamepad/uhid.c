/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * uhid.c - Virtual gamepad using uhid (Linux)       *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * Plugs the virtual gamepad through uhid (`/dev/uhid`) as a DualShock 4 (the PlayStation 4 controller) plugged by USB, as the Windows driver does (see `vhid/driver.c`).
 * It has the same identity, the same report descriptor and the same reports, so the kernel drives it with its own driver of the DualShock 4 (`hid-playstation`, or `hid-sony` before Linux 6.2).
 * That driver makes the gamepad, its touchpad and its motion sensors (which stay at rest), and a hidraw device, so the programs know it as a real one, with nothing to configure (e.g. SDL and Steam).
 *
 * The driver asks the gamepad for feature reports when it's plugged (its MAC address, its firmware and its calibration), and the programs may at any time: a thread of the gamepad answers them.
 * The input report is sent at each update, changed or not, as a DualShock 4 sends its state all the time.
 * The driver drops the ones sent before it's ready, so the gamepad leaves the state it starts with (both sticks up and left) at the next update.
 *
 * Writing to `/dev/uhid` needs root, or the udev rule of the daemon (see `daemon/60-pspdisp.rules`).
 */

#pragma once

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <linux/uhid.h>
#include <glib-unix.h>
#include <gio/gio.h>
#include "../gamepad.c" // Its declarations (when `gamepad.c` includes this file, it already has them and this include is skipped)

#define PSPDISP_GAMEPAD_NAME "uhid" // User friendly name of this virtual gamepad

#define PSPDISP_GAMEPAD_UHID_VENDOR_ID  0x054C // Sony, as a DualShock 4
#define PSPDISP_GAMEPAD_UHID_PRODUCT_ID 0x05C4 // DualShock 4 (the first one)
#define PSPDISP_GAMEPAD_UHID_VERSION    0x0111 // Version of the device, as a DualShock 4 by USB has it: SDL picks the order of the buttons by it when it reads the gamepad of the driver (which adds 0x8000)

#define PSPDISP_GAMEPAD_UHID_INPUT_REPORT_ID         0x01 // Input report, the state of the gamepad
#define PSPDISP_GAMEPAD_UHID_INPUT_REPORT_SIZE       64   // Size of the input report by USB, with its ID
#define PSPDISP_GAMEPAD_UHID_OUTPUT_REPORT_ID        0x05 // Output report, the rumble and the light bar (ignored)
#define PSPDISP_GAMEPAD_UHID_OUTPUT_REPORT_SIZE      32   // Size of the output report, with its ID
#define PSPDISP_GAMEPAD_UHID_CALIBRATION_REPORT_ID   0x02 // Feature report of the calibration of the motion sensors
#define PSPDISP_GAMEPAD_UHID_CALIBRATION_REPORT_SIZE 37	  // Size of the calibration report by USB, with its ID
#define PSPDISP_GAMEPAD_UHID_PAIRING_REPORT_ID       0x12 // Feature report of the MAC addresses of the gamepad and its host, by which the driver and SDL know the gamepad
#define PSPDISP_GAMEPAD_UHID_PAIRING_REPORT_SIZE     16   // Size of the pairing report, with its ID
#define PSPDISP_GAMEPAD_UHID_MAC_REPORT_ID           0x81 // Feature report of the MAC address of the gamepad (asked by `hid-sony`)
#define PSPDISP_GAMEPAD_UHID_MAC_REPORT_SIZE         7    // Size of the MAC address report, with its ID
#define PSPDISP_GAMEPAD_UHID_FIRMWARE_REPORT_ID      0xA3 // Feature report of the versions of the hardware and the firmware (asked by `hid-playstation`, all 0 here)
#define PSPDISP_GAMEPAD_UHID_FIRMWARE_REPORT_SIZE    49   // Size of the firmware report, with its ID

#define PSPDISP_GAMEPAD_UHID_STICK_CENTERED  0x80       // Position of a centered stick, on each axis
#define PSPDISP_GAMEPAD_UHID_HAT_CENTERED    8          // Position of the hat when the directional pad is released, out of the 0-7 range (a null state)
#define PSPDISP_GAMEPAD_UHID_BATTERY_PLUGGED 0b00010000 // Status of the battery: plugged, with its level in the low 4 bits
#define PSPDISP_GAMEPAD_UHID_BATTERY_FULL    11         // Level of a full battery, while plugged
#define PSPDISP_GAMEPAD_UHID_TOUCH_INACTIVE  0b10000000 // No finger on the touchpad, in the contact byte of a touch point

#define PSPDISP_GAMEPAD_UHID_MAC_COUNT 32 // Gamepads this daemon can plug at once, each with its own MAC address

/**
 * @brief Buttons of the DualShock 4 the PSP has, in the order of its HID buttons (the others are L2, R2, L3, R3, PS and the touchpad click).
 */
#define PSPDISP_GAMEPAD_UHID_BUTTON_SQUARE   0b0000000000000001 // Left face button
#define PSPDISP_GAMEPAD_UHID_BUTTON_CROSS    0b0000000000000010 // Bottom face button
#define PSPDISP_GAMEPAD_UHID_BUTTON_CIRCLE   0b0000000000000100 // Right face button
#define PSPDISP_GAMEPAD_UHID_BUTTON_TRIANGLE 0b0000000000001000 // Top face button
#define PSPDISP_GAMEPAD_UHID_BUTTON_L1       0b0000000000010000 // Left shoulder button
#define PSPDISP_GAMEPAD_UHID_BUTTON_R1       0b0000000000100000 // Right shoulder button
#define PSPDISP_GAMEPAD_UHID_BUTTON_SHARE    0b0000000100000000 // Share
#define PSPDISP_GAMEPAD_UHID_BUTTON_OPTIONS  0b0000001000000000 // Options

/**
 * @brief Virtual gamepad using uhid, created by `pspdisp_gamepad_new()`.
 */
struct PSPdispGamepad {
	int fd; // The uhid device, its gamepad exists until it's destroyed
	unsigned mac_index; // The index of its MAC address among the ones of this daemon (see `pspdisp_gamepad_uhid_used_macs`)
	uint8_t mac_address[6]; // Its MAC address, least significant byte first as in the feature reports
	int64_t start; // When it was created (`g_get_monotonic_time()`), the origin of the timestamps of the input reports
	uint8_t counter; // The counter of the input reports, 6 bits
	GMainContext *context; // The context of the thread that answers the requests of the kernel
	GMainLoop *loop; // The event loop of that thread
	GCancellable *thread_cancellable; // Cancelled to quit that thread
	GThread *thread; // That thread
};

/**
 * @brief Report descriptor: the one of the DualShock 4, with only the feature reports the games ask for, as the Windows driver has it but without its own output report (see `vhid/driver.c`).
 */
const uint8_t pspdisp_gamepad_uhid_report_descriptor[] = {
	0x05, 0x01,             // Usage Page (Generic Desktop)
	0x09, 0x05,             // Usage (Game Pad)
	0xA1, 0x01,             // Collection (Application)

	// The input report
	0x85, PSPDISP_GAMEPAD_UHID_INPUT_REPORT_ID, // Report ID
	0x09, 0x30,             //   Usage (X): the left stick
	0x09, 0x31,             //   Usage (Y)
	0x09, 0x32,             //   Usage (Z): the right stick
	0x09, 0x35,             //   Usage (Rz)
	0x15, 0x00,             //   Logical Minimum (0)
	0x26, 0xFF, 0x00,       //   Logical Maximum (255)
	0x75, 0x08,             //   Report Size (8)
	0x95, 0x04,             //   Report Count (4)
	0x81, 0x02,             //   Input (Data, Variable, Absolute)
	0x09, 0x39,             //   Usage (Hat Switch): the directional pad
	0x15, 0x00,             //   Logical Minimum (0)
	0x25, 0x07,             //   Logical Maximum (7)
	0x35, 0x00,             //   Physical Minimum (0)
	0x46, 0x3B, 0x01,       //   Physical Maximum (315)
	0x65, 0x14,             //   Unit (Degrees)
	0x75, 0x04,             //   Report Size (4)
	0x95, 0x01,             //   Report Count (1)
	0x81, 0x42,             //   Input (Data, Variable, Absolute, Null State): out of 0-7 when released
	0x65, 0x00,             //   Unit (None)
	0x45, 0x00,             //   Physical Maximum (0)
	0x05, 0x09,             //   Usage Page (Button)
	0x19, 0x01,             //   Usage Minimum (1)
	0x29, 0x0E,             //   Usage Maximum (14)
	0x15, 0x00,             //   Logical Minimum (0)
	0x25, 0x01,             //   Logical Maximum (1)
	0x75, 0x01,             //   Report Size (1)
	0x95, 0x0E,             //   Report Count (14)
	0x81, 0x02,             //   Input (Data, Variable, Absolute)
	0x06, 0x00, 0xFF,       //   Usage Page (Vendor Defined 0xFF00)
	0x09, 0x20,             //   Usage (0x20): the counter of the reports
	0x15, 0x00,             //   Logical Minimum (0)
	0x25, 0x7F,             //   Logical Maximum (127)
	0x75, 0x06,             //   Report Size (6)
	0x95, 0x01,             //   Report Count (1)
	0x81, 0x02,             //   Input (Data, Variable, Absolute)
	0x05, 0x01,             //   Usage Page (Generic Desktop)
	0x09, 0x33,             //   Usage (Rx): the left trigger
	0x09, 0x34,             //   Usage (Ry): the right trigger
	0x15, 0x00,             //   Logical Minimum (0)
	0x26, 0xFF, 0x00,       //   Logical Maximum (255)
	0x75, 0x08,             //   Report Size (8)
	0x95, 0x02,             //   Report Count (2)
	0x81, 0x02,             //   Input (Data, Variable, Absolute)
	0x06, 0x00, 0xFF,       //   Usage Page (Vendor Defined 0xFF00)
	0x09, 0x21,             //   Usage (0x21): the rest (the timestamp, the motion sensors, the battery, the touchpad)
	0x95, 0x36,             //   Report Count (54)
	0x81, 0x02,             //   Input (Data, Variable, Absolute)

	// The output report (the rumble and the light bar)
	0x85, PSPDISP_GAMEPAD_UHID_OUTPUT_REPORT_ID, // Report ID
	0x09, 0x22,             //   Usage (0x22)
	0x95, PSPDISP_GAMEPAD_UHID_OUTPUT_REPORT_SIZE - 1, // Report Count
	0x91, 0x02,             //   Output (Data, Variable, Absolute)

	// The feature reports the games ask for
	0x85, PSPDISP_GAMEPAD_UHID_CALIBRATION_REPORT_ID, // Report ID
	0x09, 0x24,             //   Usage (0x24)
	0x95, PSPDISP_GAMEPAD_UHID_CALIBRATION_REPORT_SIZE - 1, // Report Count
	0xB1, 0x02,             //   Feature (Data, Variable, Absolute)
	0x85, PSPDISP_GAMEPAD_UHID_PAIRING_REPORT_ID, // Report ID
	0x09, 0x25,             //   Usage (0x25)
	0x95, PSPDISP_GAMEPAD_UHID_PAIRING_REPORT_SIZE - 1, // Report Count
	0xB1, 0x02,             //   Feature (Data, Variable, Absolute)
	0x85, PSPDISP_GAMEPAD_UHID_MAC_REPORT_ID, // Report ID
	0x09, 0x26,             //   Usage (0x26)
	0x95, PSPDISP_GAMEPAD_UHID_MAC_REPORT_SIZE - 1, // Report Count
	0xB1, 0x02,             //   Feature (Data, Variable, Absolute)
	0xC0,                   // End Collection
};

/**
 * @brief The calibration of the motion sensors, the ranges of a real DualShock 4, so the programs don't divide by 0 (the sensors stay at 0).
 *
 * In the order of the calibration report: the gyroscope biases (pitch, yaw, roll), its ranges (plus and minus of each), its speeds (plus and minus), and the accelerometer ranges (plus and minus of X, Y and Z).
 */
const int16_t pspdisp_gamepad_uhid_calibration[] = {
	0, 0, 0,
	8192, -8192, 8192, -8192, 8192, -8192,
	540, 540,
	8192, -8192, 8192, -8192, 8192, -8192,
};

/**
 * @brief The buttons of the gamepad, with the button of the DualShock 4 each one shows.
 */
const struct {
	PSPdispGamepadButton button; // The button of the gamepad
	uint16_t ds4_button; // The button of the DualShock 4, `PSPDISP_GAMEPAD_UHID_BUTTON_*`
} pspdisp_gamepad_uhid_buttons[] = {
	{ PSPDISP_GAMEPAD_BUTTON_SOUTH,          PSPDISP_GAMEPAD_UHID_BUTTON_CROSS },
	{ PSPDISP_GAMEPAD_BUTTON_EAST,           PSPDISP_GAMEPAD_UHID_BUTTON_CIRCLE },
	{ PSPDISP_GAMEPAD_BUTTON_WEST,           PSPDISP_GAMEPAD_UHID_BUTTON_SQUARE },
	{ PSPDISP_GAMEPAD_BUTTON_NORTH,          PSPDISP_GAMEPAD_UHID_BUTTON_TRIANGLE },
	{ PSPDISP_GAMEPAD_BUTTON_LEFT_SHOULDER,  PSPDISP_GAMEPAD_UHID_BUTTON_L1 },
	{ PSPDISP_GAMEPAD_BUTTON_RIGHT_SHOULDER, PSPDISP_GAMEPAD_UHID_BUTTON_R1 },
	{ PSPDISP_GAMEPAD_BUTTON_BACK,           PSPDISP_GAMEPAD_UHID_BUTTON_SHARE },
	{ PSPDISP_GAMEPAD_BUTTON_START,          PSPDISP_GAMEPAD_UHID_BUTTON_OPTIONS },
};

GMutex pspdisp_gamepad_uhid_used_macs_lock; // Guards `pspdisp_gamepad_uhid_used_macs`
uint32_t pspdisp_gamepad_uhid_used_macs; // The MAC addresses of the gamepads this daemon has plugged, a bit per index

/**
 * @brief Take a MAC address no other gamepad of this daemon has, as the kernel refuses a DualShock 4 with the MAC address of another one.
 *
 * The first one is the MAC address of the Windows driver, 02:50:53:50:44:53 (locally administered, "\x02PSPDS"), the next ones count up from it.
 * The gamepads of the daemons of other users may have the same ones: the kernel then refuses the second one, which shows no gamepad.
 *
 * @param gamepad The gamepad, where to write its MAC address and its index.
 * @return `true` if a MAC address was free.
 */
bool pspdisp_gamepad_uhid_take_mac(PSPdispGamepad *gamepad) {
	g_mutex_lock(&pspdisp_gamepad_uhid_used_macs_lock);
	const int index = g_bit_nth_lsf(~(gulong) pspdisp_gamepad_uhid_used_macs, -1); // The first free one
	const bool found = index >= 0 && index < PSPDISP_GAMEPAD_UHID_MAC_COUNT;
	if (found)
		pspdisp_gamepad_uhid_used_macs |= 1u << index;
	g_mutex_unlock(&pspdisp_gamepad_uhid_used_macs_lock);

	if (!found) return false;

	const uint8_t mac_address[6] = { (uint8_t) (0x53 + index), 0x44, 0x50, 0x53, 0x50, 0x02 };
	memcpy(gamepad->mac_address, mac_address, sizeof(mac_address));
	gamepad->mac_index = (unsigned) index;
	return true;
}

/**
 * @brief Give back the MAC address of a gamepad, for the next ones.
 *
 * @param gamepad The gamepad.
 */
void pspdisp_gamepad_uhid_release_mac(PSPdispGamepad *gamepad) {
	g_mutex_lock(&pspdisp_gamepad_uhid_used_macs_lock);
	pspdisp_gamepad_uhid_used_macs &= ~(1u << gamepad->mac_index);
	g_mutex_unlock(&pspdisp_gamepad_uhid_used_macs_lock);
}

/**
 * @brief Get the position of a stick on an axis of the DualShock 4 from the one of the gamepad.
 *
 * @param value The position, from -32768 to 32767, 0 when centered.
 * @return The position, from 0 to 255, `PSPDISP_GAMEPAD_UHID_STICK_CENTERED` when centered.
 */
uint8_t pspdisp_gamepad_uhid_axis(int16_t value) {
	return (uint8_t) ((value >> 8) + PSPDISP_GAMEPAD_UHID_STICK_CENTERED);
}

/**
 * @brief Get the position of the hat from the directional pad: 0 (up) to 7 clockwise by 45 degrees, or released.
 *
 * @param buttons The pressed buttons, `PSPdispGamepadButton` bits.
 * @return The position of the hat, `PSPDISP_GAMEPAD_UHID_HAT_CENTERED` when released (or both directions of an axis are pressed).
 */
uint8_t pspdisp_gamepad_uhid_hat(uint16_t buttons) {
	const int x = ((buttons & PSPDISP_GAMEPAD_BUTTON_DPAD_RIGHT) != 0) - ((buttons & PSPDISP_GAMEPAD_BUTTON_DPAD_LEFT) != 0);
	const int y = ((buttons & PSPDISP_GAMEPAD_BUTTON_DPAD_DOWN) != 0) - ((buttons & PSPDISP_GAMEPAD_BUTTON_DPAD_UP) != 0);

	// The positions by (y + 1) * 3 + (x + 1), from the top left to the bottom right
	const uint8_t positions[9] = { 7, 0, 1, 6, PSPDISP_GAMEPAD_UHID_HAT_CENTERED, 2, 5, 4, 3 };
	return positions[(y + 1) * 3 + (x + 1)];
}

/**
 * @brief Build the input report of the DualShock 4 for a state of the gamepad, and count it.
 *
 * @param gamepad The gamepad.
 * @param state The state of the gamepad.
 * @param report Where to write the report, `PSPDISP_GAMEPAD_UHID_INPUT_REPORT_SIZE` bytes.
 */
void pspdisp_gamepad_uhid_input_report(PSPdispGamepad *gamepad, const PSPdispGamepadState *state, uint8_t *report) {
	// The timestamp, in units of 16/3 microseconds, wrapping on 16 bits
	const uint16_t timestamp = (uint16_t) ((g_get_monotonic_time() - gamepad->start) * 3 / 16);
	uint16_t buttons = 0;

	for (size_t i = 0; i < G_N_ELEMENTS(pspdisp_gamepad_uhid_buttons); i++) {
		if ((state->buttons & pspdisp_gamepad_uhid_buttons[i].button) != 0)
			buttons |= pspdisp_gamepad_uhid_buttons[i].ds4_button;
	}

	memset(report, 0, PSPDISP_GAMEPAD_UHID_INPUT_REPORT_SIZE); // The triggers, the motion sensors and the rest at 0
	report[0] = PSPDISP_GAMEPAD_UHID_INPUT_REPORT_ID;
	report[1] = pspdisp_gamepad_uhid_axis(state->left_x);
	report[2] = pspdisp_gamepad_uhid_axis(state->left_y);
	report[3] = PSPDISP_GAMEPAD_UHID_STICK_CENTERED; // The right stick
	report[4] = PSPDISP_GAMEPAD_UHID_STICK_CENTERED;
	report[5] = (uint8_t) (pspdisp_gamepad_uhid_hat(state->buttons) | (buttons & 0b1111) << 4); // The hat, then square, cross, circle and triangle
	report[6] = (uint8_t) (buttons >> 4); // L1, R1, L2, R2, share, options, L3 and R3
	report[7] = (uint8_t) (gamepad->counter << 2); // PS and the touchpad click, then the counter
	report[10] = (uint8_t) timestamp;
	report[11] = (uint8_t) (timestamp >> 8);
	report[30] = PSPDISP_GAMEPAD_UHID_BATTERY_PLUGGED | PSPDISP_GAMEPAD_UHID_BATTERY_FULL;
	report[33] = 1; // One packet of the touchpad
	report[35] = PSPDISP_GAMEPAD_UHID_TOUCH_INACTIVE; // No first finger
	report[39] = PSPDISP_GAMEPAD_UHID_TOUCH_INACTIVE; // No second finger

	gamepad->counter = (gamepad->counter + 1) & 0b111111;
}

/**
 * @brief Build a feature report of the DualShock 4, with fixed data.
 *
 * @param gamepad The gamepad, for its MAC address.
 * @param id The ID of the report.
 * @param report Where to write the report, at least `PSPDISP_GAMEPAD_UHID_FIRMWARE_REPORT_SIZE` bytes (the longest one).
 * @return The size of the report in bytes, with its ID, or 0 if the DualShock 4 has no such feature report.
 */
size_t pspdisp_gamepad_uhid_feature_report(const PSPdispGamepad *gamepad, uint8_t id, uint8_t *report) {
	memset(report, 0, PSPDISP_GAMEPAD_UHID_FIRMWARE_REPORT_SIZE);
	report[0] = id;

	switch (id) {
		case PSPDISP_GAMEPAD_UHID_CALIBRATION_REPORT_ID:
			// Little-endian, then 2 reserved bytes
			for (size_t i = 0; i < G_N_ELEMENTS(pspdisp_gamepad_uhid_calibration); i++) {
				report[1 + i * 2] = (uint8_t) pspdisp_gamepad_uhid_calibration[i];
				report[2 + i * 2] = (uint8_t) ((uint16_t) pspdisp_gamepad_uhid_calibration[i] >> 8);
			}
			return PSPDISP_GAMEPAD_UHID_CALIBRATION_REPORT_SIZE;
		case PSPDISP_GAMEPAD_UHID_PAIRING_REPORT_ID:
			// Then 3 unknown bytes, and the MAC address of the host, none
			memcpy(&report[1], gamepad->mac_address, sizeof(gamepad->mac_address));
			report[7] = 0x08;
			report[8] = 0x25;
			return PSPDISP_GAMEPAD_UHID_PAIRING_REPORT_SIZE;
		case PSPDISP_GAMEPAD_UHID_MAC_REPORT_ID:
			memcpy(&report[1], gamepad->mac_address, sizeof(gamepad->mac_address));
			return PSPDISP_GAMEPAD_UHID_MAC_REPORT_SIZE;
		case PSPDISP_GAMEPAD_UHID_FIRMWARE_REPORT_ID:
			return PSPDISP_GAMEPAD_UHID_FIRMWARE_REPORT_SIZE;
		default:
			return 0;
	}
}

/**
 * @brief Write an event to the uhid device.
 *
 * @param fd The uhid device.
 * @param event The event.
 * @return `true` if the kernel took it.
 */
bool pspdisp_gamepad_uhid_write(int fd, const struct uhid_event *event) {
	return write(fd, event, sizeof(*event)) == (ssize_t) sizeof(*event);
}

/**
 * @brief Answer an event of the kernel, called on the thread of the gamepad when the uhid device has one.
 *
 * The feature reports asked are answered (see `pspdisp_gamepad_uhid_feature_report()`), the reports set are refused, and the output reports (the rumble and the light bar) are ignored.
 *
 * @param fd The uhid device.
 * @param condition What the device has, an event to read, or an error.
 * @param gamepad The gamepad.
 * @return `G_SOURCE_CONTINUE`, or `G_SOURCE_REMOVE` if it failed.
 */
gboolean pspdisp_gamepad_uhid_on_event(int fd, GIOCondition condition, PSPdispGamepad *gamepad) {
	if ((condition & (G_IO_ERR | G_IO_HUP)) != 0) return G_SOURCE_REMOVE;

	struct uhid_event event;
	if (read(fd, &event, sizeof(event)) < (ssize_t) sizeof(event.type)) return G_SOURCE_CONTINUE;

	struct uhid_event reply = {0};

	switch (event.type) {
		case UHID_GET_REPORT:
			reply.type = UHID_GET_REPORT_REPLY;
			reply.u.get_report_reply.id = event.u.get_report.id;
			if (event.u.get_report.rtype == UHID_FEATURE_REPORT)
				reply.u.get_report_reply.size = (uint16_t) pspdisp_gamepad_uhid_feature_report(gamepad, event.u.get_report.rnum, reply.u.get_report_reply.data);
			if (reply.u.get_report_reply.size == 0)
				reply.u.get_report_reply.err = EIO;
			break;
		case UHID_SET_REPORT:
			reply.type = UHID_SET_REPORT_REPLY;
			reply.u.set_report_reply.id = event.u.set_report.id;
			reply.u.set_report_reply.err = EIO;
			break;
		default:
			return G_SOURCE_CONTINUE; // E.g. the output reports, and the device started, opened, closed or stopped by the kernel
	}

	// A reply fails when the device is destroyed meanwhile, the kernel then stops waiting for it
	pspdisp_gamepad_uhid_write(fd, &reply);
	return G_SOURCE_CONTINUE;
}

/**
 * @brief Quit the event loop of the thread of the gamepad, called when `thread_cancellable` is cancelled.
 *
 * @param cancellable The cancelled `thread_cancellable`.
 * @param gamepad The gamepad that owns the thread.
 * @return Always `G_SOURCE_REMOVE`, it's only needed once.
 */
gboolean pspdisp_gamepad_uhid_thread_cancel(GCancellable *cancellable, PSPdispGamepad *gamepad) {
	(void) cancellable;
	g_main_loop_quit(gamepad->loop);
	return G_SOURCE_REMOVE;
}

/**
 * @brief Body of the thread of the gamepad: answers the events of the kernel until the gamepad is destroyed.
 *
 * @param gamepad The gamepad that owns the thread.
 * @return Always `NULL`, nothing is given to `g_thread_join()`.
 */
gpointer pspdisp_gamepad_uhid_thread_func(PSPdispGamepad *gamepad) {
	GSource *cancellable_source = g_cancellable_source_new(gamepad->thread_cancellable);
	g_source_set_callback(cancellable_source, G_SOURCE_FUNC(pspdisp_gamepad_uhid_thread_cancel), gamepad, NULL); // Called as `GCancellableSourceFunc`
	g_source_attach(cancellable_source, gamepad->context);
	g_source_unref(cancellable_source);

	GSource *event_source = g_unix_fd_source_new(gamepad->fd, G_IO_IN);
	g_source_set_callback(event_source, G_SOURCE_FUNC(pspdisp_gamepad_uhid_on_event), gamepad, NULL); // Called as `GUnixFDSourceFunc`
	g_source_attach(event_source, gamepad->context);
	g_source_unref(event_source);

	g_main_loop_run(gamepad->loop); // Blocks until quit
	return NULL;
}

/**
 * @brief Plug a virtual DualShock 4 through uhid.
 *
 * @param name The name the computer shows for it.
 * @param error Where to tell why it failed.
 * @return The gamepad, or `NULL` if it failed.
 */
PSPdispGamepad *pspdisp_gamepad_new(const char *name, GError **error) {
	int fd = open("/dev/uhid", O_RDWR | O_NONBLOCK | O_CLOEXEC);

	if (fd < 0) {
		int code = errno;

		if (code == EACCES || code == EPERM)
			g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(code), "no access to /dev/uhid, install the udev rule (sudo make install)");
		else if (code == ENOENT || code == ENODEV)
			g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(code), "/dev/uhid is missing, load the uhid kernel module (modprobe uhid)");
		else
			g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(code), "failed to open /dev/uhid: %s", g_strerror(code));
		return NULL;
	}

	PSPdispGamepad *gamepad = g_new0(PSPdispGamepad, 1);
	gamepad->fd = fd;

	if (!pspdisp_gamepad_uhid_take_mac(gamepad)) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "too many virtual gamepads plugged at once (%d)", PSPDISP_GAMEPAD_UHID_MAC_COUNT);
		close(fd);
		g_free(gamepad);
		return NULL;
	}

	struct uhid_event create = { .type = UHID_CREATE2 };
	g_strlcpy((char *) create.u.create2.name, name, sizeof(create.u.create2.name));
	create.u.create2.rd_size = sizeof(pspdisp_gamepad_uhid_report_descriptor);
	create.u.create2.bus = BUS_USB;
	create.u.create2.vendor = PSPDISP_GAMEPAD_UHID_VENDOR_ID;
	create.u.create2.product = PSPDISP_GAMEPAD_UHID_PRODUCT_ID;
	create.u.create2.version = PSPDISP_GAMEPAD_UHID_VERSION;
	memcpy(create.u.create2.rd_data, pspdisp_gamepad_uhid_report_descriptor, sizeof(pspdisp_gamepad_uhid_report_descriptor));

	if (!pspdisp_gamepad_uhid_write(fd, &create)) {
		int code = errno;
		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(code), "failed to create the gamepad with uhid: %s", g_strerror(code));
		pspdisp_gamepad_uhid_release_mac(gamepad);
		close(fd);
		g_free(gamepad);
		return NULL;
	}

	// The kernel asks for the feature reports once the device is created, they wait for the thread
	gamepad->start = g_get_monotonic_time();
	gamepad->context = g_main_context_new();
	gamepad->loop = g_main_loop_new(gamepad->context, FALSE);
	gamepad->thread_cancellable = g_cancellable_new();
	gamepad->thread = g_thread_new("pspdisp-gamepad", (GThreadFunc) pspdisp_gamepad_uhid_thread_func, gamepad); // Last, the thread uses the fields above
	return gamepad;
}

/**
 * @brief Send the state of the gamepad, as the input report of the DualShock 4, changed or not.
 *
 * @param gamepad The gamepad.
 * @param state The new state.
 * @return `true` if the kernel took it.
 */
bool pspdisp_gamepad_update(PSPdispGamepad *gamepad, const PSPdispGamepadState *state) {
	struct uhid_event input = { .type = UHID_INPUT2 };
	input.u.input2.size = PSPDISP_GAMEPAD_UHID_INPUT_REPORT_SIZE;
	pspdisp_gamepad_uhid_input_report(gamepad, state, input.u.input2.data);

	if (!pspdisp_gamepad_uhid_write(gamepad->fd, &input)) {
		g_warning("%s(): failed to send the state of the gamepad: %s", __FUNCTION__, g_strerror(errno));
		return false;
	}
	return true;
}

/**
 * @brief Unplug the virtual gamepad and free it.
 *
 * @param gamepad The gamepad to destroy.
 */
void pspdisp_gamepad_destroy(PSPdispGamepad *gamepad) {
	// The device first, so the kernel stops waiting for the replies of the thread
	pspdisp_gamepad_uhid_write(gamepad->fd, &(struct uhid_event) { .type = UHID_DESTROY });

	g_cancellable_cancel(gamepad->thread_cancellable);
	g_thread_join(gamepad->thread);
	g_object_unref(gamepad->thread_cancellable);
	g_main_loop_unref(gamepad->loop);
	g_main_context_unref(gamepad->context);

	close(gamepad->fd);
	pspdisp_gamepad_uhid_release_mac(gamepad);
	g_free(gamepad);
}
