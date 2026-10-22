/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * protocol.h - IddCx driver and connector protocol  *
 *                                                   *
 *  This file shall not define symbols.              *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * Protocol between the PSPdisp IddCx driver (`driver.c`, built with the WDK) and its display connector (`../iddcx.c`, built with MinGW).
 *
 * The driver runs in Session 0, inside the UMDF host process of Windows, so the connector talks to it through device I/O control requests (IOCTLs) on its device interface.
 *
 * The connector connects a monitor with `IOCTL_PSPDISP_IDDCX_CONNECT`, then always keeps a `IOCTL_PSPDISP_IDDCX_WAIT` pending, which the driver completes with the next power change or asked frame.
 * It asks for each frame with `IOCTL_PSPDISP_IDDCX_REQUEST_FRAME`, like `evdi_request_update()` on EVDI, so the driver only copies the frames the connector wants.
 *
 * It's plain C11 on purpose, without `shared.h` or `edid.c`, because MSVC builds the driver and supports neither their C23 enums nor their GCC pragmas.
 */

#pragma once

#include <stdint.h>
#include <windows.h>
#include <winioctl.h>

/**
 * @brief GUID of the device interface of the driver, the one the connector opens: {08C59B93-000E-42E0-BFEE-E0AED09D408C}.
 *
 * It's an initializer, so each side defines its own constant (e.g. `static const GUID guid = PSPDISP_IDDCX_INTERFACE_GUID;`).
 */
#define PSPDISP_IDDCX_INTERFACE_GUID { 0x08C59B93, 0x000E, 0x42E0, { 0xBF, 0xEE, 0xE0, 0xAE, 0xD0, 0x9D, 0x40, 0x8C } }

#define PSPDISP_IDDCX_WIDTH     480 // Width in pixels of the frames, the PSP display width
#define PSPDISP_IDDCX_HEIGHT    272 // Height in lines of the frames, the PSP display height
#define PSPDISP_IDDCX_EDID_SIZE 128 // Size in bytes of the EDID given on connection (a base block, without extension blocks)

/**
 * @brief Connect the monitor: it appears on Windows, described by the EDID in the input buffer (`PSPDISP_IDDCX_EDID_SIZE` bytes).
 *
 * Fails with `ERROR_BUSY` when a monitor is already connected, and with `ERROR_NOT_READY` while the driver is still starting.
 */
#define IOCTL_PSPDISP_IDDCX_CONNECT CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_WRITE_DATA)

/**
 * @brief Disconnect the monitor: it disappears from Windows, nothing happens when it's not connected.
 */
#define IOCTL_PSPDISP_IDDCX_DISCONNECT CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_WRITE_DATA)

/**
 * @brief Wait for the next event of the monitor, written in the output buffer as a `PSPdispIddCxEvent`.
 *
 * It stays pending until there's a power change, or a frame asked with `IOCTL_PSPDISP_IDDCX_REQUEST_FRAME`, so keep one always pending (e.g. with overlapped I/O).
 *
 * Cancelling it (e.g. when the connector process ends) also disconnects the monitor, so it never outlives the connector.
 */
#define IOCTL_PSPDISP_IDDCX_WAIT CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_DATA)

/**
 * @brief Ask for the next frame, which completes the pending `IOCTL_PSPDISP_IDDCX_WAIT` as soon as there's a frame not delivered yet.
 *
 * It completes right away, it only marks a frame as wanted, so asking again before the frame comes does nothing.
 */
#define IOCTL_PSPDISP_IDDCX_REQUEST_FRAME CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_READ_DATA)

#define PSPDISP_IDDCX_EVENT_FRAME 1 // `PSPdispIddCxEvent.pixels` holds the frame asked with `IOCTL_PSPDISP_IDDCX_REQUEST_FRAME`
#define PSPDISP_IDDCX_EVENT_DPMS  2 // `PSPdispIddCxEvent.dpms_mode` holds a new power mode

#define PSPDISP_IDDCX_DPMS_ON  0 // Power mode on, the value of `PSPDISP_DPMS_MODE_ON`
#define PSPDISP_IDDCX_DPMS_OFF 3 // Power mode off, the value of `PSPDISP_DPMS_MODE_OFF`

/**
 * @brief Event of the monitor, the answer of `IOCTL_PSPDISP_IDDCX_WAIT`.
 *
 * A power change only writes the fields before `pixels`.
 */
typedef struct {
	uint32_t type;                                               // `PSPDISP_IDDCX_EVENT_FRAME` or `PSPDISP_IDDCX_EVENT_DPMS`
	uint32_t dpms_mode;                                          // For power changes: `PSPDISP_IDDCX_DPMS_ON` or `PSPDISP_IDDCX_DPMS_OFF`
	uint32_t width;                                              // For frames: width in pixels, at most `PSPDISP_IDDCX_WIDTH`
	uint32_t height;                                             // For frames: height in lines, at most `PSPDISP_IDDCX_HEIGHT`
	uint32_t pixels[PSPDISP_IDDCX_WIDTH * PSPDISP_IDDCX_HEIGHT]; // For frames: B,G,R,X in memory (`DXGI_FORMAT_B8G8R8A8_UNORM`), `PSPDISP_IDDCX_WIDTH` pixels per row, scaled down by the driver from a bigger mode
} PSPdispIddCxEvent;
