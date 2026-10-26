/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * test_display.c - Save the display frames to files *
 *                                                   *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

/*
 * Test of the display and its connector, without the PSP: connects the virtual display
 * and saves the current frame as a PNG on `TEST_DISPLAY_PICTURES_PATH` each second, until Ctrl+C.
 *
 * The pictures folder is relative to the working directory (`make run-test-display` runs it from `build/`).
 */

#include "display/display.c"
#include <glib-unix.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <signal.h>
#include <string.h>

#define TEST_DISPLAY_PICTURES_PATH  "./display_pictures"         // Folder where the frames are saved
#define TEST_DISPLAY_MODEL          PSPDISP_MODEL_03G            // PSP model given to the display, there's no real PSP to ask
#define TEST_DISPLAY_REGION         PSPDISP_REGION_NORTH_AMERICA // PSP region given to the display, there's no real PSP to ask
#define TEST_DISPLAY_SERIAL_NUMBER  1                            // Serial number given to the display, not 0 (see `pspdisp_display_edid_new()`)

GMutex test_display_frame_mutex; // Guards the variables below, written by the display thread and read by the main thread
uint32_t test_display_frame[PSPDISP_DISPLAY_WIDTH * PSPDISP_DISPLAY_HEIGHT]; // Copy of the last frame, on `PSPDISP_PIXEL_FORMAT_8888`
bool test_display_frame_received = false; // If some frame was received, so `test_display_frame` has something to save
unsigned test_display_frames_count = 0; // Frames received since the last save

/**
 * @brief Copy the dirty rectangles of the frame, called by the display on the display thread.
 *
 * The frame buffer is only valid during the call, so the changed areas are copied into `test_display_frame`.
 *
 * @param pixels The frame buffer.
 * @param stride The number of pixels per row in the frame buffer.
 * @param format The pixel format of the frame buffer.
 * @param rectangles The dirty rectangles.
 * @param rectangles_length The number of dirty rectangles.
 * @param data Unused.
 */
void test_display_on_frame(void *pixels, uint16_t stride, PSPdispPixelFormat format, PSPdispDirtyRectangle *rectangles, uint8_t rectangles_length, gpointer data) {
	(void) data;
	if (format != PSPDISP_PIXEL_FORMAT_8888) {
		g_warning("%s(): unsupported pixel format %d", __FUNCTION__, format);
		return;
	}

	const uint32_t *source = pixels;

	g_mutex_lock(&test_display_frame_mutex);
	for (uint8_t i = 0; i < rectangles_length; i++) {
		PSPdispDirtyRectangle rectangle = rectangles[i];
		if (rectangle.x2 > PSPDISP_DISPLAY_WIDTH || rectangle.y2 > PSPDISP_DISPLAY_HEIGHT) continue; // Out of the PSP screen

		for (uint16_t y = rectangle.y1; y < rectangle.y2; y++) {
			memcpy(&test_display_frame[y * PSPDISP_DISPLAY_WIDTH + rectangle.x1], &source[y * stride + rectangle.x1], (rectangle.x2 - rectangle.x1) * sizeof(uint32_t));
		}
	}
	test_display_frame_received |= rectangles_length > 0;
	test_display_frames_count++;
	g_mutex_unlock(&test_display_frame_mutex);
}

/**
 * @brief Print the loss of the virtual display (its driver is gone), called by the connector on the display thread.
 *
 * @param data Unused.
 */
void test_display_on_lost(gpointer data) {
	(void) data;
	g_warning("The virtual display was lost, its driver is gone");
}

/**
 * @brief Print the DPMS changes, called by the connector on the display thread.
 *
 * @param mode The DPMS mode.
 * @param data Unused.
 */
void test_display_on_dpms(PSPdispDPMSMode mode, gpointer data) {
	(void) data;
	static const char *names[] = { "ON", "STANDBY", "SUSPEND", "OFF" };
	g_message("DPMS: %s", mode < G_N_ELEMENTS(names) ? names[mode] : "unknown");
}

/**
 * @brief Free the pixels of a saved picture, called by GdkPixbuf when it's destroyed.
 *
 * @param pixels The pixels.
 * @param data Unused.
 */
void test_display_free_pixels(guchar *pixels, gpointer data) {
	(void) data;
	g_free(pixels);
}

/**
 * @brief Save the last frame as a PNG on `TEST_DISPLAY_PICTURES_PATH`, called by a timer on the main thread each second.
 *
 * @param data Unused.
 * @return Always `G_SOURCE_CONTINUE`, for the next second.
 */
gboolean test_display_save_frame(gpointer data) {
	(void) data;
	static unsigned saved_count = 0;

	// Copy the frame, so the display thread is not blocked while encoding the PNG
	g_mutex_lock(&test_display_frame_mutex);
	bool received = test_display_frame_received;
	unsigned frames_count = test_display_frames_count;
	guchar *pixels = received ? g_memdup2(test_display_frame, sizeof(test_display_frame)) : NULL;
	test_display_frames_count = 0;
	g_mutex_unlock(&test_display_frame_mutex);

	if (!received) {
		g_message("No frame received yet");
		return G_SOURCE_CONTINUE;
	}

	// PSPDISP_PIXEL_FORMAT_8888 is R,G,B,A in memory, the same layout of a GdkPixbuf with alpha
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data(pixels, GDK_COLORSPACE_RGB, TRUE, 8, PSPDISP_DISPLAY_WIDTH, PSPDISP_DISPLAY_HEIGHT, PSPDISP_DISPLAY_WIDTH * sizeof(uint32_t), test_display_free_pixels, NULL);
	g_autofree char *path = g_strdup_printf("%s/frame-%04u.png", TEST_DISPLAY_PICTURES_PATH, ++saved_count);
	GError *error = NULL;

	if (gdk_pixbuf_save(pixbuf, path, "png", &error, NULL)) {
		g_message("Saved %s (%u frames received in the last second)", path, frames_count);
	} else {
		g_warning("Failed to save %s: %s", path, error->message);
		g_error_free(error);
	}

	g_object_unref(pixbuf);
	return G_SOURCE_CONTINUE;
}

/**
 * @brief Quit the main loop, called on Ctrl+C (SIGINT) or SIGTERM.
 *
 * @param loop The main loop.
 * @return Always `G_SOURCE_CONTINUE`, the main loop ends anyway.
 */
gboolean test_display_quit(gpointer loop) {
	g_message("Quitting...");
	g_main_loop_quit(loop);
	return G_SOURCE_CONTINUE;
}

/**
 * @brief Create the virtual display and save a frame each second on `TEST_DISPLAY_PICTURES_PATH`, until Ctrl+C.
 *
 * @return 0 once it stopped, 1 if it couldn't start.
 */
int main() {
	if (g_mkdir_with_parents(TEST_DISPLAY_PICTURES_PATH, 0755) != 0) {
		g_printerr("Failed to create the folder %s\n", TEST_DISPLAY_PICTURES_PATH);
		return 1;
	}

	PSPdispDisplay *display = pspdisp_display_new(TEST_DISPLAY_MODEL, TEST_DISPLAY_REGION, TEST_DISPLAY_SERIAL_NUMBER, test_display_on_frame, test_display_on_dpms, test_display_on_lost, NULL);
	if (!pspdisp_display_connect(display)) {
		g_printerr("Failed to connect the display with the %s connector (see the errors above)\n", PSPDISP_DISPLAY_CONNECTOR_NAME);
		pspdisp_display_destroy(display);
		return 1;
	}
	g_message("Display connected with the %s connector, saving a frame each second on %s (Ctrl+C to quit)", PSPDISP_DISPLAY_CONNECTOR_NAME, TEST_DISPLAY_PICTURES_PATH);

	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	g_timeout_add_seconds(1, test_display_save_frame, NULL);
	g_unix_signal_add(SIGINT, test_display_quit, loop);
	g_unix_signal_add(SIGTERM, test_display_quit, loop);
	g_main_loop_run(loop);

	pspdisp_display_destroy(display); // Also disconnects it, removing the virtual display
	g_main_loop_unref(loop);
	return 0;
}
