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
 * It's the DualShock 4 of the virtual gamepad plugged by USB (see `protocol.h` and `../ds4.c`):
 *   - the HID class keeps read requests pending (`IOCTL_HID_READ_REPORT`), completed with the input report of the DualShock 4 when the state of the gamepad changes;
 *   - the daemon writes the state of the gamepad to the control report (`IOCTL_HID_WRITE_REPORT` or `IOCTL_UMDF_HID_SET_OUTPUT_REPORT`), the output report of the DualShock 4 is ignored;
 *   - the feature reports the games ask a DualShock 4 for are answered with fixed data (`IOCTL_UMDF_HID_GET_FEATURE`).
 *
 * Built with MSVC and the WDK by `vhid.mk`, as C (unlike the IddCx driver, the WDF and HID headers are C), and installed with `pspdisp-vhid.inf` (see its header).
 */

#define WIN32_NO_STATUS // The STATUS_* codes come from <ntstatus.h>, included by <wdf.h>, not from <windows.h>
#include <windows.h>
#undef WIN32_NO_STATUS
#include <wdf.h>
#include <hidport.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../ds4.c"
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
 * @brief HID descriptor, which gives the size of the report descriptor (`pspdisp_ds4_report_descriptor`) to the HID class.
 */
const HID_DESCRIPTOR HidDescriptor = {
	sizeof(HID_DESCRIPTOR), // bLength
	0x21,                   // bDescriptorType, HID
	0x0111,                 // bcdHID, HID 1.11 (as the DualShock 4)
	0x00,                   // bCountry, not localized
	0x01,                   // bNumDescriptors
	{ { 0x22, sizeof(pspdisp_ds4_report_descriptor) } }, // DescriptorList, the report descriptor
};

/* ------------------------------------------------------------------------- */
/* Context                                                                   */
/* ------------------------------------------------------------------------- */

/**
 * @brief State of the device, shared by the requests.
 */
typedef struct DEVICE_CONTEXT {
	WDFQUEUE ReadQueue;     // Manual queue of the pending read requests of the HID class
	LARGE_INTEGER Start;    // When the device was created, the origin of the timestamps of the input reports (from `QueryPerformanceCounter()`)
	LARGE_INTEGER Frequency; // The frequency of `QueryPerformanceCounter()`
	SRWLOCK Lock;           // Guards the fields below
	PSPdispDS4State State;  // The state of the gamepad, the newest one written by the daemon
	bool Undelivered;       // If `State` changed since the last read request completed
	UINT8 Counter;          // The counter of the input reports, 6 bits
} DEVICE_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceCtx);

/**
 * @brief Fill the input report of the DualShock 4 with the state of the gamepad, and count it.
 *
 * @param ctx The device, locked exclusively.
 * @param report The report to fill, `PSPDISP_DS4_INPUT_REPORT_SIZE` bytes.
 */
void FillInputReport(DEVICE_CONTEXT *ctx, UINT8 *report) {
	LARGE_INTEGER now;

	// The time since the device was created, in microseconds (the seconds and the rest apart, so the ticks times 1000000 can't overflow)
	QueryPerformanceCounter(&now);
	LONGLONG ticks = now.QuadPart - ctx->Start.QuadPart;
	ULONGLONG microseconds = (ULONGLONG) (ticks / ctx->Frequency.QuadPart) * 1000000 + (ULONGLONG) (ticks % ctx->Frequency.QuadPart) * 1000000 / ctx->Frequency.QuadPart;
	pspdisp_ds4_input_report(&ctx->State, &ctx->Counter, microseconds, report);
}

/**
 * @brief Complete a read request with the input report of the gamepad.
 *
 * @param ctx The device, locked exclusively.
 * @param request The `IOCTL_HID_READ_REPORT` request.
 */
void CompleteRead(DEVICE_CONTEXT *ctx, WDFREQUEST request) {
	UINT8 *report;
	NTSTATUS s = WdfRequestRetrieveOutputBuffer(request, PSPDISP_DS4_INPUT_REPORT_SIZE, (PVOID *) &report, NULL);

	if (!NT_SUCCESS(s)) {
		WdfRequestComplete(request, s);
		return;
	}
	FillInputReport(ctx, report);
	ctx->Undelivered = false;
	WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, PSPDISP_DS4_INPUT_REPORT_SIZE);
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
 * @brief Take an output report: the state the daemon wrote in the control report, delivered to a pending read request, or the rumble and the light bar of the DualShock 4, ignored.
 *
 * In UMDF, the report (with its ID first) is in the input buffer of the request.
 *
 * @param ctx The device.
 * @param request The `IOCTL_HID_WRITE_REPORT` or `IOCTL_UMDF_HID_SET_OUTPUT_REPORT` request.
 */
void WriteReport(DEVICE_CONTEXT *ctx, WDFREQUEST request) {
	const UINT8 *report;
	size_t size = 0;
	WDFREQUEST read;
	NTSTATUS s = WdfRequestRetrieveInputBuffer(request, 1, (PVOID *) &report, &size);

	if (!NT_SUCCESS(s)) {
		WdfRequestComplete(request, s);
		return;
	}

	switch (report[0]) {
		case PSPDISP_DS4_CONTROL_REPORT_ID:
			if (size < 1 + sizeof(PSPdispDS4State)) {
				WdfRequestComplete(request, STATUS_INVALID_BUFFER_SIZE);
				return;
			}
			AcquireSRWLockExclusive(&ctx->Lock);
			memcpy(&ctx->State, &((const PSPdispDS4ControlReport *) report)->state, sizeof(ctx->State));
			ctx->Undelivered = true;
			if (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(ctx->ReadQueue, &read)))
				CompleteRead(ctx, read);
			ReleaseSRWLockExclusive(&ctx->Lock);
			break;
		case PSPDISP_DS4_OUTPUT_REPORT_ID:
			break; // The gamepad has no rumble nor light bar
		default:
			WdfRequestComplete(request, STATUS_INVALID_PARAMETER);
			return;
	}
	WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, size);
}

/**
 * @brief Answer the input report asked directly (`HidD_GetInputReport()`), with the state of the gamepad.
 *
 * @param ctx The device.
 * @param request The `IOCTL_UMDF_HID_GET_INPUT_REPORT` request.
 */
void GetInputReport(DEVICE_CONTEXT *ctx, WDFREQUEST request) {
	UINT8 report[PSPDISP_DS4_INPUT_REPORT_SIZE];

	AcquireSRWLockExclusive(&ctx->Lock);
	FillInputReport(ctx, report);
	ReleaseSRWLockExclusive(&ctx->Lock);

	CompleteWithData(request, report, sizeof(report));
}

/**
 * @brief Answer a feature report of the DualShock 4 (`HidD_GetFeature()`), with fixed data.
 *
 * In UMDF, the report ID is in the input buffer of the request, and the report (with its ID first) goes to its output buffer.
 *
 * @param request The `IOCTL_UMDF_HID_GET_FEATURE` request.
 */
void GetFeature(WDFREQUEST request) {
	const UINT8 *id;
	UINT8 report[PSPDISP_DS4_FEATURE_REPORT_MAX_SIZE];
	size_t size;
	NTSTATUS s = WdfRequestRetrieveInputBuffer(request, 1, (PVOID *) &id, NULL);

	if (!NT_SUCCESS(s)) {
		WdfRequestComplete(request, s);
		return;
	}

	size = pspdisp_ds4_feature_report(pspdisp_ds4_mac_address, *id, report);
	if (size == 0) {
		WdfRequestComplete(request, STATUS_INVALID_PARAMETER);
		return;
	}
	CompleteWithData(request, report, size);
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
		case HID_STRING_ID_IPRODUCT:      text = PSPDISP_VHID_PRODUCT_STRING; break;
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
			CompleteWithData(request, pspdisp_ds4_report_descriptor, sizeof(pspdisp_ds4_report_descriptor));
			break;
		case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
			memset(&attributes, 0, sizeof(attributes));
			attributes.Size = sizeof(attributes);
			attributes.VendorID = PSPDISP_DS4_VENDOR_ID;
			attributes.ProductID = PSPDISP_DS4_PRODUCT_ID;
			attributes.VersionNumber = PSPDISP_VHID_VERSION;
			CompleteWithData(request, &attributes, sizeof(attributes));
			break;
		case IOCTL_HID_READ_REPORT:
			QueueRead(ctx, request);
			break;
		// In UMDF, `HidD_SetOutputReport()`, `HidD_GetInputReport()` and `HidD_GetFeature()` come as the `IOCTL_UMDF_HID_*` of `hidport.h`, not as the `IOCTL_HID_*` of `hidclass.h`
		case IOCTL_HID_WRITE_REPORT:
		case IOCTL_UMDF_HID_SET_OUTPUT_REPORT:
			WriteReport(ctx, request);
			break;
		case IOCTL_UMDF_HID_GET_INPUT_REPORT:
			GetInputReport(ctx, request);
			break;
		case IOCTL_UMDF_HID_GET_FEATURE:
			GetFeature(request);
			break;
		case IOCTL_HID_GET_STRING:
			GetString(request);
			break;
		case IOCTL_HID_ACTIVATE_DEVICE:
		case IOCTL_HID_DEACTIVATE_DEVICE:
			WdfRequestComplete(request, STATUS_SUCCESS);
			break;
		default:
			WdfRequestComplete(request, STATUS_NOT_IMPLEMENTED); // E.g. setting the feature reports, the indexed strings, which the gamepad doesn't have
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
	QueryPerformanceFrequency(&ctx->Frequency);
	QueryPerformanceCounter(&ctx->Start);
	InitializeSRWLock(&ctx->Lock);
	ctx->State = pspdisp_ds4_rest_state;
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
