/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * driver.c - PSPdisp virtual HID gamepad driver     *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * HID minidriver (UMDF 2) of the PSPdisp virtual gamepad, based on Microsoft's `hid/vhidmini2` sample.
 *
 * Windows loads it in Session 0, inside its UMDF host process, under its HID class driver: `mshidumdf.sys` (Microsoft's) passes it the requests of the HID class (the `IOCTL_HID_*` of `hidport.h`).
 * Its report descriptor has two top-level collections (see `protocol.h`), which Windows opens as two HID devices:
 *   - the gamepad: the HID class keeps read requests pending (`IOCTL_HID_READ_REPORT`), completed with the state of the gamepad when it changes;
 *   - the control: the daemon writes the state of the gamepad to its output report (`IOCTL_HID_WRITE_REPORT` or `IOCTL_HID_SET_OUTPUT_REPORT`).
 *
 * Built with MSVC and the WDK by `vhid.mk`, as C (unlike the IddCx driver, the WDF and HID headers are C), and installed with `pspdisp-vhid.inf` (see its header).
 */

#define WIN32_NO_STATUS // The STATUS_* codes come from <ntstatus.h>, included by <wdf.h>, not from <windows.h>
#include <windows.h>
#undef WIN32_NO_STATUS
#include <wdf.h>
#include <hidport.h>
#include <assert.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "protocol.h"

/**
 * @brief Write a message to the debugger (e.g. DebugView, with "Capture Global Win32").
 *
 * @param format The format of the message, as `printf()` with wide characters.
 * @param ... The values of the format.
 */
void Log(const wchar_t *format, ...) {
	wchar_t buffer[512];
	va_list arguments;
	va_start(arguments, format);
	_vsnwprintf_s(buffer, ARRAYSIZE(buffer), _TRUNCATE, format, arguments);
	va_end(arguments);
	OutputDebugStringW(buffer);
}

/* ------------------------------------------------------------------------- */
/* Descriptors                                                               */
/* ------------------------------------------------------------------------- */

/**
 * @brief Report descriptor: the gamepad (input report, as an Xbox controller under DirectInput) and the control collection (output report), see `protocol.h`.
 */
const UINT8 ReportDescriptor[] = {
	// The gamepad
	0x05, 0x01,             // Usage Page (Generic Desktop)
	0x09, 0x05,             // Usage (Game Pad)
	0xA1, 0x01,             // Collection (Application)
	0x85, PSPDISP_VHID_GAMEPAD_REPORT_ID, // Report ID
	0x09, 0x01,             //   Usage (Pointer)
	0xA1, 0x00,             //   Collection (Physical): the sticks
	0x09, 0x30,             //     Usage (X)
	0x09, 0x31,             //     Usage (Y)
	0x09, 0x33,             //     Usage (Rx)
	0x09, 0x34,             //     Usage (Ry)
	0x16, 0x00, 0x80,       //     Logical Minimum (-32768)
	0x26, 0xFF, 0x7F,       //     Logical Maximum (32767)
	0x75, 0x10,             //     Report Size (16)
	0x95, 0x04,             //     Report Count (4)
	0x81, 0x02,             //     Input (Data, Variable, Absolute)
	0xC0,                   //   End Collection
	0x09, 0x32,             //   Usage (Z): the left trigger
	0x09, 0x35,             //   Usage (Rz): the right trigger
	0x15, 0x00,             //   Logical Minimum (0)
	0x26, 0xFF, 0x00,       //   Logical Maximum (255)
	0x75, 0x08,             //   Report Size (8)
	0x95, 0x02,             //   Report Count (2)
	0x81, 0x02,             //   Input (Data, Variable, Absolute)
	0x09, 0x39,             //   Usage (Hat Switch): the directional pad
	0x15, 0x00,             //   Logical Minimum (0)
	0x25, 0x07,             //   Logical Maximum (7)
	0x35, 0x00,             //   Physical Minimum (0)
	0x46, 0x3B, 0x01,       //   Physical Maximum (315)
	0x65, 0x14,             //   Unit (Degrees)
	0x75, 0x08,             //   Report Size (8)
	0x95, 0x01,             //   Report Count (1)
	0x81, 0x42,             //   Input (Data, Variable, Absolute, Null State): out of 0-7 when released
	0x65, 0x00,             //   Unit (None)
	0x45, 0x00,             //   Physical Maximum (0)
	0x05, 0x09,             //   Usage Page (Button)
	0x19, 0x01,             //   Usage Minimum (1)
	0x29, 0x0A,             //   Usage Maximum (10)
	0x15, 0x00,             //   Logical Minimum (0)
	0x25, 0x01,             //   Logical Maximum (1)
	0x75, 0x01,             //   Report Size (1)
	0x95, 0x0A,             //   Report Count (10)
	0x81, 0x02,             //   Input (Data, Variable, Absolute)
	0x75, 0x06,             //   Report Size (6)
	0x95, 0x01,             //   Report Count (1)
	0x81, 0x03,             //   Input (Constant): the padding of the buttons to 16 bits
	0xC0,                   // End Collection

	// The control collection, the daemon writes the state of the gamepad to its output report
	0x06, 0x00, 0xFF,       // Usage Page (Vendor Defined 0xFF00), PSPDISP_VHID_CONTROL_USAGE_PAGE
	0x09, 0x01,             // Usage (1), PSPDISP_VHID_CONTROL_USAGE
	0xA1, 0x01,             // Collection (Application)
	0x85, PSPDISP_VHID_CONTROL_REPORT_ID, // Report ID
	0x09, 0x02,             //   Usage (2)
	0x15, 0x00,             //   Logical Minimum (0)
	0x26, 0xFF, 0x00,       //   Logical Maximum (255)
	0x75, 0x08,             //   Report Size (8)
	0x95, sizeof(PSPdispVhidState), // Report Count, the bytes of `PSPdispVhidState`
	0x91, 0x02,             //   Output (Data, Variable, Absolute)
	0xC0,                   // End Collection
};

static_assert(sizeof(PSPdispVhidState) == 13, "The state must have the layout of the report descriptor");

/**
 * @brief HID descriptor, which gives the size of the report descriptor to the HID class.
 */
const HID_DESCRIPTOR HidDescriptor = {
	sizeof(HID_DESCRIPTOR), // bLength
	0x21,                   // bDescriptorType, HID
	0x0100,                 // bcdHID, HID 1.0
	0x00,                   // bCountry, not localized
	0x01,                   // bNumDescriptors
	{ { 0x22, sizeof(ReportDescriptor) } }, // DescriptorList, the report descriptor
};

/* ------------------------------------------------------------------------- */
/* Context                                                                   */
/* ------------------------------------------------------------------------- */

/**
 * @brief State of the device, shared by the requests.
 */
typedef struct DEVICE_CONTEXT {
	WDFQUEUE ReadQueue;     // Manual queue of the pending read requests of the HID class
	SRWLOCK Lock;           // Guards the fields below
	PSPdispVhidState State; // The state of the gamepad, the newest one written by the daemon
	bool Undelivered;       // If `State` changed since the last read request completed
} DEVICE_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceCtx);

/**
 * @brief Fill an input report of the gamepad with its state.
 *
 * @param ctx The device, locked.
 * @param report The report to fill.
 */
void FillInputReport(const DEVICE_CONTEXT *ctx, PSPdispVhidReport *report) {
	report->report_id = PSPDISP_VHID_GAMEPAD_REPORT_ID;
	report->state = ctx->State;
}

/**
 * @brief Complete a read request with the input report of the gamepad.
 *
 * @param ctx The device, locked.
 * @param request The `IOCTL_HID_READ_REPORT` request.
 */
void CompleteRead(DEVICE_CONTEXT *ctx, WDFREQUEST request) {
	PSPdispVhidReport *report;
	NTSTATUS s = WdfRequestRetrieveOutputBuffer(request, sizeof(*report), (PVOID *) &report, NULL);

	if (!NT_SUCCESS(s)) {
		WdfRequestComplete(request, s);
		return;
	}
	FillInputReport(ctx, report);
	ctx->Undelivered = false;
	WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, sizeof(*report));
}

/* ------------------------------------------------------------------------- */
/* Requests of the HID class                                                 */
/* ------------------------------------------------------------------------- */

/**
 * @brief Copy a buffer into the output buffer of a request and complete it.
 *
 * @param request The request.
 * @param data The data.
 * @param size The size of the data in bytes.
 */
void CompleteWithData(WDFREQUEST request, const void *data, size_t size) {
	void *buffer;
	NTSTATUS s = WdfRequestRetrieveOutputBuffer(request, size, &buffer, NULL);

	if (!NT_SUCCESS(s)) {
		WdfRequestComplete(request, s);
		return;
	}
	memcpy(buffer, data, size);
	WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, size);
}

/**
 * @brief Queue a read request of the HID class, or complete it right away with a state not delivered yet.
 *
 * @param ctx The device.
 * @param request The `IOCTL_HID_READ_REPORT` request.
 */
void QueueRead(DEVICE_CONTEXT *ctx, WDFREQUEST request) {
	NTSTATUS s = STATUS_SUCCESS;

	AcquireSRWLockExclusive(&ctx->Lock);
	if (ctx->Undelivered)
		CompleteRead(ctx, request);
	else
		s = WdfRequestForwardToIoQueue(request, ctx->ReadQueue);
	ReleaseSRWLockExclusive(&ctx->Lock);

	if (!NT_SUCCESS(s))
		WdfRequestComplete(request, s);
}

/**
 * @brief Take the state the daemon wrote to the output report of the control collection, and deliver it to a pending read request.
 *
 * In UMDF, the report (with its ID first) is in the input buffer of the request.
 *
 * @param ctx The device.
 * @param request The `IOCTL_HID_WRITE_REPORT` or `IOCTL_HID_SET_OUTPUT_REPORT` request.
 */
void WriteState(DEVICE_CONTEXT *ctx, WDFREQUEST request) {
	const PSPdispVhidReport *report;
	size_t size = 0;
	WDFREQUEST read;
	NTSTATUS s = WdfRequestRetrieveInputBuffer(request, sizeof(*report), (PVOID *) &report, &size);

	if (!NT_SUCCESS(s)) {
		WdfRequestComplete(request, s);
		return;
	}
	if (report->report_id != PSPDISP_VHID_CONTROL_REPORT_ID) {
		WdfRequestComplete(request, STATUS_INVALID_PARAMETER);
		return;
	}

	AcquireSRWLockExclusive(&ctx->Lock);
	ctx->State = report->state;
	ctx->Undelivered = true;
	if (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(ctx->ReadQueue, &read)))
		CompleteRead(ctx, read);
	ReleaseSRWLockExclusive(&ctx->Lock);

	WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, sizeof(*report));
}

/**
 * @brief Answer the input report asked directly (`HidD_GetInputReport()`), with the state of the gamepad.
 *
 * @param ctx The device.
 * @param request The `IOCTL_HID_GET_INPUT_REPORT` request.
 */
void GetInputReport(DEVICE_CONTEXT *ctx, WDFREQUEST request) {
	PSPdispVhidReport report;

	AcquireSRWLockShared(&ctx->Lock);
	report.report_id = PSPDISP_VHID_GAMEPAD_REPORT_ID;
	report.state = ctx->State;
	ReleaseSRWLockShared(&ctx->Lock);

	CompleteWithData(request, &report, sizeof(report));
}

/**
 * @brief Answer a string of the device (`HidD_GetProductString()` and the like).
 *
 * In UMDF, the string ID is in the low word of the ULONG of the input buffer (its high word is the language).
 *
 * @param request The `IOCTL_HID_GET_STRING` request.
 */
void GetString(WDFREQUEST request) {
	const ULONG *id;
	const wchar_t *text;
	NTSTATUS s = WdfRequestRetrieveInputBuffer(request, sizeof(*id), (PVOID *) &id, NULL);

	if (!NT_SUCCESS(s)) {
		WdfRequestComplete(request, s);
		return;
	}
	switch (*id & 0xFFFF) {
		case HID_STRING_ID_IMANUFACTURER: text = L"PSPdisp"; break;
		case HID_STRING_ID_IPRODUCT:      text = L"PSPdisp Virtual Gamepad"; break;
		default:
			WdfRequestComplete(request, STATUS_INVALID_PARAMETER); // No serial number
			return;
	}
	CompleteWithData(request, text, (wcslen(text) + 1) * sizeof(wchar_t));
}

/**
 * @brief Handle a request of the HID class (through `mshidumdf.sys`).
 *
 * @param queue The default queue.
 * @param request The request.
 * @param outLen Unused, the size of the output buffer.
 * @param inLen Unused, the size of the input buffer.
 * @param code The IOCTL of the request.
 */
VOID EvtIoDeviceControl(WDFQUEUE queue, WDFREQUEST request, size_t outLen, size_t inLen, ULONG code) {
	DEVICE_CONTEXT *ctx = GetDeviceCtx(WdfIoQueueGetDevice(queue));
	HID_DEVICE_ATTRIBUTES attributes;

	UNREFERENCED_PARAMETER(outLen);
	UNREFERENCED_PARAMETER(inLen);

	switch (code) {
		case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
			CompleteWithData(request, &HidDescriptor, sizeof(HidDescriptor));
			break;
		case IOCTL_HID_GET_REPORT_DESCRIPTOR:
			CompleteWithData(request, ReportDescriptor, sizeof(ReportDescriptor));
			break;
		case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
			memset(&attributes, 0, sizeof(attributes));
			attributes.Size = sizeof(attributes);
			attributes.VendorID = PSPDISP_VHID_VENDOR_ID;
			attributes.ProductID = PSPDISP_VHID_PRODUCT_ID;
			attributes.VersionNumber = PSPDISP_VHID_VERSION;
			CompleteWithData(request, &attributes, sizeof(attributes));
			break;
		case IOCTL_HID_READ_REPORT:
			QueueRead(ctx, request);
			break;
		case IOCTL_HID_WRITE_REPORT:
		case IOCTL_HID_SET_OUTPUT_REPORT:
			WriteState(ctx, request);
			break;
		case IOCTL_HID_GET_INPUT_REPORT:
			GetInputReport(ctx, request);
			break;
		case IOCTL_HID_GET_STRING:
			GetString(request);
			break;
		case IOCTL_HID_ACTIVATE_DEVICE:
		case IOCTL_HID_DEACTIVATE_DEVICE:
			WdfRequestComplete(request, STATUS_SUCCESS);
			break;
		default:
			WdfRequestComplete(request, STATUS_NOT_IMPLEMENTED); // E.g. the feature reports, the indexed strings, which the gamepad doesn't have
			break;
	}
}

/* ------------------------------------------------------------------------- */
/* Driver and device                                                         */
/* ------------------------------------------------------------------------- */

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD EvtDeviceAdd;

/**
 * @brief Entry point of the driver: create the WDF driver, with its device add callback.
 *
 * @param driverObject The driver object.
 * @param registryPath The registry key of the driver.
 * @return `STATUS_SUCCESS`, or an error status if it failed.
 */
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath) {
	WDF_DRIVER_CONFIG config;
	WDF_DRIVER_CONFIG_INIT(&config, EvtDeviceAdd);
	return WdfDriverCreate(driverObject, registryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}

/**
 * @brief Create the device: a filter under `mshidumdf.sys` (which is the function driver of the HID device), with the queue of the HID requests and the one of the pending reads.
 *
 * @param driver Unused, the driver.
 * @param deviceInit The initialization of the device.
 * @return `STATUS_SUCCESS`, or an error status if it failed.
 */
NTSTATUS EvtDeviceAdd(WDFDRIVER driver, PWDFDEVICE_INIT deviceInit) {
	WDF_OBJECT_ATTRIBUTES attr;
	WDF_IO_QUEUE_CONFIG queueConfig;
	WDFDEVICE device;
	WDFQUEUE queue;
	DEVICE_CONTEXT *ctx;
	NTSTATUS s;

	UNREFERENCED_PARAMETER(driver);

	WdfFdoInitSetFilter(deviceInit);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, DEVICE_CONTEXT);
	s = WdfDeviceCreate(&deviceInit, &attr, &device);
	if (!NT_SUCCESS(s)) {
		Log(L"[PSPdisp] WdfDeviceCreate failed 0x%08X\n", s);
		return s;
	}

	ctx = GetDeviceCtx(device);
	InitializeSRWLock(&ctx->Lock);
	ctx->State.hat = PSPDISP_VHID_HAT_CENTERED; // At rest, the rest of the state is 0
	ctx->Undelivered = true; // The first read gets the state at rest

	// The HID requests, in parallel, as the reads stay pending
	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
	queueConfig.EvtIoDeviceControl = EvtIoDeviceControl;
	s = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);
	if (!NT_SUCCESS(s)) return s;

	// Manual queue: the read requests stay there until the state changes
	WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);
	return WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &ctx->ReadQueue);
}
