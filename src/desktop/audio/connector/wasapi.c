/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * wasapi.c - Audio connector with WASAPI (Windows)  *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * Connects the PSP audio through WASAPI: it records the default output in loopback mode, so the sound the desktop plays.
 *
 * This is far from the "PSP" output of the Linux connector, but it works well enough without a kernel driver, which Windows makes hard to ship.
 * A sound output needs a kernel driver there, as audio has no user mode driver model (IddCx only exists for the displays).
 * And with Secure Boot, Windows only loads the kernel drivers that Microsoft signed, which needs an EV code signing certificate and a Partner Center account, or test signing on every computer.
 * So the PSP gets no sound output of its own: no volume or mute of its own, no app moved to it, and the sound still plays on the computer.
 * A process loopback (`AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS`, Windows 10 build 20348 or later) could send the sound of some apps only, still without a driver.
 *
 * Windows converts the sound to the asked format itself (`AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM`), which needs Windows 10 or later, as the IddCx display connector.
 */

#pragma once

#define COBJMACROS
#include <initguid.h> // Before the WASAPI headers, so their GUIDs are defined here
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <string.h>
#include "../connector.c" // Its declarations (when `connector.c` includes this file, it already has them and this include is skipped)

#define PSPDISP_AUDIO_CONNECTOR_NAME "WASAPI" // User friendly name of this connector

#define PSPDISP_WASAPI_BUFFER_DURATION 2000000 // Size of the WASAPI buffer, 200 ms in units of 100 ns
#define PSPDISP_WASAPI_POLL_INTERVAL   10      // Time between two reads of the WASAPI buffer, in milliseconds

/**
 * @brief Audio connector using WASAPI, returned by `pspdisp_audio_connector_connect()`.
 *
 * Its thread owns the WASAPI objects (COM needs them on the thread that created them), reads the buffer, fills the chunks and hands them to the handler.
 */
struct PSPdispAudioConnector {
	GThread *thread; // The connector thread
	gint stop; // Set to non-zero to stop the connector thread
	uint32_t sample_rate; // The sample rate in Hz
	PSPdispAudioHandler audio_handler; // Handle each full chunk
	gpointer handler_data; // Given to `audio_handler`
	uint8_t *chunk; // The chunk being filled
	uint32_t chunk_size; // The size of a chunk in bytes
	uint32_t chunk_length; // How many bytes of `chunk` are filled
	GMutex mutex; // Guards `started` and `ready`
	GCond cond; // Signaled when the connector thread started (or failed)
	bool started; // If the connector thread finished its start
	bool ready; // If the recording runs
};

/**
 * @brief Fill the chunks with recorded samples, and hand each full one to the handler.
 *
 * @param audio_connector The connector.
 * @param data The samples, or `NULL` for silence.
 * @param size The size of the samples in bytes.
 */
void pspdisp_audio_connector_wasapi_push(PSPdispAudioConnector *audio_connector, const uint8_t *data, uint32_t size) {
	uint32_t position = 0;

	while (position < size) {
		uint32_t count = MIN(size - position, audio_connector->chunk_size - audio_connector->chunk_length);
		uint8_t *end = audio_connector->chunk + audio_connector->chunk_length; // Where the chunk goes on

		if (data != NULL)
			memcpy(end, data + position, count);
		else
			memset(end, 0, count);
		audio_connector->chunk_length += count;
		position += count;

		if (audio_connector->chunk_length == audio_connector->chunk_size) {
			audio_connector->audio_handler((const int16_t *) audio_connector->chunk, audio_connector->chunk_size, audio_connector->handler_data);
			audio_connector->chunk_length = 0;
		}
	}
}

/**
 * @brief Body of the connector thread: start the loopback recording, then read it until stopped.
 *
 * Windows gives no data while nothing plays, so the chunks stop then.
 *
 * @param audio_connector The connector.
 * @return Always `NULL`, nothing is given to `g_thread_join()`.
 */
gpointer pspdisp_audio_connector_wasapi_thread(PSPdispAudioConnector *audio_connector) {
	IMMDeviceEnumerator *enumerator = NULL;
	IMMDevice *device = NULL;
	IAudioClient *client = NULL;
	IAudioCaptureClient *capture = NULL;
	HRESULT result = CoInitializeEx(NULL, COINIT_MULTITHREADED);

	// 16-bit stereo at the asked sample rate, Windows converts the mix to it
	WAVEFORMATEX format = {
		.wFormatTag = WAVE_FORMAT_PCM,
		.nChannels = 2,
		.nSamplesPerSec = audio_connector->sample_rate,
		.nAvgBytesPerSec = audio_connector->sample_rate * 4,
		.nBlockAlign = 4,
		.wBitsPerSample = 16,
		.cbSize = 0,
	};

	bool ready = SUCCEEDED(result)
		&& SUCCEEDED(result = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **) &enumerator))
		&& SUCCEEDED(result = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eRender, eConsole, &device))
		&& SUCCEEDED(result = IMMDevice_Activate(device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **) &client))
		&& SUCCEEDED(result = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, PSPDISP_WASAPI_BUFFER_DURATION, 0, &format, NULL))
		&& SUCCEEDED(result = IAudioClient_GetService(client, &IID_IAudioCaptureClient, (void **) &capture))
		&& SUCCEEDED(result = IAudioClient_Start(client));

	if (!ready)
		g_warning("%s(): failed to record the sound for the PSP (error 0x%08lX)", __FUNCTION__, (unsigned long) result);

	// Tell `pspdisp_audio_connector_connect()` how it went
	g_mutex_lock(&audio_connector->mutex);
	audio_connector->started = true;
	audio_connector->ready = ready;
	g_cond_signal(&audio_connector->cond);
	g_mutex_unlock(&audio_connector->mutex);

	while (ready && !g_atomic_int_get(&audio_connector->stop)) {
		Sleep(PSPDISP_WASAPI_POLL_INTERVAL);

		UINT32 packet = 0;
		while (SUCCEEDED(IAudioCaptureClient_GetNextPacketSize(capture, &packet)) && packet > 0) {
			BYTE *data;
			UINT32 frames;
			DWORD flags;

			if (FAILED(IAudioCaptureClient_GetBuffer(capture, &data, &frames, &flags, NULL, NULL))) break;
			pspdisp_audio_connector_wasapi_push(audio_connector, (flags & AUDCLNT_BUFFERFLAGS_SILENT) ? NULL : data, frames * format.nBlockAlign);
			IAudioCaptureClient_ReleaseBuffer(capture, frames);
		}
	}

	if (client != NULL)
		IAudioClient_Stop(client);
	if (capture != NULL)
		IAudioCaptureClient_Release(capture);
	if (client != NULL)
		IAudioClient_Release(client);
	if (device != NULL)
		IMMDevice_Release(device);
	if (enumerator != NULL)
		IMMDeviceEnumerator_Release(enumerator);
	CoUninitialize();
	return NULL;
}

/**
 * @brief Connect the PSP audio with WASAPI: record the default output in loopback mode.
 *
 * It starts the connector thread and waits until the recording runs (or failed).
 *
 * @param model The PSP generation, unused: the PSP gets no sound output of its own here (see the top of this file).
 * @param region The PSP sales region, unused.
 * @param sample_rate The sample rate in Hz (e.g. 22050).
 * @param chunk_size The size of each chunk in bytes, a multiple of 4 (a stereo frame of 16-bit samples).
 * @param audio_handler Handle each full chunk, called on the connector thread.
 * @param data Given to the handler.
 * @return The connector, or `NULL` if it failed.
 */
PSPdispAudioConnector *pspdisp_audio_connector_connect(PSPdispModel model, PSPdispRegion region, uint32_t sample_rate, uint32_t chunk_size, PSPdispAudioHandler audio_handler, gpointer data) {
	(void) model;
	(void) region;

	PSPdispAudioConnector *audio_connector = g_new0(PSPdispAudioConnector, 1);
	audio_connector->sample_rate = sample_rate;
	audio_connector->audio_handler = audio_handler;
	audio_connector->handler_data = data;
	audio_connector->chunk = g_malloc0(chunk_size);
	audio_connector->chunk_size = chunk_size;
	g_mutex_init(&audio_connector->mutex);
	g_cond_init(&audio_connector->cond);
	audio_connector->thread = g_thread_new("pspdisp-audio", (GThreadFunc) pspdisp_audio_connector_wasapi_thread, audio_connector);

	g_mutex_lock(&audio_connector->mutex);
	while (!audio_connector->started) g_cond_wait(&audio_connector->cond, &audio_connector->mutex);
	bool ready = audio_connector->ready;
	g_mutex_unlock(&audio_connector->mutex);

	if (!ready) {
		pspdisp_audio_connector_disconnect(audio_connector);
		return NULL;
	}

	return audio_connector;
}

/**
 * @brief Disconnect the PSP audio: stop recording, and free the connector.
 *
 * It waits for the connector thread, so the handler is never called again.
 *
 * @param audio_connector The connector.
 */
void pspdisp_audio_connector_disconnect(PSPdispAudioConnector *audio_connector) {
	g_atomic_int_set(&audio_connector->stop, 1);
	g_thread_join(audio_connector->thread);

	g_mutex_clear(&audio_connector->mutex);
	g_cond_clear(&audio_connector->cond);
	g_free(audio_connector->chunk);
	g_free(audio_connector);
}
