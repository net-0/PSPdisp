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
 * It's a DualShock 4 plugged by USB (see `protocol.h`):
 *   - the HID class keeps read requests pending (`IOCTL_HID_READ_REPORT`), completed with the input report of the DualShock 4 when the state of the gamepad changes;
 *   - the daemon writes the state of the gamepad to its own output report (`IOCTL_HID_WRITE_REPORT` or `IOCTL_UMDF_HID_SET_OUTPUT_REPORT`), the output report of the DualShock 4 is ignored;
 *   - the feature reports the games ask a DualShock 4 for are answered with fixed data (`IOCTL_UMDF_HID_GET_FEATURE`).
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

#define DS4_CALIBRATION_REPORT_ID 0x02 // Feature report of the calibration of the motion sensors
#define DS4_PAIRING_REPORT_ID     0x12 // Feature report of the MAC addresses of the controller and its host, only answered by USB (SDL knows the connection by it)
#define DS4_MAC_REPORT_ID         0x81 // Feature report of the MAC address of the controller

/**
 * @brief Report descriptor: the one of the DualShock 4, with only the feature reports the games ask for, and the output report of the daemon, see `protocol.h`.
 */
const UINT8 ReportDescriptor[] = {
	0x05, 0x01,             // Usage Page (Generic Desktop)
	0x09, 0x05,             // Usage (Game Pad)
	0xA1, 0x01,             // Collection (Application)

	// The input report of the DualShock 4
	0x85, PSPDISP_VHID_DS4_INPUT_REPORT_ID, // Report ID
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

	// The output report of the DualShock 4 (the rumble and the light bar)
	0x85, PSPDISP_VHID_DS4_OUTPUT_REPORT_ID, // Report ID
	0x09, 0x22,             //   Usage (0x22)
	0x95, PSPDISP_VHID_OUTPUT_REPORT_SIZE - 1, // Report Count
	0x91, 0x02,             //   Output (Data, Variable, Absolute)

	// The feature reports of the DualShock 4 the games ask for
	0x85, DS4_CALIBRATION_REPORT_ID, // Report ID
	0x09, 0x24,             //   Usage (0x24)
	0x95, 0x24,             //   Report Count (36)
	0xB1, 0x02,             //   Feature (Data, Variable, Absolute)
	0x85, DS4_PAIRING_REPORT_ID, // Report ID
	0x09, 0x25,             //   Usage (0x25)
	0x95, 0x0F,             //   Report Count (15)
	0xB1, 0x02,             //   Feature (Data, Variable, Absolute)
	0x85, DS4_MAC_REPORT_ID, // Report ID
	0x09, 0x26,             //   Usage (0x26)
	0x95, 0x06,             //   Report Count (6)
	0xB1, 0x02,             //   Feature (Data, Variable, Absolute)

	// The output report of the daemon, the state of the gamepad
	0x85, PSPDISP_VHID_CONTROL_REPORT_ID, // Report ID
	0x09, 0x50,             //   Usage (0x50)
	0x95, PSPDISP_VHID_OUTPUT_REPORT_SIZE - 1, // Report Count
	0x91, 0x02,             //   Output (Data, Variable, Absolute)
	0xC0,                   // End Collection
};

static_assert(sizeof(PSPdispVhidReport) == PSPDISP_VHID_OUTPUT_REPORT_SIZE, "The report of the daemon must have the size of the output reports");

/**
 * @brief HID descriptor, which gives the size of the report descriptor to the HID class.
 */
const HID_DESCRIPTOR HidDescriptor = {
	sizeof(HID_DESCRIPTOR), // bLength
	0x21,                   // bDescriptorType, HID
	0x0111,                 // bcdHID, HID 1.11 (as the DualShock 4)
	0x00,                   // bCountry, not localized
	0x01,                   // bNumDescriptors
	{ { 0x22, sizeof(ReportDescriptor) } }, // DescriptorList, the report descriptor
};

#pragma pack(push, 1)

/**
 * @brief Feature report of the calibration of the motion sensors (by USB), little-endian: the ranges of a real DualShock 4, so the games don't divide by 0 (the sensors stay at 0).
 */
typedef struct {
	UINT8 ReportId;      // `DS4_CALIBRATION_REPORT_ID`
	INT16 GyroBias[3];   // Pitch, yaw and roll
	INT16 GyroRange[6];  // Pitch plus and minus, yaw plus and minus, roll plus and minus
	INT16 GyroSpeed[2];  // Plus and minus
	INT16 AccelRange[6]; // X plus and minus, Y plus and minus, Z plus and minus
	UINT8 Reserved[2];
} DS4_CALIBRATION;

#pragma pack(pop)

static_assert(sizeof(DS4_CALIBRATION) == 37, "The calibration must have the size of its feature report");

const DS4_CALIBRATION Calibration = {
	DS4_CALIBRATION_REPORT_ID,
	{ 0, 0, 0 },
	{ 8192, -8192, 8192, -8192, 8192, -8192 },
	{ 540, 540 },
	{ 8192, -8192, 8192, -8192, 8192, -8192 },
	{ 0, 0 },
};

/**
 * @brief MAC address of the gamepad, least significant byte first as in the feature reports: 02:50:53:50:44:53, locally administered ("\x02PSPDS").
 */
const UINT8 MacAddress[6] = { 0x53, 0x44, 0x50, 0x53, 0x50, 0x02 };

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
	PSPdispVhidState State; // The state of the gamepad, the newest one written by the daemon
	bool Undelivered;       // If `State` changed since the last read request completed
	UINT8 Counter;          // The counter of the input reports, 6 bits
} DEVICE_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceCtx);

/**
 * @brief Fill the input report of the DualShock 4 with the state of the gamepad, and count it.
 *
 * @param ctx The device, locked exclusively.
 * @param report The report to fill, `PSPDISP_VHID_DS4_INPUT_REPORT_SIZE` bytes.
 */
void FillInputReport(DEVICE_CONTEXT *ctx, UINT8 *report) {
	const PSPdispVhidState *state = &ctx->State;
	LARGE_INTEGER now;

	// The timestamp, in units of 16/3 microseconds, wrapping on 16 bits
	QueryPerformanceCounter(&now);
	LONGLONG ticks = now.QuadPart - ctx->Start.QuadPart;
	ULONGLONG microseconds = (ULONGLONG) (ticks / ctx->Frequency.QuadPart) * 1000000 + (ULONGLONG) (ticks % ctx->Frequency.QuadPart) * 1000000 / ctx->Frequency.QuadPart;
	UINT16 timestamp = (UINT16) (microseconds * 3 / 16);

	memset(report, 0, PSPDISP_VHID_DS4_INPUT_REPORT_SIZE);
	report[0] = PSPDISP_VHID_DS4_INPUT_REPORT_ID;
	report[1] = state->lx;
	report[2] = state->ly;
	report[3] = state->rx;
	report[4] = state->ry;
	report[5] = (UINT8) ((state->hat & 0x0F) | (state->buttons & 0x0F) << 4); // The hat, then square, cross, circle and triangle
	report[6] = (UINT8) (state->buttons >> 4);                                 // L1, R1, L2, R2, share, options, L3 and R3
	report[7] = (UINT8) ((state->buttons >> 12 & 0x03) | ctx->Counter << 2);  // PS and the touchpad click, then the counter
	report[8] = state->l2;
	report[9] = state->r2;
	report[10] = (UINT8) timestamp;
	report[11] = (UINT8) (timestamp >> 8);
	report[30] = 0x1B; // The battery: plugged (0x10), and full (11)
	report[33] = 1;    // One packet of the touchpad
	report[35] = 0x80; // No first finger on the touchpad
	report[39] = 0x80; // No second finger

	ctx->Counter = (ctx->Counter + 1) & 0x3F;
}

/**
 * @brief Complete a read request with the input report of the gamepad.
 *
 * @param ctx The device, locked exclusively.
 * @param request The `IOCTL_HID_READ_REPORT` request.
 */
void CompleteRead(DEVICE_CONTEXT *ctx, WDFREQUEST request) {
	UINT8 *report;
	NTSTATUS s = WdfRequestRetrieveOutputBuffer(request, PSPDISP_VHID_DS4_INPUT_REPORT_SIZE, (PVOID *) &report, NULL);

	if (!NT_SUCCESS(s)) {
		WdfRequestComplete(request, s);
		return;
	}
	FillInputReport(ctx, report);
	ctx->Undelivered = false;
	WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, PSPDISP_VHID_DS4_INPUT_REPORT_SIZE);
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
 * @brief Take an output report: the state the daemon wrote, delivered to a pending read request, or the rumble and the light bar of the DualShock 4, ignored.
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
		case PSPDISP_VHID_CONTROL_REPORT_ID:
			if (size < 1 + sizeof(PSPdispVhidState)) {
				WdfRequestComplete(request, STATUS_INVALID_BUFFER_SIZE);
				return;
			}
			AcquireSRWLockExclusive(&ctx->Lock);
			memcpy(&ctx->State, &((const PSPdispVhidReport *) report)->state, sizeof(ctx->State));
			ctx->Undelivered = true;
			if (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(ctx->ReadQueue, &read)))
				CompleteRead(ctx, read);
			ReleaseSRWLockExclusive(&ctx->Lock);
			break;
		case PSPDISP_VHID_DS4_OUTPUT_REPORT_ID:
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
	UINT8 report[PSPDISP_VHID_DS4_INPUT_REPORT_SIZE];

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
	UINT8 report[16] = {0};
	NTSTATUS s = WdfRequestRetrieveInputBuffer(request, 1, (PVOID *) &id, NULL);

	if (!NT_SUCCESS(s)) {
		WdfRequestComplete(request, s);
		return;
	}

	switch (*id) {
		case DS4_CALIBRATION_REPORT_ID:
			CompleteWithData(request, &Calibration, sizeof(Calibration));
			break;
		case DS4_PAIRING_REPORT_ID:
			report[0] = DS4_PAIRING_REPORT_ID;
			memcpy(&report[1], MacAddress, sizeof(MacAddress)); // Then 3 unknown bytes, and the MAC address of the host, none
			report[7] = 0x08;
			report[8] = 0x25;
			CompleteWithData(request, report, 16);
			break;
		case DS4_MAC_REPORT_ID:
			report[0] = DS4_MAC_REPORT_ID;
			memcpy(&report[1], MacAddress, sizeof(MacAddress));
			CompleteWithData(request, report, 7);
			break;
		default:
			WdfRequestComplete(request, STATUS_INVALID_PARAMETER);
			break;
	}
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
	ctx->State.lx = ctx->State.ly = ctx->State.rx = ctx->State.ry = PSPDISP_VHID_STICK_CENTERED; // At rest, the rest of the state is 0
	ctx->State.hat = PSPDISP_VHID_HAT_CENTERED;
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
