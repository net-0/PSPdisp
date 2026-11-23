/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * usb.h - Interface of the USB driver of the app    *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * What the app needs to run the USB driver of the kernel module `usb.prx` (see `usb.c`).
 * The app loads the module, starts the driver with `sceUsbStart(PSPDISP_USB_DRIVER_NAME, 0, 0)`, after the bus driver, and activates it with `sceUsbActivate()` and one of its product IDs.
 * Then it talks to the desktop with `pspdisp_usb_read()` and `pspdisp_usb_write()`.
 */

#pragma once

#include <psptypes.h>

#define PSPDISP_USB_DRIVER_NAME       "PSPdispUSBDriver" // Name of the driver, to start and stop it
#define PSPDISP_USB_PRODUCT_ID_TYPE_B 0x01C9             // Product ID of the PSP as type B, to activate the driver with
#define PSPDISP_USB_PRODUCT_ID_TYPE_C 0x01CA             // Product ID of the PSP as type C (the default "USB driver" of the app), to activate the driver with

/**
 * @brief Read data from the desktop, all of it.
 *
 * It fails at once while the desktop didn't configure the PSP (e.g. it's not plugged).
 * Otherwise it waits for the desktop to send the data, until the driver is detached (e.g. by `sceUsbDeactivate()`).
 * One read or write is done at a time, the others wait for it.
 *
 * @param data Where to write the data.
 * @param size The size of the data, in bytes.
 * @return `true` if all of it was read, or `false` if it failed.
 */
bool pspdisp_usb_read(void *data, u32 size);

/**
 * @brief Write data to the desktop, all of it.
 *
 * It fails at once while the desktop didn't configure the PSP (e.g. it's not plugged).
 * Otherwise it waits for the desktop to take the data, until the driver is detached (e.g. by `sceUsbDeactivate()`).
 * One read or write is done at a time, the others wait for it.
 *
 * @param data The data.
 * @param size The size of the data, in bytes.
 * @return `true` if all of it was written, or `false` if it failed.
 */
bool pspdisp_usb_write(const void *data, u32 size);
