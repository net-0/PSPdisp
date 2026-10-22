/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * driver.c - PSPdisp IddCx driver (Windows)         *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * Indirect display driver (UMDF 2 with IddCx) of the PSPdisp virtual display, based on `iddcx_virtual_monitor.basic_example.c`.
 *
 * Windows loads it in Session 0, inside its UMDF host process, so the display connector (`../iddcx.c`) talks to it through IOCTLs (see `protocol.h`):
 *   IOCTL_PSPDISP_IDDCX_CONNECT    -> IddCxMonitorCreate + IddCxMonitorArrival, with the EDID of the connector
 *   IOCTL_PSPDISP_IDDCX_WAIT          -> kept on a manual queue, completed by the next power change or asked frame
 *   IOCTL_PSPDISP_IDDCX_REQUEST_FRAME -> marks a frame as wanted, the grabber thread completes the pending wait request with it
 *   IOCTL_PSPDISP_IDDCX_DISCONNECT    -> IddCxMonitorDeparture (also when a wait request is cancelled, e.g. the connector process ended)
 *
 * When Windows assigns a swap chain to the monitor, a grabber thread drains its frames (required, or the OS stalls) and keeps a GPU copy of the newest one.
 * It copies that frame into the pending wait request only when the connector asked for a frame, so the frames nobody wants are never copied to the CPU.
 * The modes are the PSP resolution and it scaled up 2 or 4 times (from the EDID), whose frames the GPU scales down to the PSP resolution with their mip levels, so the frames sent are always 480x272.
 * Windows unassigns the swap chain when the monitor is turned off, which is sent as the DPMS off power mode (and on when assigned again).
 *
 * Build it with the WDK, see `BUILDING.md`, COM (D3D11 and DXGI) is used through the C vtable macros enabled by COBJMACROS.
 */

#define COBJMACROS
#include <windows.h>
#include <bugcodes.h>
#include <wudfwdm.h>
#include <wdf.h>
#include <dxgi1_5.h>
#include <d3d11_2.h>
#include <iddcx.h>
#include <avrt.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include "protocol.h"

#define SAFE_RELEASE(p) do { if (p) { (p)->lpVtbl->Release(p); (p) = NULL; } } while (0)

const GUID pspdisp_iddcx_interface_guid = PSPDISP_IDDCX_INTERFACE_GUID;

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
/* Contexts                                                                  */
/* ------------------------------------------------------------------------- */

typedef struct FRAME_GRABBER FRAME_GRABBER;

/**
 * @brief State of the device, shared by the IOCTLs, the IddCx callbacks and the grabber thread.
 */
typedef struct DEVICE_CONTEXT {
	IDDCX_ADAPTER Adapter;                     // NULL until the adapter is initialized
	IDDCX_MONITOR Monitor;                     // NULL while no monitor is connected
	UINT8 Edid[PSPDISP_IDDCX_EDID_SIZE];       // EDID of the connected monitor, from the connector
	WDFQUEUE WaitQueue;                        // Manual queue of the pending wait requests
	HANDLE RequestEvent;                       // Signaled when a wait request is queued or a frame is asked, so the grabber thread can deliver the newest frame
	SRWLOCK Lock;                              // Guards the fields below and `Monitor`
	bool FrameWanted;                          // If the connector asked for a frame not delivered yet (`IOCTL_PSPDISP_IDDCX_REQUEST_FRAME`)
	bool DpmsPending;                          // If a power change waits for a wait request
	UINT32 DpmsMode;                           // The power change, `PSPDISP_IDDCX_DPMS_ON` or `PSPDISP_IDDCX_DPMS_OFF`
} DEVICE_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceCtx);

/**
 * @brief State of the adapter, to find the device from the IddCx callbacks.
 */
typedef struct ADAPTER_CONTEXT {
	DEVICE_CONTEXT *Device;
} ADAPTER_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(ADAPTER_CONTEXT, GetAdapterCtx);

/**
 * @brief State of the monitor, attached to the IDDCX_MONITOR object.
 */
typedef struct MONITOR_CONTEXT {
	DEVICE_CONTEXT *Device;
	FRAME_GRABBER *Grabber; // NULL while no swap chain is assigned
} MONITOR_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(MONITOR_CONTEXT, GetMonitorCtx);

/* ------------------------------------------------------------------------- */
/* Events for the connector                                                  */
/* ------------------------------------------------------------------------- */

/**
 * @brief Send a power change to the connector: complete a pending wait request, or keep it for the next one.
 *
 * @param ctx The device.
 * @param mode The power mode (`PSPDISP_IDDCX_DPMS_*`).
 */
void SendDpms(DEVICE_CONTEXT *ctx, UINT32 mode) {
	WDFREQUEST request;
	PSPdispIddCxEvent *event;

	AcquireSRWLockExclusive(&ctx->Lock);
	if (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(ctx->WaitQueue, &request))) {
		if (NT_SUCCESS(WdfRequestRetrieveOutputBuffer(request, sizeof(*event), (PVOID *) &event, NULL))) {
			event->type = PSPDISP_IDDCX_EVENT_DPMS;
			event->dpms_mode = mode;
			WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, FIELD_OFFSET(PSPdispIddCxEvent, pixels));
		} else {
			WdfRequestComplete(request, STATUS_BUFFER_TOO_SMALL);
		}
		ctx->DpmsPending = false;
	} else {
		ctx->DpmsPending = true;
		ctx->DpmsMode = mode;
	}
	ReleaseSRWLockExclusive(&ctx->Lock);
}

/**
 * @brief Disconnect the monitor, it disappears from Windows (IddCx unassigns its swap chain first).
 *
 * @param ctx The device.
 */
void DisconnectMonitor(DEVICE_CONTEXT *ctx) {
	IDDCX_MONITOR monitor;

	AcquireSRWLockExclusive(&ctx->Lock);
	monitor = ctx->Monitor;
	ctx->Monitor = NULL;
	ReleaseSRWLockExclusive(&ctx->Lock);

	if (monitor != NULL) {
		NTSTATUS s = IddCxMonitorDeparture(monitor);
		if (!NT_SUCCESS(s))
			Log(L"[PSPdisp] IddCxMonitorDeparture failed 0x%08X\n", s);
	}
}

/* ------------------------------------------------------------------------- */
/* Modes, from the detailed timings of the EDID                              */
/* ------------------------------------------------------------------------- */

#define MAX_MODES 4 // Modes of the monitor at most, one per descriptor of the EDID
#define MAX_LEVEL 3 // Mip levels at most from a mode down to the PSP resolution, so a mode is up to 8 times the PSP resolution

typedef struct MODE {
	UINT32 Width, Height;             // Active pixels and lines
	UINT32 TotalWidth, TotalHeight;   // With the blanking
	UINT64 PixelClock;                // In Hz
} MODE;

/**
 * @brief Tell the mip level of the PSP resolution in a frame of a size, the number of times the GPU halves the frame down to it.
 *
 * @param width The width in pixels.
 * @param height The height in lines.
 * @return The mip level (0 for the PSP resolution, 1 for twice it, 2 for 4 times), or -1 for a size that isn't the PSP resolution scaled up by a power of 2.
 */
int PspLevel(UINT32 width, UINT32 height) {
	for (int level = 0; level <= MAX_LEVEL; level++) {
		if (width == ((UINT32) PSPDISP_IDDCX_WIDTH << level) && height == ((UINT32) PSPDISP_IDDCX_HEIGHT << level)) return level;
	}
	return -1;
}

/**
 * @brief Read the modes of the monitor from the detailed timings of its EDID whose frames can be scaled down to the PSP resolution (see `PspLevel()`), or the PSP mode if it has none.
 *
 * @param edid The EDID, `PSPDISP_IDDCX_EDID_SIZE` bytes.
 * @param modes Where to write the modes, `MAX_MODES` at most, in the order of the EDID (so the first is the preferred one).
 * @return The number of modes, at least 1.
 */
UINT32 ModesFromEdid(const UINT8 *edid, MODE *modes) {
	UINT32 count = 0;

	// The 4 descriptors of 18 bytes from byte 54
	for (UINT32 i = 0; i < 4; i++) {
		const UINT8 *t = edid + 54 + i * 18;
		UINT32 clock = t[0] | (t[1] << 8); // In units of 10 kHz, 0 marks a display descriptor
		MODE m;

		m.Width       = t[2] | ((t[4] >> 4) << 8);
		m.TotalWidth  = m.Width + (t[3] | ((t[4] & 0x0F) << 8));
		m.Height      = t[5] | ((t[7] >> 4) << 8);
		m.TotalHeight = m.Height + (t[6] | ((t[7] & 0x0F) << 8));
		m.PixelClock  = (UINT64) clock * 10000;
		if (clock != 0 && PspLevel(m.Width, m.Height) >= 0)
			modes[count++] = m;
	}

	if (count == 0) {
		modes[0].Width = PSPDISP_IDDCX_WIDTH;
		modes[0].Height = PSPDISP_IDDCX_HEIGHT;
		modes[0].TotalWidth = 525;
		modes[0].TotalHeight = 286;
		modes[0].PixelClock = 9000000; // 59.94 Hz, as the PSP LCD
		count = 1;
	}
	return count;
}

/**
 * @brief Fill the video signal of a mode, for a monitor mode or for a target mode.
 *
 * @param s The video signal to fill.
 * @param m The mode.
 * @param monitorMode If it's for a monitor mode, otherwise for a target mode (their vertical sync dividers differ).
 */
void FillSignalInfo(DISPLAYCONFIG_VIDEO_SIGNAL_INFO *s, const MODE *m, BOOL monitorMode) {
	s->activeSize.cx = m->Width;
	s->activeSize.cy = m->Height;
	s->totalSize.cx = m->TotalWidth;
	s->totalSize.cy = m->TotalHeight;
	s->AdditionalSignalInfo.vSyncFreqDivider = monitorMode ? 0 : 1;
	s->AdditionalSignalInfo.videoStandard = 255;
	s->vSyncFreq.Numerator   = (UINT32) m->PixelClock;
	s->vSyncFreq.Denominator = m->TotalWidth * m->TotalHeight;
	s->hSyncFreq.Numerator   = (UINT32) m->PixelClock;
	s->hSyncFreq.Denominator = m->TotalWidth;
	s->scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
	s->pixelRate = m->PixelClock;
}

/**
 * @brief Make a monitor mode from a mode, as the EDID describes it.
 *
 * @param m The mode.
 * @return The monitor mode.
 */
IDDCX_MONITOR_MODE MakeMonitorMode(const MODE *m) {
	IDDCX_MONITOR_MODE mm = {0};
	mm.Size = sizeof(mm);
	mm.Origin = IDDCX_MONITOR_MODE_ORIGIN_MONITORDESCRIPTOR;
	FillSignalInfo(&mm.MonitorVideoSignalInfo, m, TRUE);
	return mm;
}

/**
 * @brief Make a target mode from a mode, a mode Windows may set on the monitor.
 *
 * @param m The mode.
 * @return The target mode.
 */
IDDCX_TARGET_MODE MakeTargetMode(const MODE *m) {
	IDDCX_TARGET_MODE tm = {0};
	tm.Size = sizeof(tm);
	FillSignalInfo(&tm.TargetVideoSignalInfo.targetVideoSignalInfo, m, FALSE);
	return tm;
}

/* ------------------------------------------------------------------------- */
/* Frame grabber: owns the swap chain for one assignment                     */
/* ------------------------------------------------------------------------- */

struct FRAME_GRABBER {
	DEVICE_CONTEXT *Device;
	IDDCX_SWAPCHAIN SwapChain;
	HANDLE NewFrameEvent;
	LUID RenderAdapter;
	HANDLE StopEvent;
	HANDLE Thread;
};

/**
 * @brief Textures of the frames, made for their size and format (see `RecreateTextures()`).
 */
typedef struct FRAME_TEXTURES {
	D3D11_TEXTURE2D_DESC Desc;      // Description of the frames
	UINT Level;                     // Mip level of `Latest` with the PSP resolution, 0 when the frames have it
	ID3D11Texture2D *Latest;        // GPU copy of the newest frame, in its first mip level, with the mip levels down to the PSP resolution
	ID3D11ShaderResourceView *Mips; // View of `Latest` that `GenerateMips()` makes the mip levels from (NULL when `Level` is 0)
	ID3D11Texture2D *Staging;       // CPU-readable copy of the mip level with the PSP resolution
} FRAME_TEXTURES;

/**
 * @brief Release the textures of the frames.
 *
 * @param t The textures.
 */
void ReleaseTextures(FRAME_TEXTURES *t) {
	SAFE_RELEASE(t->Mips);
	SAFE_RELEASE(t->Staging);
	SAFE_RELEASE(t->Latest);
}

/**
 * @brief Copy the newest frame, scaled down to the PSP resolution, into a pending wait request, if the connector asked for a frame and there's a wait request.
 *
 * @param g The grabber.
 * @param ctx The device context of D3D11.
 * @param t The textures of the frames, with the newest frame.
 * @return 1 if the frame was delivered, 0 if nobody asked or waits for it (it stays the newest one), -1 if it failed.
 */
int DeliverFrame(FRAME_GRABBER *g, ID3D11DeviceContext *ctx, const FRAME_TEXTURES *t) {
	WDFREQUEST request;
	PSPdispIddCxEvent *event;
	D3D11_MAPPED_SUBRESOURCE mapped = {0};
	UINT32 width = min(t->Desc.Width >> t->Level, PSPDISP_IDDCX_WIDTH);
	UINT32 height = min(t->Desc.Height >> t->Level, PSPDISP_IDDCX_HEIGHT);
	UINT32 y;

	AcquireSRWLockExclusive(&g->Device->Lock);
	if (!g->Device->FrameWanted || !NT_SUCCESS(WdfIoQueueRetrieveNextRequest(g->Device->WaitQueue, &request))) {
		ReleaseSRWLockExclusive(&g->Device->Lock);
		return 0;
	}
	g->Device->FrameWanted = false; // Answered by this request
	ReleaseSRWLockExclusive(&g->Device->Lock);

	if (!NT_SUCCESS(WdfRequestRetrieveOutputBuffer(request, sizeof(*event), (PVOID *) &event, NULL))) {
		WdfRequestComplete(request, STATUS_BUFFER_TOO_SMALL);
		return 0;
	}

	// The mip levels down to the PSP resolution, each the previous one halved by the GPU (a 2x2 average in practice, D3D11 doesn't define the filter), only for the frames delivered
	if (t->Mips)
		ID3D11DeviceContext_GenerateMips(ctx, t->Mips);
	ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *) t->Staging, 0, 0, 0, 0, (ID3D11Resource *) t->Latest, t->Level, NULL);

	if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *) t->Staging, 0, D3D11_MAP_READ, 0, &mapped))) {
		AcquireSRWLockExclusive(&g->Device->Lock);
		WdfRequestRequeue(request); // Kept for the next frame
		g->Device->FrameWanted = true; // Still wanted, nothing was delivered
		ReleaseSRWLockExclusive(&g->Device->Lock);
		return -1;
	}

	event->type = PSPDISP_IDDCX_EVENT_FRAME;
	event->width = width;
	event->height = height;
	for (y = 0; y < height; y++) {
		memcpy(event->pixels + y * PSPDISP_IDDCX_WIDTH, (const UINT8 *) mapped.pData + y * mapped.RowPitch, width * 4);
	}
	ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *) t->Staging, 0);

	WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, sizeof(*event));
	return 1;
}

/**
 * @brief Create the textures of the frames again, for a new size or format.
 *
 * A frame of the PSP resolution scaled up by a power of 2 gets the mip levels down to the PSP resolution, the frames of another size are cut to it.
 *
 * @param device The D3D11 device.
 * @param d The description of the frames.
 * @param t The textures, released first.
 * @return `TRUE` if they were created, `FALSE` if it failed.
 */
BOOL RecreateTextures(ID3D11Device *device, const D3D11_TEXTURE2D_DESC *d, FRAME_TEXTURES *t) {
	D3D11_TEXTURE2D_DESC ld = {0};
	D3D11_TEXTURE2D_DESC sd;
	UINT support = 0;
	int level = PspLevel(d->Width, d->Height);

	ReleaseTextures(t);
	t->Desc = *d;

	// Another size than the modes, or a format the GPU makes no mip levels for: only the top left corner of the frames is sent
	if (level > 0 && (FAILED(ID3D11Device_CheckFormatSupport(device, d->Format, &support)) || !(support & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN)))
		level = -1;
	if (level < 0) {
		Log(L"[PSPdisp] frames of %ux%u format %u can't be scaled down, only their top left corner is sent\n", d->Width, d->Height, d->Format);
		level = 0;
	}
	t->Level = (UINT) level;

	ld.Width = d->Width;
	ld.Height = d->Height;
	ld.MipLevels = t->Level + 1;
	ld.ArraySize = 1;
	ld.Format = d->Format;
	ld.SampleDesc.Count = 1;
	ld.Usage = D3D11_USAGE_DEFAULT;
	if (t->Level > 0) {
		ld.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE; // Both needed by `GenerateMips()`
		ld.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
	}

	// The CPU reads the last mip level only, the PSP resolution
	sd = ld;
	sd.Width = d->Width >> t->Level;
	sd.Height = d->Height >> t->Level;
	sd.MipLevels = 1;
	sd.Usage = D3D11_USAGE_STAGING;
	sd.BindFlags = 0;
	sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	sd.MiscFlags = 0;

	if (FAILED(ID3D11Device_CreateTexture2D(device, &ld, NULL, &t->Latest))
	 || FAILED(ID3D11Device_CreateTexture2D(device, &sd, NULL, &t->Staging))
	 || (t->Level > 0 && FAILED(ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource *) t->Latest, NULL, &t->Mips)))) {
		Log(L"[PSPdisp] texture creation failed\n");
		return FALSE;
	}
	Log(L"[PSPdisp] mode %ux%u format %u, %u mip levels\n", d->Width, d->Height, d->Format, ld.MipLevels);
	return TRUE;
}

/**
 * @brief Drain the frames of the swap chain and keep a GPU copy of the newest one, delivered when the connector asks for it, until it's stopped or the swap chain fails.
 *
 * @param g The grabber.
 */
void GrabberRunCore(FRAME_GRABBER *g) {
	HRESULT hr;
	IDXGIFactory5 *factory = NULL;
	IDXGIAdapter1 *adapter = NULL;
	ID3D11Device *device = NULL;
	ID3D11DeviceContext *ctx = NULL;
	IDXGIDevice *dxgiDevice = NULL;
	FRAME_TEXTURES textures = {0};   // The textures of the frames, made with the first frame
	BOOL undelivered = FALSE;        // If `textures.Latest` holds a frame not delivered yet
	IDARG_IN_SWAPCHAINSETDEVICE setDevice = {0};

	// D3D11 device on the adapter Windows renders this monitor with
	if (FAILED(hr = CreateDXGIFactory2(0, &IID_IDXGIFactory5, (void **) &factory))
	 || FAILED(hr = IDXGIFactory5_EnumAdapterByLuid(factory, g->RenderAdapter, &IID_IDXGIAdapter1, (void **) &adapter))
	 || FAILED(hr = D3D11CreateDevice((IDXGIAdapter *) adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &device, NULL, &ctx))
	 || FAILED(hr = ID3D11Device_QueryInterface(device, &IID_IDXGIDevice, (void **) &dxgiDevice))) {
		Log(L"[PSPdisp] D3D11 device creation failed 0x%08X\n", hr);
		goto cleanup;
	}

	setDevice.pDevice = dxgiDevice;
	if (FAILED(hr = IddCxSwapChainSetDevice(g->SwapChain, &setDevice))) {
		Log(L"[PSPdisp] IddCxSwapChainSetDevice failed 0x%08X\n", hr);
		goto cleanup;
	}

	for (;;) {
		IDARG_OUT_RELEASEANDACQUIREBUFFER buffer = {0};
		hr = IddCxSwapChainReleaseAndAcquireBuffer(g->SwapChain, &buffer);

		if (hr == E_PENDING) {
			// No new frame: wait for one, for a wait or frame request (to deliver the newest frame) or for stop
			HANDLE waits[3] = { g->NewFrameEvent, g->Device->RequestEvent, g->StopEvent };
			DWORD w = WaitForMultipleObjects(3, waits, FALSE, INFINITE);

			if (w == WAIT_OBJECT_0 + 1 && undelivered) {
				int delivered = DeliverFrame(g, ctx, &textures);
				if (delivered < 0) break;
				if (delivered > 0)
					undelivered = FALSE;
			}
			if (w == WAIT_OBJECT_0 || w == WAIT_OBJECT_0 + 1) continue;
			break; // Stop requested or wait error
		}
		if (FAILED(hr)) break; // Swap chain abandoned or device lost

		{
			// IddCx owns this reference, release it before FinishedProcessingFrame
			IDXGIResource *surface = buffer.MetaData.pSurface;
			ID3D11Texture2D *tex = NULL;
			BOOL fatal = FALSE;

			if (SUCCEEDED(IDXGIResource_QueryInterface(surface, &IID_ID3D11Texture2D, (void **) &tex))) {
				D3D11_TEXTURE2D_DESC d;
				ID3D11Texture2D_GetDesc(tex, &d);

				if (d.Format != DXGI_FORMAT_B8G8R8A8_UNORM && d.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
					Log(L"[PSPdisp] unsupported format %u, skipping\n", d.Format);
				} else {
					if (!textures.Latest || d.Width != textures.Desc.Width || d.Height != textures.Desc.Height || d.Format != textures.Desc.Format) {
						if (!RecreateTextures(device, &d, &textures))
							fatal = TRUE;
					}
					if (!fatal) {
						ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *) textures.Latest, 0, 0, 0, 0, (ID3D11Resource *) tex, 0, NULL); // Cheap GPU copy, into the first mip level
						undelivered = TRUE;
					}
				}
				SAFE_RELEASE(tex);
			}
			SAFE_RELEASE(surface);

			if (fatal) break;
		}

		if (FAILED(IddCxSwapChainFinishedProcessingFrame(g->SwapChain))) break;

		// Copy to the CPU outside the acquire/finish window, so Windows isn't held up
		if (undelivered) {
			int delivered = DeliverFrame(g, ctx, &textures);
			if (delivered < 0) break;
			if (delivered > 0)
				undelivered = FALSE; // Otherwise delivered when the connector asks for it (and its wait request is pending)
		}
	}

cleanup:
	ReleaseTextures(&textures);
	SAFE_RELEASE(dxgiDevice);
	SAFE_RELEASE(ctx);
	SAFE_RELEASE(device);
	SAFE_RELEASE(adapter);
	SAFE_RELEASE(factory);
}

/**
 * @brief Body of the grabber thread: run the grabber with a multimedia priority, then delete the swap chain, so Windows assigns a new one after an error.
 *
 * @param param The grabber (`FRAME_GRABBER *`).
 * @return Always 0.
 */
DWORD WINAPI GrabberThread(LPVOID param) {
	FRAME_GRABBER *g = (FRAME_GRABBER *) param;
	DWORD taskIndex = 0;
	HANDLE avrt = AvSetMmThreadCharacteristicsW(L"Distribution", &taskIndex);

	GrabberRunCore(g);

	if (avrt)
		AvRevertMmThreadCharacteristics(avrt);

	// Deleting the swap chain tells Windows we're done with it, after an error (e.g. device lost) it assigns a new one
	WdfObjectDelete((WDFOBJECT) g->SwapChain);
	return 0;
}

/**
 * @brief Start a grabber on the swap chain assigned to the monitor.
 *
 * @param device The device.
 * @param swapChain The swap chain, deleted if it failed.
 * @param newFrameEvent The event Windows signals when a new frame is available.
 * @param renderAdapter The adapter Windows renders the monitor with.
 * @return The grabber, stop it with `GrabberDestroy()`, or `NULL` if it failed.
 */
FRAME_GRABBER *GrabberCreate(DEVICE_CONTEXT *device, IDDCX_SWAPCHAIN swapChain, HANDLE newFrameEvent, LUID renderAdapter) {
	FRAME_GRABBER *g = (FRAME_GRABBER *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*g));
	if (!g) return NULL;

	g->Device = device;
	g->SwapChain = swapChain;
	g->NewFrameEvent = newFrameEvent;
	g->RenderAdapter = renderAdapter;
	g->StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	g->Thread = CreateThread(NULL, 0, GrabberThread, g, 0, NULL);

	if (!g->StopEvent || !g->Thread) {
		Log(L"[PSPdisp] grabber thread creation failed\n");
		if (g->StopEvent)
			CloseHandle(g->StopEvent);
		HeapFree(GetProcessHeap(), 0, g);
		WdfObjectDelete((WDFOBJECT) swapChain);
		return NULL;
	}
	return g;
}

/**
 * @brief Stop a grabber and free it, waiting for its thread.
 *
 * @param g The grabber (`NULL` for none).
 */
void GrabberDestroy(FRAME_GRABBER *g) {
	if (!g) return;
	SetEvent(g->StopEvent);
	WaitForSingleObject(g->Thread, INFINITE);
	CloseHandle(g->Thread);
	CloseHandle(g->StopEvent);
	HeapFree(GetProcessHeap(), 0, g);
}

/**
 * @brief Stop the grabber of a monitor when it's deleted, the cleanup of its context.
 *
 * @param obj The monitor.
 */
VOID EvtMonitorCleanup(WDFOBJECT obj) {
	MONITOR_CONTEXT *mc = GetMonitorCtx(obj);
	GrabberDestroy(mc->Grabber);
	mc->Grabber = NULL;
}

/* ------------------------------------------------------------------------- */
/* Callbacks                                                                 */
/* ------------------------------------------------------------------------- */

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD EvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP EvtDeviceCleanup;
EVT_WDF_DEVICE_D0_ENTRY EvtD0Entry;
EVT_WDF_IO_QUEUE_IO_CANCELED_ON_QUEUE EvtWaitCanceled;
EVT_IDD_CX_DEVICE_IO_CONTROL EvtIoControl;
EVT_IDD_CX_ADAPTER_INIT_FINISHED EvtAdapterInitFinished;
EVT_IDD_CX_ADAPTER_COMMIT_MODES EvtCommitModes;
EVT_IDD_CX_PARSE_MONITOR_DESCRIPTION EvtParseMonitorDescription;
EVT_IDD_CX_MONITOR_GET_DEFAULT_DESCRIPTION_MODES EvtGetDefaultModes;
EVT_IDD_CX_MONITOR_QUERY_TARGET_MODES EvtQueryTargetModes;
EVT_IDD_CX_MONITOR_ASSIGN_SWAPCHAIN EvtAssignSwapChain;
EVT_IDD_CX_MONITOR_UNASSIGN_SWAPCHAIN EvtUnassignSwapChain;

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
 * @brief Create the device: its IddCx callbacks, the queue of the wait requests, and the interface the connector opens.
 *
 * @param driver Unused, the driver.
 * @param deviceInit The initialization of the device.
 * @return `STATUS_SUCCESS`, or an error status if it failed.
 */
NTSTATUS EvtDeviceAdd(WDFDRIVER driver, PWDFDEVICE_INIT deviceInit) {
	WDF_PNPPOWER_EVENT_CALLBACKS pnp;
	IDD_CX_CLIENT_CONFIG cfg;
	WDF_OBJECT_ATTRIBUTES attr;
	WDF_IO_QUEUE_CONFIG queueConfig;
	WDFDEVICE device;
	DEVICE_CONTEXT *ctx;
	NTSTATUS s;

	UNREFERENCED_PARAMETER(driver);

	WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
	pnp.EvtDeviceD0Entry = EvtD0Entry;
	WdfDeviceInitSetPnpPowerEventCallbacks(deviceInit, &pnp);

	IDD_CX_CLIENT_CONFIG_INIT(&cfg);
	cfg.EvtIddCxDeviceIoControl                   = EvtIoControl;
	cfg.EvtIddCxAdapterInitFinished               = EvtAdapterInitFinished;
	cfg.EvtIddCxAdapterCommitModes                = EvtCommitModes;
	cfg.EvtIddCxParseMonitorDescription           = EvtParseMonitorDescription;
	cfg.EvtIddCxMonitorGetDefaultDescriptionModes = EvtGetDefaultModes;
	cfg.EvtIddCxMonitorQueryTargetModes           = EvtQueryTargetModes;
	cfg.EvtIddCxMonitorAssignSwapChain            = EvtAssignSwapChain;
	cfg.EvtIddCxMonitorUnassignSwapChain          = EvtUnassignSwapChain;

	s = IddCxDeviceInitConfig(deviceInit, &cfg);
	if (!NT_SUCCESS(s)) return s;

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, DEVICE_CONTEXT);
	attr.EvtCleanupCallback = EvtDeviceCleanup;
	s = WdfDeviceCreate(&deviceInit, &attr, &device);
	if (!NT_SUCCESS(s)) return s;

	ctx = GetDeviceCtx(device);
	InitializeSRWLock(&ctx->Lock);
	ctx->RequestEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
	if (!ctx->RequestEvent) return STATUS_INSUFFICIENT_RESOURCES;

	// Manual queue: the wait requests stay there until a frame or power change completes them
	WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);
	queueConfig.EvtIoCanceledOnQueue = EvtWaitCanceled;
	s = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &ctx->WaitQueue);
	if (!NT_SUCCESS(s)) return s;

	// The interface the connector opens
	s = WdfDeviceCreateDeviceInterface(device, &pspdisp_iddcx_interface_guid, NULL);
	if (!NT_SUCCESS(s)) return s;

	return IddCxDeviceInitialize(device);
}

/**
 * @brief Free the resources of the device, the cleanup of its context.
 *
 * @param device The device.
 */
VOID EvtDeviceCleanup(WDFOBJECT device) {
	DEVICE_CONTEXT *ctx = GetDeviceCtx(device);
	if (ctx->RequestEvent)
		CloseHandle(ctx->RequestEvent);
}

/**
 * @brief Start the IddCx adapter when the device powers up, for one monitor.
 *
 * @param device The device.
 * @param previousState Unused, the power state before.
 * @return `STATUS_SUCCESS`, or an error status if it failed.
 */
NTSTATUS EvtD0Entry(WDFDEVICE device, WDF_POWER_DEVICE_STATE previousState) {
	IDDCX_ENDPOINT_VERSION version = {0};
	IDDCX_ADAPTER_CAPS caps = {0};
	WDF_OBJECT_ATTRIBUTES attr;
	IDARG_IN_ADAPTER_INIT in = {0};
	IDARG_OUT_ADAPTER_INIT out = {0};
	NTSTATUS s;

	UNREFERENCED_PARAMETER(previousState);

	version.Size = sizeof(version);
	version.MajorVer = 1;

	caps.Size = sizeof(caps);
	caps.MaxMonitorsSupported = 1;
	caps.EndPointDiagnostics.Size = sizeof(caps.EndPointDiagnostics);
	caps.EndPointDiagnostics.GammaSupport = IDDCX_FEATURE_IMPLEMENTATION_NONE;
	caps.EndPointDiagnostics.TransmissionType = IDDCX_TRANSMISSION_TYPE_WIRED_USB;
	caps.EndPointDiagnostics.pEndPointFriendlyName     = L"PSPdisp Virtual Display";
	caps.EndPointDiagnostics.pEndPointManufacturerName = L"PSPdisp";
	caps.EndPointDiagnostics.pEndPointModelName        = L"PSPdisp";
	caps.EndPointDiagnostics.pFirmwareVersion = &version;
	caps.EndPointDiagnostics.pHardwareVersion = &version;

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, ADAPTER_CONTEXT);

	in.WdfDevice = device;
	in.pCaps = &caps;
	in.ObjectAttributes = &attr;

	s = IddCxAdapterInitAsync(&in, &out);
	if (!NT_SUCCESS(s)) {
		Log(L"[PSPdisp] IddCxAdapterInitAsync failed 0x%08X\n", s);
		return s;
	}

	GetAdapterCtx(out.AdapterObject)->Device = GetDeviceCtx(device);
	return s;
}

/**
 * @brief Keep the adapter once IddCx initialized it, the connector connects the monitor later.
 *
 * @param adapter The adapter.
 * @param in The result of the initialization.
 * @return Always `STATUS_SUCCESS`.
 */
NTSTATUS EvtAdapterInitFinished(IDDCX_ADAPTER adapter, const IDARG_IN_ADAPTER_INIT_FINISHED *in) {
	if (!NT_SUCCESS(in->AdapterInitStatus)) {
		Log(L"[PSPdisp] adapter initialization failed 0x%08X\n", in->AdapterInitStatus);
		return STATUS_SUCCESS;
	}

	// No monitor yet, the connector connects it with IOCTL_PSPDISP_IDDCX_CONNECT
	GetAdapterCtx(adapter)->Device->Adapter = adapter;
	return STATUS_SUCCESS;
}

/**
 * @brief Connect the monitor, with the EDID in the input buffer of the request.
 *
 * @param ctx The device.
 * @param request The `IOCTL_PSPDISP_IDDCX_CONNECT` request.
 * @return `STATUS_SUCCESS`, or an error status if it failed.
 */
NTSTATUS ConnectMonitor(DEVICE_CONTEXT *ctx, WDFREQUEST request) {
	const UINT8 *edid;
	WDF_OBJECT_ATTRIBUTES attr;
	IDDCX_MONITOR_INFO info = {0};
	IDARG_IN_MONITORCREATE create = {0};
	IDARG_OUT_MONITORCREATE createOut = {0};
	IDARG_OUT_MONITORARRIVAL arrival = {0};
	NTSTATUS s;

	s = WdfRequestRetrieveInputBuffer(request, PSPDISP_IDDCX_EDID_SIZE, (PVOID *) &edid, NULL);
	if (!NT_SUCCESS(s)) return s;
	if (ctx->Adapter == NULL) return STATUS_DEVICE_NOT_READY;

	AcquireSRWLockExclusive(&ctx->Lock);
	if (ctx->Monitor != NULL) {
		ReleaseSRWLockExclusive(&ctx->Lock);
		return STATUS_DEVICE_BUSY;
	}
	memcpy(ctx->Edid, edid, PSPDISP_IDDCX_EDID_SIZE);
	ctx->FrameWanted = false; // Only the connector of this monitor asks for its frames
	ctx->DpmsPending = false; // A power change of a previous monitor is not news for this one
	ReleaseSRWLockExclusive(&ctx->Lock);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, MONITOR_CONTEXT);
	attr.EvtCleanupCallback = EvtMonitorCleanup;

	info.Size = sizeof(info);
	info.MonitorType = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI; // As the example and Microsoft's sample, any connector type works for a virtual sink
	info.ConnectorIndex = 0;
	info.MonitorDescription.Size = sizeof(info.MonitorDescription);
	info.MonitorDescription.Type = IDDCX_MONITOR_DESCRIPTION_TYPE_EDID;
	info.MonitorDescription.DataSize = PSPDISP_IDDCX_EDID_SIZE;
	info.MonitorDescription.pData = ctx->Edid;
	CoCreateGuid(&info.MonitorContainerId);

	create.ObjectAttributes = &attr;
	create.pMonitorInfo = &info;

	s = IddCxMonitorCreate(ctx->Adapter, &create, &createOut);
	if (!NT_SUCCESS(s)) {
		Log(L"[PSPdisp] IddCxMonitorCreate failed 0x%08X\n", s);
		return s;
	}
	GetMonitorCtx(createOut.MonitorObject)->Device = ctx;
	GetMonitorCtx(createOut.MonitorObject)->Grabber = NULL;

	s = IddCxMonitorArrival(createOut.MonitorObject, &arrival); // The monitor is "plugged in"
	if (!NT_SUCCESS(s)) {
		Log(L"[PSPdisp] IddCxMonitorArrival failed 0x%08X\n", s);
		WdfObjectDelete((WDFOBJECT) createOut.MonitorObject);
		return s;
	}

	AcquireSRWLockExclusive(&ctx->Lock);
	ctx->Monitor = createOut.MonitorObject;
	ReleaseSRWLockExclusive(&ctx->Lock);
	return STATUS_SUCCESS;
}

/**
 * @brief Queue a wait request, or complete it right away with a power change not delivered yet.
 *
 * @param ctx The device.
 * @param request The `IOCTL_PSPDISP_IDDCX_WAIT` request.
 */
void QueueWait(DEVICE_CONTEXT *ctx, WDFREQUEST request) {
	PSPdispIddCxEvent *event;
	NTSTATUS s;

	AcquireSRWLockExclusive(&ctx->Lock);
	if (ctx->DpmsPending) {
		UINT32 mode = ctx->DpmsMode;
		ctx->DpmsPending = false;
		ReleaseSRWLockExclusive(&ctx->Lock);

		s = WdfRequestRetrieveOutputBuffer(request, sizeof(*event), (PVOID *) &event, NULL);
		if (!NT_SUCCESS(s)) {
			WdfRequestComplete(request, s);
			return;
		}
		event->type = PSPDISP_IDDCX_EVENT_DPMS;
		event->dpms_mode = mode;
		WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, FIELD_OFFSET(PSPdispIddCxEvent, pixels));
		return;
	}

	s = WdfRequestForwardToIoQueue(request, ctx->WaitQueue);
	ReleaseSRWLockExclusive(&ctx->Lock);

	if (!NT_SUCCESS(s)) {
		WdfRequestComplete(request, s);
		return;
	}
	SetEvent(ctx->RequestEvent); // The grabber thread delivers the newest frame, if one was asked and it has one not delivered yet
}

/**
 * @brief Mark a frame as wanted: the grabber thread completes the pending wait request with the newest frame not delivered yet.
 *
 * @param ctx The device.
 */
void RequestFrame(DEVICE_CONTEXT *ctx) {
	AcquireSRWLockExclusive(&ctx->Lock);
	ctx->FrameWanted = true;
	ReleaseSRWLockExclusive(&ctx->Lock);

	SetEvent(ctx->RequestEvent); // The grabber thread delivers the newest frame, if it has one not delivered yet (otherwise the next one)
}

/**
 * @brief Handle a request of the connector (see `protocol.h`).
 *
 * @param device The device.
 * @param request The request.
 * @param outLen Unused, the size of the output buffer.
 * @param inLen Unused, the size of the input buffer.
 * @param code The IOCTL of the request.
 */
VOID EvtIoControl(WDFDEVICE device, WDFREQUEST request, size_t outLen, size_t inLen, ULONG code) {
	DEVICE_CONTEXT *ctx = GetDeviceCtx(device);

	UNREFERENCED_PARAMETER(outLen);
	UNREFERENCED_PARAMETER(inLen);

	switch (code) {
		case IOCTL_PSPDISP_IDDCX_CONNECT:
			WdfRequestComplete(request, ConnectMonitor(ctx, request));
			break;
		case IOCTL_PSPDISP_IDDCX_DISCONNECT:
			DisconnectMonitor(ctx);
			WdfRequestComplete(request, STATUS_SUCCESS);
			break;
		case IOCTL_PSPDISP_IDDCX_WAIT:
			QueueWait(ctx, request);
			break;
		case IOCTL_PSPDISP_IDDCX_REQUEST_FRAME:
			RequestFrame(ctx);
			WdfRequestComplete(request, STATUS_SUCCESS);
			break;
		default:
			WdfRequestComplete(request, STATUS_INVALID_DEVICE_REQUEST);
			break;
	}
}

/**
 * @brief A pending wait request was cancelled (e.g. the connector process ended): disconnect the monitor, so it never outlives the connector.
 *
 * @param queue The queue of the wait requests.
 * @param request The cancelled request.
 */
VOID EvtWaitCanceled(WDFQUEUE queue, WDFREQUEST request) {
	WdfRequestComplete(request, STATUS_CANCELLED);
	DisconnectMonitor(GetDeviceCtx(WdfIoQueueGetDevice(queue)));
}

/**
 * @brief Accept the modes Windows sets, nothing to do for a virtual monitor.
 *
 * @param adapter Unused, the adapter.
 * @param in Unused, the modes.
 * @return Always `STATUS_SUCCESS`.
 */
NTSTATUS EvtCommitModes(IDDCX_ADAPTER adapter, const IDARG_IN_COMMITMODES *in) {
	// Real hardware would reprogram its output here, nothing to do for a virtual sink
	UNREFERENCED_PARAMETER(adapter);
	UNREFERENCED_PARAMETER(in);
	return STATUS_SUCCESS;
}

/**
 * @brief Give Windows the modes of the monitor, from its EDID.
 *
 * @param in The EDID, and where to write the modes.
 * @param out The number of modes, and the preferred one.
 * @return `STATUS_SUCCESS`, or an error status if an argument is invalid.
 */
NTSTATUS EvtParseMonitorDescription(const IDARG_IN_PARSEMONITORDESCRIPTION *in, IDARG_OUT_PARSEMONITORDESCRIPTION *out) {
	MODE modes[MAX_MODES];
	UINT32 count;

	if (in->MonitorDescription.DataSize < PSPDISP_IDDCX_EDID_SIZE) return STATUS_INVALID_PARAMETER;

	// Only the modes whose frames the grabber scales down to the PSP resolution, the size of the frames sent to the daemon (`PSPDISP_IDDCX_WIDTH` and `PSPDISP_IDDCX_HEIGHT`)
	count = ModesFromEdid((const UINT8 *) in->MonitorDescription.pData, modes);
	out->MonitorModeBufferOutputCount = count;
	if (in->MonitorModeBufferInputCount == 0) return STATUS_SUCCESS; // IddCx only asks for the count first
	if (in->MonitorModeBufferInputCount < count) return STATUS_BUFFER_TOO_SMALL;

	for (UINT32 i = 0; i < count; i++)
		in->pMonitorModes[i] = MakeMonitorMode(&modes[i]);
	out->PreferredMonitorModeIdx = 0; // The first detailed timing of the EDID
	return STATUS_SUCCESS;
}

/**
 * @brief Give Windows the default mode of a monitor that has no EDID.
 *
 * @param monitor The monitor.
 * @param in Where to write the modes.
 * @param out The number of modes, and the preferred one.
 * @return Always `STATUS_SUCCESS`.
 */
NTSTATUS EvtGetDefaultModes(IDDCX_MONITOR monitor, const IDARG_IN_GETDEFAULTDESCRIPTIONMODES *in, IDARG_OUT_GETDEFAULTDESCRIPTIONMODES *out) {
	MODE modes[MAX_MODES];
	UINT32 count;

	// Only called without an EDID, never for the PSPdisp monitors, but kept for completeness
	count = ModesFromEdid(GetMonitorCtx(monitor)->Device->Edid, modes);
	out->DefaultMonitorModeBufferOutputCount = count;
	if (in->DefaultMonitorModeBufferInputCount == 0) return STATUS_SUCCESS;
	if (in->DefaultMonitorModeBufferInputCount < count) return STATUS_BUFFER_TOO_SMALL;

	for (UINT32 i = 0; i < count; i++) {
		in->pDefaultMonitorModes[i] = MakeMonitorMode(&modes[i]);
		in->pDefaultMonitorModes[i].Origin = IDDCX_MONITOR_MODE_ORIGIN_DRIVER;
	}
	out->PreferredMonitorModeIdx = 0;
	return STATUS_SUCCESS;
}

/**
 * @brief Give Windows the modes it may set on the monitor: the ones of its EDID, so no other.
 *
 * @param monitor The monitor.
 * @param in Where to write the modes.
 * @param out The number of modes.
 * @return `STATUS_SUCCESS`, or `STATUS_BUFFER_TOO_SMALL` if an argument is invalid.
 */
NTSTATUS EvtQueryTargetModes(IDDCX_MONITOR monitor, const IDARG_IN_QUERYTARGETMODES *in, IDARG_OUT_QUERYTARGETMODES *out) {
	MODE modes[MAX_MODES];
	UINT32 count = ModesFromEdid(GetMonitorCtx(monitor)->Device->Edid, modes);

	out->TargetModeBufferOutputCount = count;
	if (in->TargetModeBufferInputCount == 0) return STATUS_SUCCESS; // IddCx only asks for the count first
	if (in->TargetModeBufferInputCount < count) return STATUS_BUFFER_TOO_SMALL;

	for (UINT32 i = 0; i < count; i++)
		in->pTargetModes[i] = MakeTargetMode(&modes[i]);
	return STATUS_SUCCESS;
}

/**
 * @brief Start a grabber on the swap chain Windows assigned to the monitor, and send the power on to the connector.
 *
 * @param monitor The monitor.
 * @param in The swap chain, its new frame event and its render adapter.
 * @return `STATUS_SUCCESS`, or `STATUS_INSUFFICIENT_RESOURCES` if it failed.
 */
NTSTATUS EvtAssignSwapChain(IDDCX_MONITOR monitor, const IDARG_IN_SETSWAPCHAIN *in) {
	MONITOR_CONTEXT *mc = GetMonitorCtx(monitor);

	GrabberDestroy(mc->Grabber);
	mc->Grabber = GrabberCreate(mc->Device, in->hSwapChain, in->hNextSurfaceAvailable, in->RenderAdapterLuid);
	if (!mc->Grabber) return STATUS_INSUFFICIENT_RESOURCES;

	SendDpms(mc->Device, PSPDISP_IDDCX_DPMS_ON); // Windows shows the desktop on the monitor
	return STATUS_SUCCESS;
}

/**
 * @brief Stop the grabber when Windows unassigns the swap chain, and send the power off to the connector.
 *
 * @param monitor The monitor.
 * @return Always `STATUS_SUCCESS`.
 */
NTSTATUS EvtUnassignSwapChain(IDDCX_MONITOR monitor) {
	MONITOR_CONTEXT *mc = GetMonitorCtx(monitor);

	GrabberDestroy(mc->Grabber);
	mc->Grabber = NULL;

	SendDpms(mc->Device, PSPDISP_IDDCX_DPMS_OFF); // The monitor is turned off, disabled or disconnected
	return STATUS_SUCCESS;
}
