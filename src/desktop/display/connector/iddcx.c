/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * iddcx.c - Display connector using IddCx (Windows) *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * The virtual display is made by the PSPdisp IddCx driver (`iddcx/driver.c`), which must be installed first (see `iddcx/BUILDING.md`).
 *
 * This connector only talks to the driver: it connects a monitor with the PSP EDID, then waits for its power changes and the frames it asks for (see `iddcx/protocol.h`).
 */

#pragma once

#include <windows.h>
#include <cfgmgr32.h>
#include "./iddcx/protocol.h"
#include "../connector.c" // Its declarations (when `connector.c` includes this file, it already has them and this include is skipped)

#define PSPDISP_DISPLAY_CONNECTOR_NAME "IddCx" // User friendly name of this connector

static_assert(PSPDISP_IDDCX_WIDTH == PSPDISP_DISPLAY_WIDTH && PSPDISP_IDDCX_HEIGHT == PSPDISP_DISPLAY_HEIGHT, "The driver frames must have the PSP resolution");
static_assert(PSPDISP_IDDCX_EDID_SIZE == sizeof(PSPdispEDID), "The driver must take a whole PSP EDID");
static_assert(PSPDISP_IDDCX_DPMS_ON == PSPDISP_DPMS_MODE_ON && PSPDISP_IDDCX_DPMS_OFF == PSPDISP_DPMS_MODE_OFF, "The driver sends the values of `PSPdispDPMSMode`");

/**
 * @brief Display connector using the PSPdisp IddCx driver, the source returned by `pspdisp_display_connector_connect()`.
 *
 * It keeps a wait request (`IOCTL_PSPDISP_IDDCX_WAIT`) always pending on the driver, and GLib polls the event of its overlapped I/O.
 * So it dispatches when the driver answers with a power change, or with a frame asked with `IOCTL_PSPDISP_IDDCX_REQUEST_FRAME`.
 * It asks for the next frame as soon as one arrives, without a timer: Windows only gives the driver a new frame when it draws, at most once per refresh.
 */
typedef struct {
	GSource source; // The base source, first so this struct is also a `GSource`
	PSPdispConnectorFrameHandler frame_handler; // Handle each new frame
	PSPdispDPMSHandler dpms_handler; // Handle each DPMS change
	PSPdispLostHandler lost_handler; // Handle the loss of the virtual display
	gpointer handler_data; // Given to the handlers
	HANDLE device; // The opened driver device, for overlapped I/O
	OVERLAPPED overlapped; // State of the pending wait request, its event is signaled when the driver answers
	GPollFD poll; // The event of `overlapped`, polled by GLib (on Windows a `GPollFD` holds a `HANDLE`)
	PSPdispIddCxEvent *event; // Answer of the pending wait request, written by the driver
	uint32_t *pixels; // Frame converted to `PSPDISP_PIXEL_FORMAT_8888`
} PSPdispDisplayConnector;

/**
 * @brief Open the device of the PSPdisp IddCx driver, from its device interface.
 *
 * @return The device handle, for overlapped I/O, or `INVALID_HANDLE_VALUE` if it failed.
 */
HANDLE pspdisp_display_connector_iddcx_open() {
	GUID guid = PSPDISP_IDDCX_INTERFACE_GUID;
	ULONG length = 0;

	// A list of device interface paths, each terminated by a null character, and the list by another one
	if (CM_Get_Device_Interface_List_SizeW(&length, &guid, NULL, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS || length <= 1) {
		g_warning("%s(): the PSPdisp IddCx driver is not installed (no device interface)", __FUNCTION__);
		return INVALID_HANDLE_VALUE;
	}

	WCHAR *paths = g_new0(WCHAR, length);
	HANDLE device = INVALID_HANDLE_VALUE;

	if (CM_Get_Device_Interface_ListW(&guid, NULL, paths, length, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) == CR_SUCCESS) {
		device = CreateFileW(paths, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL); // The first path, there's only one device
		if (device == INVALID_HANDLE_VALUE)
			g_warning("%s(): failed to open the PSPdisp IddCx driver (error %lu)", __FUNCTION__, GetLastError());
	}

	g_free(paths);
	return device;
}

/**
 * @brief Send a request to the driver and wait for its answer.
 *
 * @param device The driver device, opened for overlapped I/O.
 * @param code The request, an `IOCTL_PSPDISP_IDDCX_*` code.
 * @param input The input buffer (`NULL` for none).
 * @param input_size The size of the input buffer in bytes.
 * @return `true` if it succeeded, `false` if it failed.
 */
bool pspdisp_display_connector_iddcx_control(HANDLE device, DWORD code, const void *input, DWORD input_size) {
	OVERLAPPED overlapped = { .hEvent = CreateEventW(NULL, TRUE, FALSE, NULL) };
	DWORD bytes = 0;
	bool done = DeviceIoControl(device, code, (void *) input, input_size, NULL, 0, &bytes, &overlapped)
	         || (GetLastError() == ERROR_IO_PENDING && GetOverlappedResult(device, &overlapped, &bytes, TRUE));

	DWORD error = GetLastError();
	CloseHandle(overlapped.hEvent);
	SetLastError(error); // Kept for the caller, `CloseHandle()` may change it
	return done;
}

/**
 * @brief Ask the driver for its next event, without waiting: the event of `overlapped` is signaled when it answers.
 *
 * @param display_connector The connector.
 * @return `true` if the request is pending (or already answered), `false` if it failed.
 */
bool pspdisp_display_connector_iddcx_wait(PSPdispDisplayConnector *display_connector) {
	// The system resets the event when the request starts, and signals it when the request ends (even right away)
	if (DeviceIoControl(display_connector->device, IOCTL_PSPDISP_IDDCX_WAIT, NULL, 0, display_connector->event, sizeof(*display_connector->event), NULL, &display_connector->overlapped)) return true;
	if (GetLastError() == ERROR_IO_PENDING) return true;

	g_warning("%s(): the PSPdisp IddCx driver refused the wait request (error %lu)", __FUNCTION__, GetLastError());
	return false;
}

/**
 * @brief Convert a frame of the driver to `PSPDISP_PIXEL_FORMAT_8888` and hand it to the frame handler.
 *
 * The driver gives no dirty rectangles yet, so the whole frame is always dirty.
 * Its frames have the PSP resolution, as the driver scales the bigger modes of the EDID down to it (with the mip levels of the GPU).
 *
 * @param display_connector The connector, whose `event` holds the frame.
 */
void pspdisp_display_connector_iddcx_on_frame(PSPdispDisplayConnector *display_connector) {
	const PSPdispIddCxEvent *event = display_connector->event;
	uint32_t width = MIN(event->width, PSPDISP_DISPLAY_WIDTH);
	uint32_t height = MIN(event->height, PSPDISP_DISPLAY_HEIGHT);

	// B,G,R,X in memory: swap R and B, force opaque alpha
	for (uint32_t y = 0; y < height; y++) {
		const uint32_t *source_row = event->pixels + y * PSPDISP_IDDCX_WIDTH;
		uint32_t *row = display_connector->pixels + y * PSPDISP_DISPLAY_WIDTH;

		for (uint32_t x = 0; x < width; x++) {
			uint32_t pixel = source_row[x]; // 0xXXRRGGBB
			row[x] = 0xFF000000u                    // A = opaque
			       | ((pixel & 0x000000FFu) << 16)  // B -> bits 16-23
			       |  (pixel & 0x0000FF00u)         // G unchanged
			       | ((pixel & 0x00FF0000u) >> 16); // R -> bits 0-7
		}
	}

	PSPdispDirtyRectangle rectangle = { .x1 = 0, .y1 = 0, .x2 = width, .y2 = height };
	display_connector->frame_handler(display_connector->pixels, PSPDISP_DISPLAY_WIDTH, PSPDISP_DISPLAY_HEIGHT, PSPDISP_DISPLAY_WIDTH, PSPDISP_PIXEL_FORMAT_8888, &rectangle, 1, display_connector->handler_data);
}

/**
 * @brief Ask the driver for the next frame.
 *
 * The driver answers it through the pending wait request: right away with the newest frame not handled yet, or when Windows draws the next one.
 *
 * @param display_connector The connector.
 * @return `true` if the driver took the request, `false` if it failed.
 */
bool pspdisp_display_connector_iddcx_request_frame(PSPdispDisplayConnector *display_connector) {
	if (pspdisp_display_connector_iddcx_control(display_connector->device, IOCTL_PSPDISP_IDDCX_REQUEST_FRAME, NULL, 0)) return true;

	g_warning("%s(): the PSPdisp IddCx driver refused the frame request (error %lu)", __FUNCTION__, GetLastError());
	return false;
}

/**
 * @brief Tell GLib if the source must dispatch: when the event of `overlapped` is signaled, the driver answered.
 *
 * @param source The connector.
 * @return `TRUE` when the event of `overlapped` is signaled.
 */
gboolean pspdisp_display_connector_iddcx_check(GSource *source) {
	// GLib checks by itself only the fds added with `g_source_add_unix_fd()`, which doesn't exist on Windows (there a `GPollFD` holds a `HANDLE`), so the event is added with `g_source_add_poll()` and its `revents` must be checked here.
	// Without it the event never dispatches the source, and as nobody resets it, `poll()` returns at once while it stays signaled: the thread would spin.
	return ((PSPdispDisplayConnector *) source)->poll.revents != 0;
}

/**
 * @brief Tell the lost handler that the driver is gone, as the source destroys itself.
 *
 * @param display_connector The connector.
 * @return Always `G_SOURCE_REMOVE`, for the dispatch to return it.
 */
gboolean pspdisp_display_connector_iddcx_lost(PSPdispDisplayConnector *display_connector) {
	display_connector->lost_handler(display_connector->handler_data);
	return G_SOURCE_REMOVE;
}

/**
 * @brief Handle the answer of the driver, then ask for its next event (and the next frame, after a frame).
 *
 * @param source The connector.
 * @param callback Unused, the connector calls its handlers itself.
 * @param user_data Unused.
 * @return `G_SOURCE_CONTINUE` while the driver answers, or `G_SOURCE_REMOVE` if it failed.
 */
gboolean pspdisp_display_connector_iddcx_dispatch(GSource *source, GSourceFunc callback, gpointer user_data) {
	(void) callback;
	(void) user_data;
	PSPdispDisplayConnector *display_connector = (PSPdispDisplayConnector *) source;
	DWORD bytes = 0;

	if (!GetOverlappedResult(display_connector->device, &display_connector->overlapped, &bytes, FALSE)) {
		if (GetLastError() == ERROR_IO_INCOMPLETE) return G_SOURCE_CONTINUE; // Not answered yet

		g_warning("%s(): the wait request failed (error %lu)", __FUNCTION__, GetLastError());
		return pspdisp_display_connector_iddcx_lost(display_connector);
	}

	if (bytes < offsetof(PSPdispIddCxEvent, pixels)) {
		g_warning("%s(): the PSPdisp IddCx driver answered %lu bytes, less than an event", __FUNCTION__, bytes);
	} else if (display_connector->event->type == PSPDISP_IDDCX_EVENT_FRAME && bytes == sizeof(PSPdispIddCxEvent)) {
		pspdisp_display_connector_iddcx_on_frame(display_connector);
		if (!pspdisp_display_connector_iddcx_request_frame(display_connector)) // The next one, Windows paces it
			return pspdisp_display_connector_iddcx_lost(display_connector);
	} else if (display_connector->event->type == PSPDISP_IDDCX_EVENT_DPMS && display_connector->event->dpms_mode <= PSPDISP_DPMS_MODE_OFF) {
		display_connector->dpms_handler((PSPdispDPMSMode) display_connector->event->dpms_mode, display_connector->handler_data);
	} else {
		g_warning("%s(): unknown event %u from the PSPdisp IddCx driver", __FUNCTION__, display_connector->event->type);
	}

	return pspdisp_display_connector_iddcx_wait(display_connector) ? G_SOURCE_CONTINUE : pspdisp_display_connector_iddcx_lost(display_connector);
}

/**
 * @brief Free the connector resources, called by GLib when the last reference to the source is released.
 *
 * Cancels the pending wait request and disconnects the monitor, so it disappears from Windows.
 *
 * @param source The connector.
 */
void pspdisp_display_connector_iddcx_finalize(GSource *source) {
	PSPdispDisplayConnector *display_connector = (PSPdispDisplayConnector *) source;
	DWORD bytes = 0;

	if (CancelIoEx(display_connector->device, &display_connector->overlapped) || GetLastError() != ERROR_NOT_FOUND) {
		GetOverlappedResult(display_connector->device, &display_connector->overlapped, &bytes, TRUE); // The driver writes `event` until the request ends
	}
	pspdisp_display_connector_iddcx_control(display_connector->device, IOCTL_PSPDISP_IDDCX_DISCONNECT, NULL, 0);

	CloseHandle(display_connector->overlapped.hEvent);
	CloseHandle(display_connector->device);
	g_free(display_connector->event);
	g_free(display_connector->pixels);
}

/**
 * @brief Functions of the connector source.
 *
 * No `prepare`: there's no timeout, it only waits for the event of `overlapped`.
 */
GSourceFuncs pspdisp_display_connector_iddcx_source_funcs = {
	.check    = pspdisp_display_connector_iddcx_check,
	.dispatch = pspdisp_display_connector_iddcx_dispatch,
	.finalize = pspdisp_display_connector_iddcx_finalize,
};

/**
 * @brief Create the virtual display through the PSPdisp IddCx driver, and the source of its events.
 *
 * Connects a monitor with the EDID, so Windows only sets its modes (the driver scales them down to the PSP resolution), then asks the driver for its first event and its first frame.
 *
 * @param edid The identification given to the computer.
 * @param frame_handler Handle each new frame.
 * @param dpms_handler Handle each DPMS change.
 * @param lost_handler Handle the loss of the virtual display.
 * @param data Given to the handlers.
 * @return The source of the display events (not attached yet), or `NULL` if it failed.
 */
GSource *pspdisp_display_connector_connect(PSPdispEDID edid, PSPdispConnectorFrameHandler frame_handler, PSPdispDPMSHandler dpms_handler, PSPdispLostHandler lost_handler, gpointer data) {
	HANDLE device = pspdisp_display_connector_iddcx_open();
	if (device == INVALID_HANDLE_VALUE) return NULL;

	if (!pspdisp_display_connector_iddcx_control(device, IOCTL_PSPDISP_IDDCX_CONNECT, &edid, sizeof(edid))) {
		g_warning("%s(): the PSPdisp IddCx driver refused the monitor (error %lu)", __FUNCTION__, GetLastError());
		CloseHandle(device);
		return NULL;
	}

	GSource *source = g_source_new(&pspdisp_display_connector_iddcx_source_funcs, sizeof(PSPdispDisplayConnector));
	PSPdispDisplayConnector *display_connector = (PSPdispDisplayConnector *) source;
	display_connector->frame_handler = frame_handler;
	display_connector->dpms_handler = dpms_handler;
	display_connector->lost_handler = lost_handler;
	display_connector->handler_data = data;
	display_connector->device = device;
	display_connector->overlapped = (OVERLAPPED) { .hEvent = CreateEventW(NULL, TRUE, FALSE, NULL) };
	display_connector->poll = (GPollFD) { .fd = (gintptr) display_connector->overlapped.hEvent, .events = G_IO_IN };
	display_connector->event = g_new0(PSPdispIddCxEvent, 1);
	display_connector->pixels = g_new0(uint32_t, PSPDISP_DISPLAY_WIDTH * PSPDISP_DISPLAY_HEIGHT);
	g_source_add_poll(source, &display_connector->poll);

	// The answers come through the wait request once the source is attached
	if (!pspdisp_display_connector_iddcx_wait(display_connector) || !pspdisp_display_connector_iddcx_request_frame(display_connector)) {
		g_source_unref(source); // Finalized: disconnects the monitor and frees everything
		return NULL;
	}

	return source;
}
