/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * usb.c - USB driver of the app                     *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * The USB driver of the app, the USB device the desktop talks to, with two bulk endpoints: one from the desktop (0x02) and one to it (0x81).
 * Its product IDs, bulk endpoints and handshake are the ones of USBHostFS of PSPLink, which the desktop expects.
 * It's the kernel module `usb.prx`, which the app loads from its folder, and which exports the functions the app calls (see `exports.exp` and `usb.h`).
 *
 * It's a function driver of the bus driver of the PSP (see `pspusbbus.h`), registered while the module is loaded, which sets up the device with the desktop from the descriptors of the driver.
 * The bus driver tells the driver when the desktop configured the device (attached), so the endpoints can transfer, and when it's detached (unplugged, or deactivated by the app).
 *
 * Once attached, the first read or write does the handshake of the desktop (see `pspdisp_usb_handshake()`), then the data goes through the endpoints as it is.
 * Each transfer goes through a buffer, as the USB hardware reads and writes the memory without the cache of the CPU.
 * A transfer waits until the desktop takes or sends the data, or until the driver is detached (which the app does to stop a transfer).
 */

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

#define PSPDISP_USB_HANDSHAKE_MAGIC          0x782F0812 // Sent by the desktop to start the handshake, and first field of the hello (`HOSTFS_MAGIC` of USBHostFS)
#define PSPDISP_USB_HELLO_COMMAND            0x8FFC0000 // Command of the hello (`HOSTFS_CMD_HELLO` of USBHostFS)

#define PSPDISP_USB_BUFFER_SIZE              (64 * 1024)         				// Size of the buffer of the transfers, bigger data goes in several transfers
#define PSPDISP_USB_CACHE_LINE_SIZE          64                  				// Size of a line of the data cache of the CPU
#define PSPDISP_USB_TRANSFER_DONE_EVENT      0b00000000000000000000000000000001	// Bit of the event flag of the transfers, set when the transfer is done

/**
 * @brief The hello the PSP sends in the handshake, which the desktop sends back.
 */
typedef struct {
	u32 magic; // `PSPDISP_USB_HANDSHAKE_MAGIC`
	u32 command; // `PSPDISP_USB_HELLO_COMMAND`
	u32 extra_size; // Size of the data following it, none
} PSPdispUSBHello;

// The descriptors of the device, the same at both speeds, but the size of the packets of the endpoints (see `pspdisp_usb_data_fill()`)
const struct DeviceDescriptor pspdisp_usb_device_descriptor = {
	.bLength = sizeof(struct DeviceDescriptor),
	.bDescriptorType = 0x01, // Device
	.bcdUSB = 0x0200, // USB 2.0
	.bMaxPacketSize = 64, // Of the control endpoint
	.idVendor = 0, // Sony, given by the bus driver
	.idProduct = 0, // The one the app activates the driver with, given by the bus driver
	.bcdDevice = 0x0100,
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
const struct InterfaceDescriptor pspdisp_usb_interface_descriptor = {
	.bLength = sizeof(struct InterfaceDescriptor),
	.bDescriptorType = 0x04, // Interface
	.bInterfaceNumber = 0,
	.bNumEndpoints = PSPDISP_USB_BULK_ENDPOINT_COUNT,
	.bInterfaceClass = 0xFF, // Vendor-specific
	.bInterfaceSubClass = 0x01,
	.bInterfaceProtocol = 0xFF,
	.iInterface = 1, // `pspdisp_usb_string_descriptor`
};
struct StringDescriptor pspdisp_usb_string_descriptor = {
	.bLength = 8, // Its header, then "<>" and a null character in UTF-16
	.bDescriptorType = 0x03, // String
	.bString = { '<', '>' },
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
u8 pspdisp_usb_buffer[PSPDISP_USB_BUFFER_SIZE] __attribute__((aligned(PSPDISP_USB_CACHE_LINE_SIZE))); // The data of the transfer, on whole lines of the cache, so dropping them from the cache drops nothing else
volatile bool pspdisp_usb_attached = false; // If the desktop configured the device, so its endpoints can transfer
volatile bool pspdisp_usb_connected = false; // If the handshake was done since the device was attached

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
 * @brief Called by the bus driver for a request of the desktop on the control endpoint that it doesn't answer itself, which the driver ignores.
 *
 * @param request The request.
 * @return 0.
 */
int pspdisp_usb_on_control_request(int, int, struct DeviceRequest *request) {
	return 0;
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
	pspdisp_usb_connected = false; // Each configuration starts with a handshake
	return 0;
}

/**
 * @brief Called by the bus driver when the device is detached from the desktop, which ends the transfer being done.
 *
 * @return 0.
 */
int pspdisp_usb_on_detach(int, int, int) {
	pspdisp_usb_attached = false;
	pspdisp_usb_connected = false;
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
	.str = &pspdisp_usb_string_descriptor,
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

/**
 * @brief Do the handshake of the desktop, which starts its connection.
 *
 * The desktop sends `PSPDISP_USB_HANDSHAKE_MAGIC`, then the PSP sends its hello, which the desktop sends back.
 *
 * @return `true` if the desktop is connected, or `false` if it failed.
 */
bool pspdisp_usb_handshake() {
	u32 magic = 0;
	if (!pspdisp_usb_receive(&magic, sizeof(magic)) || magic != PSPDISP_USB_HANDSHAKE_MAGIC) return false;

	const PSPdispUSBHello hello = { .magic = PSPDISP_USB_HANDSHAKE_MAGIC, .command = PSPDISP_USB_HELLO_COMMAND };
	if (!pspdisp_usb_send(&hello, sizeof(hello))) return false;

	PSPdispUSBHello answer = {0};
	return pspdisp_usb_receive(&answer, sizeof(answer)) && answer.magic == hello.magic && answer.command == hello.command;
}

/**
 * @brief Connect the desktop if it's not, once the device is attached.
 *
 * @return `true` if the desktop is connected, or `false` if it failed.
 */
bool pspdisp_usb_connect() {
	if (!pspdisp_usb_attached) return false;

	if (!pspdisp_usb_connected)
		pspdisp_usb_connected = pspdisp_usb_handshake();
	return pspdisp_usb_connected;
}

bool pspdisp_usb_read(void *data, u32 size) {
	if (data == NULL) return false;

	// The kernel functions check the memory they're given against the mode of the caller (in k1), the app, so k1 is cleared while the driver gives its own
	const unsigned int k1 = pspSdkSetK1(0);
	bool read = false;

	if (sceKernelWaitSema(pspdisp_usb_lock_id, 1, NULL) >= 0) {
		read = pspdisp_usb_connect() && pspdisp_usb_receive(data, size);
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
		written = pspdisp_usb_connect() && pspdisp_usb_send(data, size);
		sceKernelSignalSema(pspdisp_usb_lock_id, 1);
	}

	pspSdkSetK1(k1);
	return written;
}
