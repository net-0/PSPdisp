/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * test_audio.c - Record the desktop sound to a file *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * Test of the audio and its connector, without the PSP: connects the PSP audio and records its sound into a WAV file on `TEST_AUDIO_PATH`, until Ctrl+C.
 *
 * Each second it prints how much was recorded and the loudest sample, so a silent capture is easy to spot.
 * The file is relative to the working directory (`make run-test-audio` runs it from `build/`).
 */

#include "audio/audio.c"
#include <glib-unix.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#define TEST_AUDIO_PATH        "./test_audio.wav"           // File where the sound is recorded
#define TEST_AUDIO_SAMPLE_RATE 22050                        // Sample rate in Hz, one of the PSP rates (11025, 22050 or 44100)
#define TEST_AUDIO_CHUNK_SIZE  (2240 * 2)                   // Size of each chunk in bytes, as sent to the PSP at 11025 and 22050 Hz (2688 * 2 at 44100 Hz)
#define TEST_AUDIO_MODEL       PSPDISP_MODEL_03G            // PSP model given to the audio, there's no real PSP to ask
#define TEST_AUDIO_REGION      PSPDISP_REGION_NORTH_AMERICA // PSP region given to the audio, there's no real PSP to ask

FILE *test_audio_file = NULL; // The WAV file, written by the connector thread until the audio is disconnected
gint test_audio_bytes = 0; // Bytes of samples written to the file
gint test_audio_peak = 0; // Loudest sample since the last report, from 0 to 32768

/**
 * @brief Write the header of the WAV file, at its start: 16-bit PCM, stereo, at `TEST_AUDIO_SAMPLE_RATE`.
 *
 * It's written with no samples when the file is created, then again with their size when recording stops.
 *
 * @param data_size The size of the samples in bytes.
 */
void test_audio_write_header(uint32_t data_size) {
	struct __attribute__((packed)) {
		char riff[4]; // "RIFF"
		uint32_t riff_size; // Size of the rest of the file
		char wave[4]; // "WAVE"
		char fmt[4]; // "fmt "
		uint32_t fmt_size; // Size of the format, 16 for PCM
		uint16_t format; // 1, PCM
		uint16_t channels; // 2, stereo
		uint32_t sample_rate; // Samples per second
		uint32_t byte_rate; // Bytes per second
		uint16_t block_align; // Bytes per stereo sample
		uint16_t bits; // Bits per sample
		char data[4]; // "data"
		uint32_t data_size; // Size of the samples
	} header = {
		.riff = { 'R', 'I', 'F', 'F' },
		.riff_size = GUINT32_TO_LE(36 + data_size),
		.wave = { 'W', 'A', 'V', 'E' },
		.fmt = { 'f', 'm', 't', ' ' },
		.fmt_size = GUINT32_TO_LE(16),
		.format = GUINT16_TO_LE(1),
		.channels = GUINT16_TO_LE(2),
		.sample_rate = GUINT32_TO_LE(TEST_AUDIO_SAMPLE_RATE),
		.byte_rate = GUINT32_TO_LE(TEST_AUDIO_SAMPLE_RATE * 4),
		.block_align = GUINT16_TO_LE(4),
		.bits = GUINT16_TO_LE(16),
		.data = { 'd', 'a', 't', 'a' },
		.data_size = GUINT32_TO_LE(data_size),
	};

	rewind(test_audio_file);
	fwrite(&header, sizeof(header), 1, test_audio_file);
	fseek(test_audio_file, 0, SEEK_END);
}

/**
 * @brief Write a chunk to the file and keep its loudest sample, called by the audio connector on its thread.
 *
 * @param samples The samples of the chunk.
 * @param size The size of the chunk in bytes.
 * @param data Unused.
 */
void test_audio_on_chunk(const int16_t *samples, uint32_t size, gpointer data) {
	(void) data;
	int peak = 0;
	for (uint32_t i = 0; i < size / sizeof(int16_t); i++)
		peak = MAX(peak, abs(samples[i]));

	fwrite(samples, 1, size, test_audio_file);
	g_atomic_int_add(&test_audio_bytes, size);

	int previous = g_atomic_int_get(&test_audio_peak);
	while (peak > previous && !g_atomic_int_compare_and_exchange_full(&test_audio_peak, previous, peak, &previous));
}

/**
 * @brief Print how much was recorded and the loudest sample of the last second, called by a timer on the main thread each second.
 *
 * @param data Unused.
 * @return Always `G_SOURCE_CONTINUE`, for the next second.
 */
gboolean test_audio_report(gpointer data) {
	(void) data;
	double seconds = (double) g_atomic_int_get(&test_audio_bytes) / (TEST_AUDIO_SAMPLE_RATE * 4);
	int peak = g_atomic_int_exchange(&test_audio_peak, 0);

	if (peak == 0)
		g_message("Recorded %.1f s, silence (is something playing?)", seconds);
	else
		g_message("Recorded %.1f s, loudest sample at %d%%", seconds, peak * 100 / 32768);
	return G_SOURCE_CONTINUE;
}

/**
 * @brief Quit the main loop, called on Ctrl+C (SIGINT) or SIGTERM.
 *
 * @param loop The main loop.
 * @return Always `G_SOURCE_CONTINUE`, the main loop ends anyway.
 */
gboolean test_audio_quit(gpointer loop) {
	g_message("Quitting...");
	g_main_loop_quit(loop);
	return G_SOURCE_CONTINUE;
}

/**
 * @brief Record the sound for the PSP into `TEST_AUDIO_PATH`, until Ctrl+C.
 *
 * @return 0 once it stopped, 1 if it couldn't start.
 */
int main() {
	test_audio_file = fopen(TEST_AUDIO_PATH, "wb");
	if (test_audio_file == NULL) {
		g_printerr("Failed to create the file %s\n", TEST_AUDIO_PATH);
		return 1;
	}
	test_audio_write_header(0); // Completed when recording stops

	PSPdispAudio *audio = pspdisp_audio_new(TEST_AUDIO_MODEL, TEST_AUDIO_REGION, TEST_AUDIO_SAMPLE_RATE, TEST_AUDIO_CHUNK_SIZE, test_audio_on_chunk, NULL);
	if (!pspdisp_audio_connect(audio)) {
		g_printerr("Failed to connect the audio with the %s connector (see the errors above)\n", PSPDISP_AUDIO_CONNECTOR_NAME);
		pspdisp_audio_destroy(audio);
		fclose(test_audio_file);
		return 1;
	}
	g_message("Audio \"%s\" connected with the %s connector, recording at %d Hz into %s (Ctrl+C to quit)", pspdisp_psp_name(TEST_AUDIO_MODEL, TEST_AUDIO_REGION), PSPDISP_AUDIO_CONNECTOR_NAME, TEST_AUDIO_SAMPLE_RATE, TEST_AUDIO_PATH);

	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	g_timeout_add_seconds(1, test_audio_report, NULL);
	g_unix_signal_add(SIGINT, test_audio_quit, loop);
	g_unix_signal_add(SIGTERM, test_audio_quit, loop);
	g_main_loop_run(loop);

	pspdisp_audio_destroy(audio); // Also disconnects it, so no chunk comes after it and the file can be completed

	uint32_t size = g_atomic_int_get(&test_audio_bytes);
	test_audio_write_header(size);
	fclose(test_audio_file);
	g_message("Saved %.1f s into %s", (double) size / (TEST_AUDIO_SAMPLE_RATE * 4), TEST_AUDIO_PATH);

	g_main_loop_unref(loop);
	return 0;
}
