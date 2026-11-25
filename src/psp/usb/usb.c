/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * usb.c - USB driver of the app                     *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * The USB driver of the app, the USB device the desktop talks to, with two bulk endpoints: one from the desktop (0x02) and one to it (0x81).
 * Its product IDs and bulk endpoints are the ones of USBHostFS of PSPLink, which the desktop expects.
 * It's the kernel module `usb.prx`, which the app loads from its folder, and which exports the functions the app calls (see `exports.exp` and `usb.h`).
 *
 * It's a function driver of the bus driver of the PSP (see `pspusbbus.h`), registered while the module is loaded, which sets up the device with the desktop from the descriptors of the driver.
 * The bus driver tells the driver when the desktop configured the device (attached), so the endpoints can transfer, and when it's detached (unplugged, or deactivated by the app).
 * It passes on the requests it doesn't answer itself, of which the driver answers the ones of Windows for its Microsoft OS descriptors, so Windows gives it WinUSB without a driver to install.
 *
 * Once attached, the data goes through the endpoints as it is.
 * Each transfer goes through a buffer, as the USB hardware reads and writes the memory without the cache of the CPU.
 * A transfer waits until the desktop takes or sends the data, or until the driver is detached (which the app does to stop a transfer).
 */

#include <stddef.h>
#include <string.h>
#include <pspkernel.h>
#include <pspsdk.h>
#include <pspusbbus.h>
#include "usb.h"

PSP_MODULE_INFO("PSPdispUSB", PSP_MODULE_KERNEL, 1, 0);

#define PSPDISP_USB_ENDPOINT_IN              0x81					  // Bulk endpoint 1, from the PSP to the desktop (IN, as USB names the directions from the host)
#define PSPDISP_USB_ENDPOINT_OUT             0x02					  // Bulk endpoint 2, from the desktop to the PSP (OUT)
#define PSPDISP_USB_ENDPOINT_NUMBER(address) ((address) & 0b00001111) // Number of an endpoint, the bits 3-0 of its address
#define PSPDISP_USB_BULK_ENDPOINT_COUNT      2						  // The bulk endpoints, `PSPDISP_USB_ENDPOINT_IN` and `PSPDISP_USB_ENDPOINT_OUT`
#define PSPDISP_USB_HIGH_SPEED_PACKET_SIZE   512					  // Biggest packet of a bulk endpoint at high speed (USB 2.0)
#define PSPDISP_USB_FULL_SPEED_PACKET_SIZE   64						  // Biggest packet of a bulk endpoint at full speed (USB 1.1)

#define PSPDISP_USB_REQUEST_GET_DESCRIPTOR             0x06       // Standard request of a descriptor (`bRequest`)
#define PSPDISP_USB_REQUEST_TO_HOST_STANDARD_DEVICE    0b10000000 // `bmRequestType` of a standard request to the device, with data to the host
#define PSPDISP_USB_REQUEST_TO_HOST_VENDOR_DEVICE      0b11000000 // `bmRequestType` of a vendor-specific request to the device, with data to the host
#define PSPDISP_USB_REQUEST_TO_HOST_VENDOR_INTERFACE   0b11000001 // `bmRequestType` of a vendor-specific request to an interface, with data to the host
#define PSPDISP_USB_MICROSOFT_OS_STRING_INDEX          0xEE       // String Windows asks for, to know if a device has Microsoft OS descriptors
#define PSPDISP_USB_MICROSOFT_VENDOR_CODE              0x01       // Vendor-specific request Windows asks for the Microsoft OS feature descriptors with (`bRequest`), any one the driver doesn't use otherwise
#define PSPDISP_USB_MICROSOFT_COMPATIBLE_ID_INDEX      0x0004     // Microsoft OS feature descriptor of the compatible IDs (`wIndex`)
#define PSPDISP_USB_MICROSOFT_PROPERTIES_INDEX         0x0005     // Microsoft OS feature descriptor of the properties (`wIndex`)
#define PSPDISP_USB_INTERFACE_GUID_NAME                u"DeviceInterfaceGUID"
#define PSPDISP_USB_INTERFACE_GUID                     u"{21EFA200-DE10-44A9-BB86-AD29A2FD1E74}" // Device interface of the PSP, which WinUSB applications can find it by

#define PSPDISP_USB_BUFFER_SIZE              (64 * 1024)         				// Size of the buffer of the transfers, bigger data goes in several transfers
#define PSPDISP_USB_CACHE_LINE_SIZE          64                  				// Size of a line of the data cache of the CPU
#define PSPDISP_USB_TRANSFER_DONE_EVENT      0b00000000000000000000000000000001	// Bit of the event flag of the transfers, set when the transfer is done

/**
 * @brief The Microsoft OS string (string `PSPDISP_USB_MICROSOFT_OS_STRING_INDEX`), which tells Windows the request to ask for the Microsoft OS feature descriptors with.
 */
typedef struct {
	u8 length; // Of the descriptor
	u8 type; // 0x03, a string
	u16 signature[7]; // "MSFT100", in UTF-16
	u8 vendor_code; // `PSPDISP_USB_MICROSOFT_VENDOR_CODE`
	u8 padding;
} __attribute__((packed)) PSPdispUSBMicrosoftOSString;

/**
 * @brief The Microsoft OS feature descriptor of the compatible IDs, which gives the interface the one of WinUSB, so Windows installs WinUSB for it.
 */
typedef struct {
	u32 length; // Of the descriptor
	u16 version; // 0x0100, 1.0
	u16 index; // `PSPDISP_USB_MICROSOFT_COMPATIBLE_ID_INDEX`
	u8 function_count; // The functions following the header, one
	u8 reserved[7];
	u8 first_interface; // Of the function
	u8 reserved_one; // 0x01
	char compatible_id[8]; // "WINUSB"
	char sub_compatible_id[8];
	u8 reserved_end[6];
} __attribute__((packed)) PSPdispUSBMicrosoftCompatibleID;

/**
 * @brief The Microsoft OS feature descriptor of the properties, which gives the device its device interface (`PSPDISP_USB_INTERFACE_GUID`), as Windows writes it in the registry.
 */
typedef struct {
	u32 length; // Of the descriptor
	u16 version; // 0x0100, 1.0
	u16 index; // `PSPDISP_USB_MICROSOFT_PROPERTIES_INDEX`
	u16 property_count; // The properties following the header, one
	u32 property_size; // Of the property, from here
	u32 property_type; // 1, a string (REG_SZ)
	u16 name_size; // In bytes
	u16 name[sizeof(PSPDISP_USB_INTERFACE_GUID_NAME) / sizeof(u16)]; // In UTF-16, with its null character
	u32 value_size; // In bytes
	u16 value[sizeof(PSPDISP_USB_INTERFACE_GUID) / sizeof(u16)]; // In UTF-16, with its null character
} __attribute__((packed)) PSPdispUSBMicrosoftProperties;

// The descriptors of the device, the same at both speeds, but the size of the packets of the endpoints (see `pspdisp_usb_data_fill()`)
const struct DeviceDescriptor pspdisp_usb_device_descriptor = {
	.bLength = sizeof(struct DeviceDescriptor),
	.bDescriptorType = 0x01, // Device
	.bcdUSB = 0x0200, // USB 2.0
	.bMaxPacketSize = 64, // Of the control endpoint
	.idVendor = 0, // Sony, given by the bus driver
	.idProduct = 0, // The one the app activates the driver with, given by the bus driver
	.bcdDevice = 0x0200, // Windows asks for the Microsoft OS descriptors once per vendor, product and release, so a release that changes them needs a new number
	.bNumConfigurations = 1,
};
const struct ConfigDescriptor pspdisp_usb_configuration_descriptor = {
	.bLength = sizeof(struct ConfigDescriptor),
	.bDescriptorType = 0x02, // Configuration
	.wTotalLength = sizeof(struct ConfigDescriptor) + sizeof(struct InterfaceDescriptor) + PSPDISP_USB_BULK_ENDPOINT_COUNT * sizeof(struct EndpointDescriptor), // With the descriptors under it
	.bNumInterfaces = 1,
	.bConfigurationValue = 1,
	.bmAttributes = 0b11000000, // Self-powered (with the bit USB reserves set)
	.bMaxPower = 0,
};
// TODO: have dedicated interfaces for each thing: a Audio interface, a HID interface, a custom Display interface, etc...
const struct InterfaceDescriptor pspdisp_usb_interface_descriptor = {
	.bLength = sizeof(struct InterfaceDescriptor),
	.bDescriptorType = 0x04, // Interface
	.bInterfaceNumber = 0,
	.bNumEndpoints = PSPDISP_USB_BULK_ENDPOINT_COUNT,
	.bInterfaceClass = 0xFF, // Vendor-specific
	.bInterfaceSubClass = 0x01,
	.bInterfaceProtocol = 0xFF,
	.iInterface = 1, // The first of `pspdisp_usb_string_descriptors`
};
// TODO: we have to rework the string descriptors of the USB!!
// The strings of the device, string 1 first, ending with an empty one, in case the bus driver reads them as a list
struct StringDescriptor pspdisp_usb_string_descriptors[] = {
	{
		.bLength = 8, // Its header, then "<>" and a null character in UTF-16
		.bDescriptorType = 0x03, // String
		.bString = { '<', '>' },
	},
	{ .bLength = 0 },
};

// The Microsoft OS descriptors, which make Windows install WinUSB for the PSP, sent by `pspdisp_usb_answer()`, so they start a line of the cache
const PSPdispUSBMicrosoftOSString pspdisp_usb_microsoft_os_string __attribute__((aligned(PSPDISP_USB_CACHE_LINE_SIZE))) = {
	.length = sizeof(PSPdispUSBMicrosoftOSString),
	.type = 0x03, // String
	.signature = { 'M', 'S', 'F', 'T', '1', '0', '0' },
	.vendor_code = PSPDISP_USB_MICROSOFT_VENDOR_CODE,
};
const PSPdispUSBMicrosoftCompatibleID pspdisp_usb_microsoft_compatible_id __attribute__((aligned(PSPDISP_USB_CACHE_LINE_SIZE))) = {
	.length = sizeof(PSPdispUSBMicrosoftCompatibleID),
	.version = 0x0100,
	.index = PSPDISP_USB_MICROSOFT_COMPATIBLE_ID_INDEX,
	.function_count = 1,
	.first_interface = 0,
	.reserved_one = 0x01,
	.compatible_id = "WINUSB",
};
const PSPdispUSBMicrosoftProperties pspdisp_usb_microsoft_properties __attribute__((aligned(PSPDISP_USB_CACHE_LINE_SIZE))) = {
	.length = sizeof(PSPdispUSBMicrosoftProperties),
	.version = 0x0100,
	.index = PSPDISP_USB_MICROSOFT_PROPERTIES_INDEX,
	.property_count = 1,
	.property_size = sizeof(PSPdispUSBMicrosoftProperties) - offsetof(PSPdispUSBMicrosoftProperties, property_size),
	.property_type = 1,
	.name_size = sizeof(PSPDISP_USB_INTERFACE_GUID_NAME),
	.name = PSPDISP_USB_INTERFACE_GUID_NAME,
	.value_size = sizeof(PSPDISP_USB_INTERFACE_GUID),
	.value = PSPDISP_USB_INTERFACE_GUID,
};

// The descriptors laid out as the bus driver reads them, at high speed and at full speed, filled by `pspdisp_usb_data_fill()`
// Packed, but the bus driver reads their pointers as words, so they start on a line of the cache, in case the USB hardware also reads them directly
struct UsbData pspdisp_usb_high_speed_data __attribute__((aligned(PSPDISP_USB_CACHE_LINE_SIZE)));
struct UsbData pspdisp_usb_full_speed_data __attribute__((aligned(PSPDISP_USB_CACHE_LINE_SIZE)));

// The endpoints by number, the control endpoint first, the transfers are queued on them
struct UsbEndpoint pspdisp_usb_endpoints[] = {
	{ .endpnum = 0 },
	{ .endpnum = PSPDISP_USB_ENDPOINT_NUMBER(PSPDISP_USB_ENDPOINT_IN) },
	{ .endpnum = PSPDISP_USB_ENDPOINT_NUMBER(PSPDISP_USB_ENDPOINT_OUT) },
};

// The interfaces of the driver, a single one
struct UsbInterface pspdisp_usb_interfaces = {
	.expect_interface = -1,
	.num_interface = 1,
};

SceUID pspdisp_usb_lock_id = -1; // Semaphore letting one read or write go at a time, while the driver is started
SceUID pspdisp_usb_transfer_event_id = -1; // Event flag of the transfers (`PSPDISP_USB_TRANSFER_DONE_EVENT`), while the driver is started
struct UsbdDeviceReq pspdisp_usb_transfer_request; // The transfer being done
struct UsbdDeviceReq pspdisp_usb_answer_request; // The answer to a request of the desktop on the control endpoint
u8 pspdisp_usb_buffer[PSPDISP_USB_BUFFER_SIZE] __attribute__((aligned(PSPDISP_USB_CACHE_LINE_SIZE))); // The data of the transfer, on whole lines of the cache, so dropping them from the cache drops nothing else
volatile bool pspdisp_usb_attached = false; // If the desktop configured the device, so its endpoints can transfer

/**
 * @brief Lay out the descriptors as the bus driver reads them, for a speed.
 *
 * @param data Where to lay them out.
 * @param max_packet_size The biggest packet of the bulk endpoints at that speed.
 */
void pspdisp_usb_data_fill(struct UsbData *data, u16 max_packet_size) {
	// Endpoint descriptors (type 0x05) of bulk endpoints (attributes 0x02)
	const struct EndpointDescriptor endpoint_descriptors[PSPDISP_USB_BULK_ENDPOINT_COUNT] = {
		{ .bLength = sizeof(struct EndpointDescriptor), .bDescriptorType = 0x05, .bEndpointAddress = PSPDISP_USB_ENDPOINT_IN, .bmAttributes = 0x02, .wMaxPacketSize = max_packet_size },
		{ .bLength = sizeof(struct EndpointDescriptor), .bDescriptorType = 0x05, .bEndpointAddress = PSPDISP_USB_ENDPOINT_OUT, .bmAttributes = 0x02, .wMaxPacketSize = max_packet_size },
	};

	memcpy(data->devdesc, &pspdisp_usb_device_descriptor, sizeof(pspdisp_usb_device_descriptor));
	memcpy(data->confdesc.desc, &pspdisp_usb_configuration_descriptor, sizeof(pspdisp_usb_configuration_descriptor));
	memcpy(data->interdesc.desc, &pspdisp_usb_interface_descriptor, sizeof(pspdisp_usb_interface_descriptor));
	for (int i = 0; i < PSPDISP_USB_BULK_ENDPOINT_COUNT; i++)
		memcpy(data->endp[i].desc, &endpoint_descriptors[i], sizeof(endpoint_descriptors[i]));

	// Each descriptor points to the ones under it
	data->config.pconfdesc = &data->confdesc;
	data->config.pinterfaces = &data->interfaces;
	data->config.pinterdesc = &data->interdesc;
	data->config.pendp = data->endp;
	data->confdesc.pinterfaces = &data->interfaces;
	data->interfaces.pinterdesc[0] = &data->interdesc;
	data->interfaces.intcount = 1;
	data->interdesc.pendp = data->endp;
}

/**
 * @brief Called by the bus driver once an answer on the control endpoint is sent, nothing to do.
 *
 * @return 0.
 */
int pspdisp_usb_on_answer_done(struct UsbdDeviceReq *, int, int) {
	return 0;
}

/**
 * @brief Answer a request of the desktop on the control endpoint, with as much of the data as it asked for.
 *
 * @param data The data, starting a line of the cache.
 * @param size The size of the data.
 * @param asked_size The size the desktop asked for.
 * @return 0, or -1 if it failed.
 */
int pspdisp_usb_answer(const void *data, u32 size, u16 asked_size) {
	sceKernelDcacheWritebackRange(data, size); // The USB hardware reads the memory, not the cache of the CPU

	pspdisp_usb_answer_request = (struct UsbdDeviceReq) {
		.endp = &pspdisp_usb_endpoints[0], // The control endpoint
		.data = (void *) data,
		.size = size < asked_size ? size : asked_size,
		.func = pspdisp_usb_on_answer_done,
	};
	return sceUsbbdReqSend(&pspdisp_usb_answer_request) < 0 ? -1 : 0;
}

/**
 * @brief Called by the bus driver for a request of the desktop on the control endpoint that it doesn't answer itself.
 *
 * The driver answers the requests of Windows for its Microsoft OS descriptors, and refuses the others.
 *
 * @param status Negative when the request can't be answered (what else it tells isn't known).
 * @param request The request.
 * @return 0 if the request is answered, or -1 if it's refused.
 */
int pspdisp_usb_on_control_request(int, int status, struct DeviceRequest *request) {
	if (status < 0) return -1;

	// The Microsoft OS string, a standard request of a string descriptor (its type, then its index)
	const u16 microsoft_os_string = 0x03 << 8 | PSPDISP_USB_MICROSOFT_OS_STRING_INDEX;
	if (request->bmRequestType == PSPDISP_USB_REQUEST_TO_HOST_STANDARD_DEVICE && request->bRequest == PSPDISP_USB_REQUEST_GET_DESCRIPTOR && request->wValue == microsoft_os_string)
		return pspdisp_usb_answer(&pspdisp_usb_microsoft_os_string, sizeof(pspdisp_usb_microsoft_os_string), request->wLength);

	// The Microsoft OS feature descriptors, asked for with the vendor code, to the device (or to the interface for the properties, depending on the version of Windows)
	const bool microsoft_feature = (request->bmRequestType == PSPDISP_USB_REQUEST_TO_HOST_VENDOR_DEVICE || request->bmRequestType == PSPDISP_USB_REQUEST_TO_HOST_VENDOR_INTERFACE) && request->bRequest == PSPDISP_USB_MICROSOFT_VENDOR_CODE;
	if (microsoft_feature && request->wIndex == PSPDISP_USB_MICROSOFT_COMPATIBLE_ID_INDEX)
		return pspdisp_usb_answer(&pspdisp_usb_microsoft_compatible_id, sizeof(pspdisp_usb_microsoft_compatible_id), request->wLength);
	if (microsoft_feature && request->wIndex == PSPDISP_USB_MICROSOFT_PROPERTIES_INDEX)
		return pspdisp_usb_answer(&pspdisp_usb_microsoft_properties, sizeof(pspdisp_usb_microsoft_properties), request->wLength);

	return -1;
}

/**
 * @brief Called by the bus driver for an unknown reason, nothing to do.
 *
 * @return 0.
 */
int pspdisp_usb_on_unknown(int, int, int) {
	return 0;
}

/**
 * @brief Called by the bus driver when the desktop configured the device, so its endpoints can transfer.
 *
 * @param speed The speed of the device, 1 for full speed, 2 for high speed.
 * @return 0.
 */
int pspdisp_usb_on_attach(int speed, void *, void *) {
	pspdisp_usb_attached = true;
	return 0;
}

/**
 * @brief Called by the bus driver when the device is detached from the desktop, which ends the transfer being done.
 *
 * @return 0.
 */
int pspdisp_usb_on_detach(int, int, int) {
	pspdisp_usb_attached = false;
	return 0;
}

/**
 * @brief Called by the bus driver when the app stops the driver, after it's deactivated, to delete what the transfers need.
 *
 * @return 0.
 */
int pspdisp_usb_on_stop(int size, void *arguments) {
	if (pspdisp_usb_lock_id >= 0)
		sceKernelDeleteSema(pspdisp_usb_lock_id);
	if (pspdisp_usb_transfer_event_id >= 0)
		sceKernelDeleteEventFlag(pspdisp_usb_transfer_event_id);

	pspdisp_usb_lock_id = -1;
	pspdisp_usb_transfer_event_id = -1;
	return 0;
}

/**
 * @brief Called by the bus driver when the app starts the driver, before it's activated, to create what the transfers need.
 *
 * @return 0, or -1 if it failed.
 */
int pspdisp_usb_on_start(int size, void *arguments) {
	pspdisp_usb_lock_id = sceKernelCreateSema("PSPdispUSBLock", 0, 1, 1, NULL);
	pspdisp_usb_transfer_event_id = sceKernelCreateEventFlag("PSPdispUSBTransfer", 0, 0, NULL);

	if (pspdisp_usb_lock_id < 0 || pspdisp_usb_transfer_event_id < 0) {
		pspdisp_usb_on_stop(size, arguments);
		return -1;
	}
	return 0;
}

// The driver, as the bus driver knows it
struct UsbDriver pspdisp_usb_driver = {
	.name = PSPDISP_USB_DRIVER_NAME,
	.endpoints = sizeof(pspdisp_usb_endpoints) / sizeof(pspdisp_usb_endpoints[0]),
	.endp = pspdisp_usb_endpoints,
	.intp = &pspdisp_usb_interfaces,
	.devp_hi = pspdisp_usb_high_speed_data.devdesc,
	.confp_hi = &pspdisp_usb_high_speed_data.config,
	.devp = pspdisp_usb_full_speed_data.devdesc,
	.confp = &pspdisp_usb_full_speed_data.config,
	.str = pspdisp_usb_string_descriptors,
	.recvctl = pspdisp_usb_on_control_request,
	.func28 = pspdisp_usb_on_unknown,
	.attach = pspdisp_usb_on_attach,
	.detach = pspdisp_usb_on_detach,
	.start_func = pspdisp_usb_on_start,
	.stop_func = pspdisp_usb_on_stop,
};

/**
 * @brief Called by the system once the module is loaded, to register the driver to the bus driver, so the app can start it.
 *
 * @return 0, or a negative error code if it failed.
 */
int module_start(SceSize size, void *arguments) {
	pspdisp_usb_data_fill(&pspdisp_usb_high_speed_data, PSPDISP_USB_HIGH_SPEED_PACKET_SIZE);
	pspdisp_usb_data_fill(&pspdisp_usb_full_speed_data, PSPDISP_USB_FULL_SPEED_PACKET_SIZE);
	return sceUsbbdRegister(&pspdisp_usb_driver);
}

/**
 * @brief Called by the system before the module is unloaded, once the app stopped the driver, to unregister it.
 *
 * @return 0.
 */
int module_stop(SceSize size, void *arguments) {
	sceUsbbdUnregister(&pspdisp_usb_driver);
	return 0;
}

/**
 * @brief Called by the bus driver when the transfer is done, from an interrupt, to wake up the one waiting for it.
 *
 * @param request The transfer.
 * @return 0.
 */
int pspdisp_usb_on_transfer_done(struct UsbdDeviceReq *request, int, int) {
	sceKernelSetEventFlag(pspdisp_usb_transfer_event_id, PSPDISP_USB_TRANSFER_DONE_EVENT);
	return 0;
}

/**
 * @brief Do a transfer with the start of the buffer, and wait until it's done.
 *
 * @param endpoint The endpoint, `PSPDISP_USB_ENDPOINT_IN` to send the data, `PSPDISP_USB_ENDPOINT_OUT` to receive it.
 * @param size The size to send, or the most to receive, more than 0 and at most `PSPDISP_USB_BUFFER_SIZE`.
 * @return The size transferred, or 0 if it failed.
 */
u32 pspdisp_usb_transfer(u8 endpoint, u32 size) {
	const bool receiving = endpoint == PSPDISP_USB_ENDPOINT_OUT;

	// The USB hardware reads and writes the memory, not the cache of the CPU: the cached lines of the data to receive are dropped, and the data to send is written back from the cache
	if (receiving) {
		const u32 lines_size = (size + PSPDISP_USB_CACHE_LINE_SIZE - 1) / PSPDISP_USB_CACHE_LINE_SIZE * PSPDISP_USB_CACHE_LINE_SIZE; // Whole lines, within the buffer
		sceKernelDcacheInvalidateRange(pspdisp_usb_buffer, lines_size);
	} else {
		sceKernelDcacheWritebackRange(pspdisp_usb_buffer, size);
	}

	pspdisp_usb_transfer_request = (struct UsbdDeviceReq) {
		.endp = &pspdisp_usb_endpoints[PSPDISP_USB_ENDPOINT_NUMBER(endpoint)],
		.data = pspdisp_usb_buffer,
		.size = size,
		.func = pspdisp_usb_on_transfer_done,
	};

	// Cleared first, in case a transfer nobody waited for set it (e.g. one cancelled after its wait failed)
	sceKernelClearEventFlag(pspdisp_usb_transfer_event_id, ~PSPDISP_USB_TRANSFER_DONE_EVENT);

	const int queued = receiving ? sceUsbbdReqRecv(&pspdisp_usb_transfer_request) : sceUsbbdReqSend(&pspdisp_usb_transfer_request);
	if (queued < 0) return 0;

	const int waited = sceKernelWaitEventFlag(pspdisp_usb_transfer_event_id, PSPDISP_USB_TRANSFER_DONE_EVENT, PSP_EVENT_WAITOR | PSP_EVENT_WAITCLEAR, NULL, NULL);
	if (waited < 0) return 0;

	// The return code is not 0 when the transfer was cancelled (e.g. the device was detached) or failed, and nothing transferred is a failure too, so the callers don't wait forever for the rest
	if (pspdisp_usb_transfer_request.retcode != 0 || pspdisp_usb_transfer_request.recvsize <= 0) return 0;

	return pspdisp_usb_transfer_request.recvsize;
}

/**
 * @brief Receive data from the desktop, all of it.
 *
 * @param data Where to write the data.
 * @param size The size of the data.
 * @return `true` if all of it was received, or `false` if it failed.
 */
bool pspdisp_usb_receive(void *data, u32 size) {
	for (u32 received = 0; received < size;) {
		const u32 remaining = size - received;
		const u32 transferred = pspdisp_usb_transfer(PSPDISP_USB_ENDPOINT_OUT, remaining < PSPDISP_USB_BUFFER_SIZE ? remaining : PSPDISP_USB_BUFFER_SIZE);
		if (transferred == 0) return false;

		memcpy((u8 *) data + received, pspdisp_usb_buffer, transferred);
		received += transferred;
	}
	return true;
}

/**
 * @brief Send data to the desktop, all of it.
 *
 * @param data The data.
 * @param size The size of the data.
 * @return `true` if all of it was sent, or `false` if it failed.
 */
bool pspdisp_usb_send(const void *data, u32 size) {
	for (u32 sent = 0; sent < size;) {
		const u32 remaining = size - sent;
		const u32 chunk_size = remaining < PSPDISP_USB_BUFFER_SIZE ? remaining : PSPDISP_USB_BUFFER_SIZE;
		memcpy(pspdisp_usb_buffer, (const u8 *) data + sent, chunk_size);

		const u32 transferred = pspdisp_usb_transfer(PSPDISP_USB_ENDPOINT_IN, chunk_size);
		if (transferred == 0) return false;
		sent += transferred;
	}
	return true;
}

bool pspdisp_usb_read(void *data, u32 size) {
	if (data == NULL) return false;

	// The kernel functions check the memory they're given against the mode of the caller (in k1), the app, so k1 is cleared while the driver gives its own
	const unsigned int k1 = pspSdkSetK1(0);
	bool read = false;

	if (sceKernelWaitSema(pspdisp_usb_lock_id, 1, NULL) >= 0) {
		read = pspdisp_usb_attached && pspdisp_usb_receive(data, size);
		sceKernelSignalSema(pspdisp_usb_lock_id, 1);
	}

	pspSdkSetK1(k1);
	return read;
}

bool pspdisp_usb_write(const void *data, u32 size) {
	if (data == NULL) return false;

	// See `pspdisp_usb_read()`
	const unsigned int k1 = pspSdkSetK1(0);
	bool written = false;

	if (sceKernelWaitSema(pspdisp_usb_lock_id, 1, NULL) >= 0) {
		written = pspdisp_usb_attached && pspdisp_usb_send(data, size);
		sceKernelSignalSema(pspdisp_usb_lock_id, 1);
	}

	pspSdkSetK1(k1);
	return written;
}
