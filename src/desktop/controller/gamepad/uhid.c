/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * uhid.c - Virtual gamepad using uhid (Linux)       *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * Plugs the DualShock 4 of the virtual gamepad (see `ds4.c`) through uhid (`/dev/uhid`), with its identity, its report descriptor and its reports, so the kernel drives it with its own driver of the DualShock 4 (`hid-playstation`, or `hid-sony` before Linux 6.2).
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
#include "./ds4.c"
#include "../gamepad.c" // Its declarations (when `gamepad.c` includes this file, it already has them and this include is skipped)

#define PSPDISP_GAMEPAD_NAME "uhid" // User friendly name of this virtual gamepad

#define PSPDISP_GAMEPAD_UHID_VERSION 0x0111 // Version of the device, as a DualShock 4 by USB has it: SDL picks the order of the buttons by it when it reads the gamepad of the driver (which adds 0x8000)

#define PSPDISP_GAMEPAD_UHID_MAC_COUNT 32 // Gamepads this daemon can plug at once, each with its own MAC address

/**
 * @brief Virtual gamepad using uhid, created by `pspdisp_gamepad_new()`.
 */
struct PSPdispGamepad {
	int fd; // The uhid device, its gamepad exists until it's destroyed
	unsigned mac_index; // The index of its MAC address among the ones of this daemon (see `pspdisp_gamepad_uhid_used_macs`)
	uint8_t mac_address[PSPDISP_DS4_MAC_ADDRESS_SIZE]; // Its MAC address, least significant byte first as in the feature reports
	int64_t start; // When it was created (`g_get_monotonic_time()`), the origin of the timestamps of the input reports
	uint8_t counter; // The counter of the input reports, 6 bits
	GMainContext *context; // The context of the thread that answers the requests of the kernel
	GMainLoop *loop; // The event loop of that thread
	GCancellable *thread_cancellable; // Cancelled to quit that thread
	GThread *thread; // That thread
};

GMutex pspdisp_gamepad_uhid_used_macs_lock; // Guards `pspdisp_gamepad_uhid_used_macs`
uint32_t pspdisp_gamepad_uhid_used_macs; // The MAC addresses of the gamepads this daemon has plugged, a bit per index

/**
 * @brief Take a MAC address no other gamepad of this daemon has, as the kernel refuses a DualShock 4 with the MAC address of another one.
 *
 * The first one is the one of the DualShock 4 (`pspdisp_ds4_mac_address`), the next ones count up from it.
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

	memcpy(gamepad->mac_address, pspdisp_ds4_mac_address, sizeof(gamepad->mac_address));
	gamepad->mac_address[0] += (uint8_t) index; // The least significant byte
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
 * The feature reports asked are answered (see `pspdisp_ds4_feature_report()`), the reports set are refused, and the output reports (the rumble and the light bar) are ignored.
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
				reply.u.get_report_reply.size = (uint16_t) pspdisp_ds4_feature_report(gamepad->mac_address, event.u.get_report.rnum, reply.u.get_report_reply.data);
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
	create.u.create2.rd_size = sizeof(pspdisp_ds4_report_descriptor);
	create.u.create2.bus = BUS_USB;
	create.u.create2.vendor = PSPDISP_DS4_VENDOR_ID;
	create.u.create2.product = PSPDISP_DS4_PRODUCT_ID;
	create.u.create2.version = PSPDISP_GAMEPAD_UHID_VERSION;
	memcpy(create.u.create2.rd_data, pspdisp_ds4_report_descriptor, sizeof(pspdisp_ds4_report_descriptor));

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
bool pspdisp_gamepad_update(PSPdispGamepad *gamepad, const PSPdispDS4State *state) {
	struct uhid_event input = { .type = UHID_INPUT2 };
	input.u.input2.size = PSPDISP_DS4_INPUT_REPORT_SIZE;
	pspdisp_ds4_input_report(state, &gamepad->counter, (uint64_t) (g_get_monotonic_time() - gamepad->start), input.u.input2.data);

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
