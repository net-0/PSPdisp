/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * ds4.c - DualShock 4 of the virtual gamepad        *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * The DualShock 4 (the PlayStation 4 controller) plugged by USB that the virtual gamepad is: its identity, its report descriptor, its state and its reports.
 * The programs know it as a real one (e.g. SDL and Steam), with nothing to configure, and show the symbols of the PSP buttons.
 *
 * The gamepad of each platform plugs it into the computer (see `gamepad.c`), and the driver of the gamepad includes this file too, as it builds the reports itself (see `vhid/driver.c`).
 * So it's plain C17, without GLib and without `shared.h`, because MSVC builds that driver and supports neither C23 enums nor binary literals, nor the GCC pragmas of `shared.h`.
 */

#pragma once

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PSPDISP_DS4_VENDOR_ID  0x054C // Sony
#define PSPDISP_DS4_PRODUCT_ID 0x05C4 // DualShock 4 (the first one)

#define PSPDISP_DS4_INPUT_REPORT_ID         0x01 // Input report, the state of the gamepad
#define PSPDISP_DS4_INPUT_REPORT_SIZE       64   // Size of the input report by USB, with its ID
#define PSPDISP_DS4_OUTPUT_REPORT_ID        0x05 // Output report, the rumble and the light bar (ignored)
#define PSPDISP_DS4_OUTPUT_REPORT_SIZE      32   // Size of the output report, with its ID
#define PSPDISP_DS4_CONTROL_REPORT_ID       0x50 // Output report of PSPdisp, which no DualShock 4 has: the state of the gamepad (see `PSPdispDS4ControlReport`)
#define PSPDISP_DS4_CONTROL_REPORT_SIZE     PSPDISP_DS4_OUTPUT_REPORT_SIZE // Size of the control report, with its ID: the one of the output report of the DualShock 4, as the system may pad every output report to the longest one
#define PSPDISP_DS4_CALIBRATION_REPORT_ID   0x02 // Feature report of the calibration of the motion sensors
#define PSPDISP_DS4_CALIBRATION_REPORT_SIZE 37   // Size of the calibration report by USB, with its ID
#define PSPDISP_DS4_PAIRING_REPORT_ID       0x12 // Feature report of the MAC addresses of the gamepad and its host, only answered by USB, by which the drivers and SDL know the gamepad and its connection
#define PSPDISP_DS4_PAIRING_REPORT_SIZE     16   // Size of the pairing report, with its ID
#define PSPDISP_DS4_MAC_REPORT_ID           0x81 // Feature report of the MAC address of the gamepad (asked by `hid-sony`)
#define PSPDISP_DS4_MAC_REPORT_SIZE         7    // Size of the MAC address report, with its ID
#define PSPDISP_DS4_FIRMWARE_REPORT_ID      0xA3 // Feature report of the versions of the hardware and the firmware (asked by `hid-playstation`, all 0 here)
#define PSPDISP_DS4_FIRMWARE_REPORT_SIZE    49   // Size of the firmware report, with its ID
#define PSPDISP_DS4_FEATURE_REPORT_MAX_SIZE PSPDISP_DS4_FIRMWARE_REPORT_SIZE // Size of the longest feature report

#define PSPDISP_DS4_MAC_ADDRESS_SIZE 6 // Size of a MAC address

#define PSPDISP_DS4_STICK_CENTERED  0x80     // Position of a centered stick, on each axis
#define PSPDISP_DS4_HAT_CENTERED    8        // Position of the hat when the directional pad is released, out of the 0-7 range (a null state)
#define PSPDISP_DS4_BATTERY_PLUGGED (1 << 4) // Status of the battery: plugged, with its level in the low 4 bits
#define PSPDISP_DS4_BATTERY_FULL    11       // Level of a full battery, while plugged
#define PSPDISP_DS4_TOUCH_INACTIVE  (1 << 7) // No finger on the touchpad, in the contact byte of a touch point

/**
 * @brief Buttons of the DualShock 4, in the order of its HID buttons 1 to 14.
 */
#define PSPDISP_DS4_BUTTON_SQUARE   (1 << 0)  // Left face button
#define PSPDISP_DS4_BUTTON_CROSS    (1 << 1)  // Bottom face button
#define PSPDISP_DS4_BUTTON_CIRCLE   (1 << 2)  // Right face button
#define PSPDISP_DS4_BUTTON_TRIANGLE (1 << 3)  // Top face button
#define PSPDISP_DS4_BUTTON_L1       (1 << 4)  // Left shoulder button
#define PSPDISP_DS4_BUTTON_R1       (1 << 5)  // Right shoulder button
#define PSPDISP_DS4_BUTTON_L2       (1 << 6)  // Left trigger, pressed
#define PSPDISP_DS4_BUTTON_R2       (1 << 7)  // Right trigger, pressed
#define PSPDISP_DS4_BUTTON_SHARE    (1 << 8)  // Share
#define PSPDISP_DS4_BUTTON_OPTIONS  (1 << 9)  // Options
#define PSPDISP_DS4_BUTTON_L3       (1 << 10) // Left stick button
#define PSPDISP_DS4_BUTTON_R3       (1 << 11) // Right stick button
#define PSPDISP_DS4_BUTTON_PS       (1 << 12) // PS button
#define PSPDISP_DS4_BUTTON_TOUCHPAD (1 << 13) // Touchpad click

#pragma pack(push, 1)

/**
 * @brief State of the DualShock 4, what its input report has that the programs read, without padding.
 */
typedef struct {
	uint8_t lx;       // Left stick, from the left (0) to the right (255), `PSPDISP_DS4_STICK_CENTERED` when centered
	uint8_t ly;       // Left stick, from the top (0) to the bottom (255)
	uint8_t rx;       // Right stick, from the left (0) to the right (255)
	uint8_t ry;       // Right stick, from the top (0) to the bottom (255)
	uint8_t l2;       // Left trigger, from released (0) to pressed (255)
	uint8_t r2;       // Right trigger, from released (0) to pressed (255)
	uint8_t hat;      // Directional pad, 0 (up) to 7 clockwise by 45 degrees, `PSPDISP_DS4_HAT_CENTERED` when released
	uint16_t buttons; // The pressed buttons, `PSPDISP_DS4_BUTTON_*` bits
} PSPdispDS4State;

/**
 * @brief The control report (`PSPDISP_DS4_CONTROL_REPORT_ID`), by which a driver of the gamepad takes its state from the daemon, as long as the output report of the DualShock 4.
 */
typedef struct {
	uint8_t report_id;     // `PSPDISP_DS4_CONTROL_REPORT_ID`
	PSPdispDS4State state; // The state of the gamepad
	uint8_t reserved[PSPDISP_DS4_CONTROL_REPORT_SIZE - 1 - sizeof(PSPdispDS4State)]; // 0
} PSPdispDS4ControlReport;

#pragma pack(pop)

static_assert(sizeof(PSPdispDS4ControlReport) == PSPDISP_DS4_CONTROL_REPORT_SIZE, "The control report must have the size of the output reports");

/**
 * @brief The state of the DualShock 4 at rest: its sticks centered, and nothing pressed.
 */
const PSPdispDS4State pspdisp_ds4_rest_state = {
	.lx = PSPDISP_DS4_STICK_CENTERED,
	.ly = PSPDISP_DS4_STICK_CENTERED,
	.rx = PSPDISP_DS4_STICK_CENTERED,
	.ry = PSPDISP_DS4_STICK_CENTERED,
	.hat = PSPDISP_DS4_HAT_CENTERED,
};

/**
 * @brief Report descriptor: the one of the DualShock 4, with only the feature reports the programs ask for, and the control report of PSPdisp.
 */
const uint8_t pspdisp_ds4_report_descriptor[] = {
	0x05, 0x01,             // Usage Page (Generic Desktop)
	0x09, 0x05,             // Usage (Game Pad)
	0xA1, 0x01,             // Collection (Application)

	// The input report
	0x85, PSPDISP_DS4_INPUT_REPORT_ID, // Report ID
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
	0x85, PSPDISP_DS4_OUTPUT_REPORT_ID, // Report ID
	0x09, 0x22,             //   Usage (0x22)
	0x95, PSPDISP_DS4_OUTPUT_REPORT_SIZE - 1, // Report Count
	0x91, 0x02,             //   Output (Data, Variable, Absolute)

	// The feature reports the programs ask for
	0x85, PSPDISP_DS4_CALIBRATION_REPORT_ID, // Report ID
	0x09, 0x24,             //   Usage (0x24)
	0x95, PSPDISP_DS4_CALIBRATION_REPORT_SIZE - 1, // Report Count
	0xB1, 0x02,             //   Feature (Data, Variable, Absolute)
	0x85, PSPDISP_DS4_PAIRING_REPORT_ID, // Report ID
	0x09, 0x25,             //   Usage (0x25)
	0x95, PSPDISP_DS4_PAIRING_REPORT_SIZE - 1, // Report Count
	0xB1, 0x02,             //   Feature (Data, Variable, Absolute)
	0x85, PSPDISP_DS4_MAC_REPORT_ID, // Report ID
	0x09, 0x26,             //   Usage (0x26)
	0x95, PSPDISP_DS4_MAC_REPORT_SIZE - 1, // Report Count
	0xB1, 0x02,             //   Feature (Data, Variable, Absolute)

	// The control report of PSPdisp, the state of the gamepad
	0x85, PSPDISP_DS4_CONTROL_REPORT_ID, // Report ID
	0x09, 0x50,             //   Usage (0x50)
	0x95, PSPDISP_DS4_CONTROL_REPORT_SIZE - 1, // Report Count
	0x91, 0x02,             //   Output (Data, Variable, Absolute)
	0xC0,                   // End Collection
};

/**
 * @brief The calibration of the motion sensors, the ranges of a real DualShock 4, so the programs don't divide by 0 (the sensors stay at 0).
 *
 * In the order of the calibration report: the gyroscope biases (pitch, yaw, roll), its ranges (plus and minus of each), its speeds (plus and minus), and the accelerometer ranges (plus and minus of X, Y and Z).
 */
const int16_t pspdisp_ds4_calibration[] = {
	0, 0, 0,
	8192, -8192, 8192, -8192, 8192, -8192,
	540, 540,
	8192, -8192, 8192, -8192, 8192, -8192,
};

/**
 * @brief MAC address of the gamepad, least significant byte first as in the feature reports: 02:50:53:50:44:53, locally administered ("\x02PSPDS").
 *
 * The gamepads plugged at once each need their own, the next ones count up from it.
 */
const uint8_t pspdisp_ds4_mac_address[PSPDISP_DS4_MAC_ADDRESS_SIZE] = { 0x53, 0x44, 0x50, 0x53, 0x50, 0x02 };

/**
 * @brief Get the position of the hat from the directions pressed on the directional pad.
 *
 * @param x The direction pressed on the horizontal axis: -1 (left), 1 (right), or 0 (none, or both).
 * @param y The direction pressed on the vertical axis: -1 (up), 1 (down), or 0 (none, or both).
 * @return The position of the hat, 0 (up) to 7 clockwise by 45 degrees, or `PSPDISP_DS4_HAT_CENTERED` when released.
 */
uint8_t pspdisp_ds4_hat(int x, int y) {
	// The positions by (y + 1) * 3 + (x + 1), from the top left to the bottom right
	const uint8_t positions[9] = { 7, 0, 1, 6, PSPDISP_DS4_HAT_CENTERED, 2, 5, 4, 3 };
	return positions[(y + 1) * 3 + (x + 1)];
}

/**
 * @brief Build the input report of the DualShock 4 for a state, and count it.
 *
 * @param state The state.
 * @param counter The counter of the input reports of the gamepad, 6 bits, counted up.
 * @param microseconds The time since the gamepad was plugged, for the timestamp of the report.
 * @param report Where to write the report, `PSPDISP_DS4_INPUT_REPORT_SIZE` bytes.
 */
void pspdisp_ds4_input_report(const PSPdispDS4State *state, uint8_t *counter, uint64_t microseconds, uint8_t *report) {
	// The timestamp, in units of 16/3 microseconds, wrapping on 16 bits
	const uint16_t timestamp = (uint16_t) (microseconds * 3 / 16);

	memset(report, 0, PSPDISP_DS4_INPUT_REPORT_SIZE); // The motion sensors and the rest at 0
	report[0] = PSPDISP_DS4_INPUT_REPORT_ID;
	report[1] = state->lx;
	report[2] = state->ly;
	report[3] = state->rx;
	report[4] = state->ry;
	report[5] = (uint8_t) ((state->hat & 0x0F) | (state->buttons & 0x0F) << 4); // The hat, then square, cross, circle and triangle
	report[6] = (uint8_t) (state->buttons >> 4); // L1, R1, L2, R2, share, options, L3 and R3
	report[7] = (uint8_t) ((state->buttons >> 12 & 0x03) | *counter << 2); // PS and the touchpad click, then the counter
	report[8] = state->l2;
	report[9] = state->r2;
	report[10] = (uint8_t) timestamp;
	report[11] = (uint8_t) (timestamp >> 8);
	report[30] = PSPDISP_DS4_BATTERY_PLUGGED | PSPDISP_DS4_BATTERY_FULL;
	report[33] = 1; // One packet of the touchpad
	report[35] = PSPDISP_DS4_TOUCH_INACTIVE; // No first finger
	report[39] = PSPDISP_DS4_TOUCH_INACTIVE; // No second finger

	*counter = (uint8_t) ((*counter + 1) & 0x3F);
}

/**
 * @brief Build a feature report of the DualShock 4, with fixed data.
 *
 * @param mac_address The MAC address of the gamepad, least significant byte first.
 * @param id The ID of the report.
 * @param report Where to write the report, `PSPDISP_DS4_FEATURE_REPORT_MAX_SIZE` bytes.
 * @return The size of the report in bytes, with its ID, or 0 if the DualShock 4 has no such feature report.
 */
size_t pspdisp_ds4_feature_report(const uint8_t mac_address[PSPDISP_DS4_MAC_ADDRESS_SIZE], uint8_t id, uint8_t *report) {
	memset(report, 0, PSPDISP_DS4_FEATURE_REPORT_MAX_SIZE);
	report[0] = id;

	switch (id) {
		case PSPDISP_DS4_CALIBRATION_REPORT_ID:
			// Little-endian, then 2 reserved bytes
			for (size_t i = 0; i < sizeof(pspdisp_ds4_calibration) / sizeof(pspdisp_ds4_calibration[0]); i++) {
				report[1 + i * 2] = (uint8_t) pspdisp_ds4_calibration[i];
				report[2 + i * 2] = (uint8_t) ((uint16_t) pspdisp_ds4_calibration[i] >> 8);
			}
			return PSPDISP_DS4_CALIBRATION_REPORT_SIZE;
		case PSPDISP_DS4_PAIRING_REPORT_ID:
			// Then 3 unknown bytes, and the MAC address of the host, none
			memcpy(&report[1], mac_address, PSPDISP_DS4_MAC_ADDRESS_SIZE);
			report[7] = 0x08;
			report[8] = 0x25;
			return PSPDISP_DS4_PAIRING_REPORT_SIZE;
		case PSPDISP_DS4_MAC_REPORT_ID:
			memcpy(&report[1], mac_address, PSPDISP_DS4_MAC_ADDRESS_SIZE);
			return PSPDISP_DS4_MAC_REPORT_SIZE;
		case PSPDISP_DS4_FIRMWARE_REPORT_ID:
			return PSPDISP_DS4_FIRMWARE_REPORT_SIZE;
		default:
			return 0;
	}
}
