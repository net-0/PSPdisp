/*****************************************************
 * SPDX-License-Identifier: BSD-3-Clause             *
 *                                                   *
 * shared.h - Metadata shared by PSP and Desktop     *
 *                                                   *
 *  This file shall only define static symbols.      *
 *                                                   *
 * Copyright © 2008-2015 Jochen Schleu               *
 * Copyright © 2026 Arthur Neto                      *
 *****************************************************/

#pragma once

#include <stdint.h>

#define PSPDISP_DISPLAY_WIDTH               480 // The PSP display width in pixels
#define PSPDISP_DISPLAY_HEIGHT              272 // The PSP display height in pixels
#define PSPDISP_DISPLAY_FREQUENCY            60 // The PSP display refresh rate in hertz
#define PSPDISP_DISPLAY_FRAME_BUFFER_STRIDE 512 // The PSP display frame-buffer pixels per row in memory

// All the types of this file are packed and little-endian whatever the host, so the PSP and the desktop share the same layout.
// Both pragmas are reset at the end of this file, so they don't leak into the files that include it.
#pragma pack(push, 1)
#pragma scalar_storage_order little-endian

/**
 * @brief Pixel formats supported by the PSP frame buffer
 *
 * Values match the `PspDisplayPixelFormats` from `pspdisplay.h`.
 *
 * Little-endian, red in the lowest bits.
 */
typedef enum: uint8_t {
	PSPDISP_PIXEL_FORMAT_565  = 0, // 16 bpp: R5 G6 B5, no alpha
	PSPDISP_PIXEL_FORMAT_5551 = 1, // 16 bpp: R5 G5 B5 A1
	PSPDISP_PIXEL_FORMAT_4444 = 2, // 16 bpp: R4 G4 B4 A4
	PSPDISP_PIXEL_FORMAT_8888 = 3, // 32 bpp: R8 G8 B8 A8
} PSPdispPixelFormat;

/**
 * @brief Display power management modes, modeled after VESA DPMS.
 *
 * Values are ordered by increasing power saving, so they can be compared directly (e.g. `mode >= PSPDISP_DPMS_MODE_SUSPEND`).
 */
typedef enum: uint8_t {
	PSPDISP_DPMS_MODE_ON,      // Display fully powered and showing output
	PSPDISP_DPMS_MODE_STANDBY, // Light power saving with near-instant recovery
	PSPDISP_DPMS_MODE_SUSPEND, // Deeper power saving with slower recovery
	PSPDISP_DPMS_MODE_OFF,     // Display powered down; slowest recovery
} PSPdispDPMSMode;

/**
 * @brief Rectangle of pixels that changed, if compared with the previous frame.
 *
 * Corners are absolute screen coordinates (origin top-left, x grows right, y grows down).
 *
 * The region is half-open: it covers [x1, x2) horizontally and [y1, y2) vertically, so width = x2 - x1 and height = y2 - y1.
 */
typedef struct {
	uint16_t x1; // Left edge, inclusive
	uint16_t y1; // Top edge, inclusive
	uint16_t x2; // Right edge, exclusive
	uint16_t y2; // Bottom edge, exclusive
} PSPdispDirtyRectangle;

/**
 * @brief Buttons of the PSP, combined with bitwise OR.
 *
 * Values match `PspCtrlButtons` from `pspctrl.h` (the bits of `SceCtrlData.Buttons`).
 *
 * The PSP app reads them in user mode, so it only sends the first 12, the others are only readable in kernel mode.
 * They only take the 24 lowest bits of the 32 of the buttons: the PSP app sends the character typed on its on-screen keyboard in the highest 8 instead (see `PSPdispComResponseHeader.buttons`).
 */
typedef enum: uint32_t {
	PSPDISP_BUTTON_SELECT   = 0b000000000000000000000001, // Select
	PSPDISP_BUTTON_START    = 0b000000000000000000001000, // Start
	PSPDISP_BUTTON_UP       = 0b000000000000000000010000, // Up of the directional pad
	PSPDISP_BUTTON_RIGHT    = 0b000000000000000000100000, // Right of the directional pad
	PSPDISP_BUTTON_DOWN     = 0b000000000000000001000000, // Down of the directional pad
	PSPDISP_BUTTON_LEFT     = 0b000000000000000010000000, // Left of the directional pad
	PSPDISP_BUTTON_L        = 0b000000000000000100000000, // Left shoulder button ("L trigger")
	PSPDISP_BUTTON_R        = 0b000000000000001000000000, // Right shoulder button ("R trigger")
	PSPDISP_BUTTON_TRIANGLE = 0b000000000001000000000000, // Triangle, the top face button
	PSPDISP_BUTTON_CIRCLE   = 0b000000000010000000000000, // Circle, the right face button
	PSPDISP_BUTTON_CROSS    = 0b000000000100000000000000, // Cross, the bottom face button
	PSPDISP_BUTTON_SQUARE   = 0b000000001000000000000000, // Square, the left face button
	PSPDISP_BUTTON_HOME     = 0b000000010000000000000000, // Home (kernel mode only)
	PSPDISP_BUTTON_HOLD     = 0b000000100000000000000000, // Hold switch (kernel mode only)
	PSPDISP_BUTTON_WLAN_UP  = 0b000001000000000000000000, // WLAN switch turned on (kernel mode only)
	PSPDISP_BUTTON_REMOTE   = 0b000010000000000000000000, // Remote control connected (kernel mode only)
	PSPDISP_BUTTON_VOLUP    = 0b000100000000000000000000, // Volume up (kernel mode only)
	PSPDISP_BUTTON_VOLDOWN  = 0b001000000000000000000000, // Volume down (kernel mode only)
	PSPDISP_BUTTON_SCREEN   = 0b010000000000000000000000, // Screen, the brightness button (kernel mode only)
	PSPDISP_BUTTON_NOTE     = 0b100000000000000000000000, // Note, the sound button (kernel mode only)
} PSPdispButton;

/**
 * @brief PSP hardware generation.
 *
 * Values match the return of `kuKernelGetModel()` from `kubridge.h`.
 *
 * The value is the "Xg" generation number minus one.
 */
typedef enum: uint8_t {
	PSPDISP_MODEL_01G = 0,  // PSP-1000 "fat"
	PSPDISP_MODEL_02G = 1,  // PSP-2000 "slim"
	PSPDISP_MODEL_03G = 2,  // PSP-3000 "brite", early boards
	PSPDISP_MODEL_04G = 3,  // PSP-3000, TA-093
	PSPDISP_MODEL_05G = 4,  // PSP Go (N1000)
	PSPDISP_MODEL_06G = 5,  // Never released
	PSPDISP_MODEL_07G = 6,  // PSP-3000, TA-095
	PSPDISP_MODEL_08G = 7,  // Never released
	PSPDISP_MODEL_09G = 8,  // PSP-3000, last revision
	PSPDISP_MODEL_10G = 9,  // Never released
	PSPDISP_MODEL_11G = 10, // PSP-E1000 "Street"
} PSPdispModel;

/**
 * @brief PSP sales region.
 *
 * Values match `ScePsCode.productCode` as filled by `sceChkregGetPsCode()` from uOFW's `chkreg.h` (listed as `SCE_PSP_PRODUCT_CODE_*` in `openpsid_kernel.h`).
 *
 * Constants are ordered by the last digits of the retail model given in the comments (e.g. PSP-2001 -> PSP_REGION_NORTH_AMERICA).
 */
typedef enum: uint8_t {
	PSPDISP_REGION_JAPAN         = 0x03, // x000, Japan
	PSPDISP_REGION_NORTH_AMERICA = 0x04, // x001, North America
	PSPDISP_REGION_AUSTRALIA     = 0x09, // x002, Australia / New Zealand
	PSPDISP_REGION_UK            = 0x07, // x003, United Kingdom
	PSPDISP_REGION_EUROPE        = 0x05, // x004, Europe / Middle East
	PSPDISP_REGION_KOREA         = 0x06, // x005, Korea
	PSPDISP_REGION_HONG_KONG     = 0x0A, // x006, Hong Kong / Singapore
	PSPDISP_REGION_TAIWAN        = 0x0B, // x007, Taiwan
	PSPDISP_REGION_RUSSIA        = 0x0C, // x008, Russia
	PSPDISP_REGION_CHINA         = 0x0D, // x009, China
	PSPDISP_REGION_MEXICO        = 0x08, // x010, Mexico / Latin America
	PSPDISP_REGION_TEST_UNIT     = 0x00, // No retail model, prototype / test unit
	PSPDISP_REGION_DEVKIT        = 0x01, // No retail model, DEM-1000 / DTP-T1000
	PSPDISP_REGION_TESTKIT       = 0x02, // No retail model, DTP-H1500
} PSPdispRegion;

/*
 * Communication protocol between the desktop and the PSP, over USB or WLAN.
 *
 * For each frame, the desktop sends a `PSPdispComFrameHeader` followed by its data (see its description), and the PSP answers with a `PSPdispComResponseHeader` or a `PSPdispComSettingsResponse`.
 * The settings are the desktop options that the PSP shows and changes on its "PC Settings" menu.
 *
 * DEPRECATED: every definition of this section is going to be replaced soon by a simpler protocol, easier to maintain, so don't build anything new on them.
 */

#define PSPDISP_COM_HEADER_MAGIC 0xAFFE0600 // First value of every frame and response header, to recognize them

#define PSPDISP_COM_MAX_FRAME_DATA_SIZE   (400 * 1024) // Max size in bytes of the data of a frame (after its header), the size of the PSP receive buffer
#define PSPDISP_COM_AUDIO_CHUNK_2240_SIZE (2240 * 2)   // Size in bytes of an audio chunk with `PSPDISP_COM_FLAG_AUDIO_CHUNK_2240`
#define PSPDISP_COM_AUDIO_CHUNK_2688_SIZE (2688 * 2)   // Size in bytes of an audio chunk with `PSPDISP_COM_FLAG_AUDIO_CHUNK_2688`

#define PSPDISP_COM_MAX_DISPLAY_DEVICES 10 // Max display device names the PSP keeps (see `PSPdispComSettingsReceived.devices`)
#define PSPDISP_COM_MAX_CONTROLS        20 // Max control file names the PSP keeps (see `PSPdispComSettingsReceived.controls`)
#define PSPDISP_COM_MAX_PRESETS         20 // Max preset names the PSP keeps (see `PSPdispComSettingsReceived.presets`)
#define PSPDISP_COM_NAME_SIZE           32 // Size in bytes of each display device, control file and preset name, null-terminated or padded with zeros

/**
 * @brief Flags of a frame header (desktop to PSP) or of a response header (PSP to desktop), combined with bitwise OR.
 *
 * Each flag tells which data follows the header, or how to handle it.
 * Only `PSPDISP_COM_FLAG_CONTAINS_SETTINGS_DATA` and `PSPDISP_COM_FLAG_FORCE_UPDATE` are used in a response header.
 */
typedef enum: uint32_t {
	PSPDISP_COM_FLAG_CONTAINS_IMAGE_DATA      = 0b00000000000000000000000000000001, // Frame: an image follows the header (`image_size` bytes)
	PSPDISP_COM_FLAG_CONTAINS_AUDIO_DATA      = 0b00000000000000000000000000000010, // Frame: an audio chunk follows the image (see `PSPDISP_COM_FLAG_AUDIO_CHUNK_2240` and `PSPDISP_COM_FLAG_AUDIO_CHUNK_2688`)
	PSPDISP_COM_FLAG_CONTAINS_SETTINGS_DATA   = 0b00000000000000000000000000000100, // Frame: the settings follow the audio (`settings_size` bytes), response: it's a `PSPdispComSettingsResponse` with settings changed on the PSP
	PSPDISP_COM_FLAG_ENABLE_OSK               = 0b00000000000000000000000000001000, // Frame: the PSP may open its on-screen keyboard (Danzeff) with the Select button
	PSPDISP_COM_FLAG_IMAGE_IS_JPEG            = 0b00000000000000000000000000010000, // Frame: the image is a JPEG
	PSPDISP_COM_FLAG_IMAGE_IS_UNCOMPRESSED    = 0b00000000000000000000000001000000, // Frame: the image is uncompressed, 24 bpp in B,G,R order with the rows bottom-up (a Windows bitmap without its headers)
	PSPDISP_COM_FLAG_AUDIO_11025_HZ           = 0b00000000000000000001000000000000, // Frame: the audio sample rate is 11025 Hz
	PSPDISP_COM_FLAG_AUDIO_22050_HZ           = 0b00000000000000000010000000000000, // Frame: the audio sample rate is 22050 Hz
	PSPDISP_COM_FLAG_AUDIO_44100_HZ           = 0b00000000000000000100000000000000, // Frame: the audio sample rate is 44100 Hz
	PSPDISP_COM_FLAG_AUDIO_CHUNK_2240         = 0b00000000000000010000000000000000, // Frame: the audio chunk is 2240 samples of 16 bits, stereo (4480 bytes, used at 11025 and 22050 Hz)
	PSPDISP_COM_FLAG_AUDIO_CHUNK_2688         = 0b00000000000000100000000000000000, // Frame: the audio chunk is 2688 samples of 16 bits, stereo (5376 bytes, used at 44100 Hz)
	PSPDISP_COM_FLAG_AUDIO_SAMPLE_RATE_MASK   = 0b00000000000000110111000000000000, // Mask of the 3 sample rate flags and the 2 chunk flags, the PSP restarts its audio when they change
	PSPDISP_COM_FLAG_IMAGE_CLEAR_SCREEN       = 0b00000000000100000000000000000000, // Frame: no image, the PSP clears its screen (the display streaming is disabled)
	PSPDISP_COM_FLAG_FORCE_UPDATE             = 0b00000001000000000000000000000000, // Response: the PSP asks for a new image even if the desktop didn't change (e.g. after closing its menu)
} PSPdispComFlags;

/**
 * @brief JPEG quality of the images sent to the PSP (`PSPdispComSettings.quality`), the higher the bigger the frames.
 */
typedef enum: uint32_t {
	PSPDISP_COM_QUALITY_LOWEST  = 0, // JPEG quality 10
	PSPDISP_COM_QUALITY_LOW     = 1, // JPEG quality 20
	PSPDISP_COM_QUALITY_MEDIUM  = 2, // JPEG quality 30
	PSPDISP_COM_QUALITY_HIGH    = 3, // JPEG quality 50
	PSPDISP_COM_QUALITY_HIGHEST = 4, // JPEG quality 80
} PSPdispComQuality;

/**
 * @brief Pause of the desktop after sending each frame (`PSPdispComSettings.update_interval`), to lower the frame rate and the load.
 */
typedef enum: uint32_t {
	PSPDISP_COM_UPDATE_INTERVAL_CONTINUOUS = 0, // No pause, the next frame is sent right away
	PSPDISP_COM_UPDATE_INTERVAL_SHORTEST   = 1, // 30 ms
	PSPDISP_COM_UPDATE_INTERVAL_SHORT      = 2, // 40 ms
	PSPDISP_COM_UPDATE_INTERVAL_MEDIUM     = 3, // 50 ms
	PSPDISP_COM_UPDATE_INTERVAL_LONG       = 4, // 100 ms
	PSPDISP_COM_UPDATE_INTERVAL_LONGEST    = 5, // 200 ms
} PSPdispComUpdateInterval;

/**
 * @brief Sample rate of the sound streamed to the PSP (`PSPdispComSettings.sound_sample_rate`), see the `PSPDISP_COM_FLAG_AUDIO_*_HZ` flags.
 */
typedef enum: uint32_t {
	PSPDISP_COM_SAMPLE_RATE_11025 = 0, // 11025 Hz
	PSPDISP_COM_SAMPLE_RATE_22050 = 1, // 22050 Hz
	PSPDISP_COM_SAMPLE_RATE_44100 = 2, // 44100 Hz
} PSPdispComSampleRate;

/**
 * @brief Desktop settings shown and changed on the "PC Settings" menu of the PSP.
 *
 * The desktop sends them in a frame, after the audio (see `PSPdispComFrameHeader`), and the PSP sends them back changed in a `PSPdispComSettingsResponse`.
 * The flags are 0 (off) or 1 (on), the `*_index` fields select an entry of the names in `PSPdispComSettingsReceived`, and the `*_count` fields tell how many there are.
 */
typedef struct {
	uint32_t device_index;                    // Unused, always 0
	PSPdispComQuality quality;                // JPEG quality of the image
	uint32_t screen_no_compression;           // Flag: send the images uncompressed (`PSPDISP_COM_FLAG_IMAGE_IS_UNCOMPRESSED`) instead of JPEG
	PSPdispComUpdateInterval update_interval; // Pause after each frame
	uint32_t sound_enabled;                   // Flag: stream the desktop sound
	PSPdispComSampleRate sound_sample_rate;   // Sample rate of the sound
	uint32_t control_index;                   // Control file in use (how the PSP buttons drive the desktop), in `PSPdispComSettingsReceived.controls`
	uint32_t control_count;                   // Number of control files, at most `PSPDISP_COM_MAX_CONTROLS`
	uint32_t display_device_index;            // Display device (monitor) streamed, in `PSPdispComSettingsReceived.devices`
	uint32_t display_device_count;            // Number of display devices, at most `PSPDISP_COM_MAX_DISPLAY_DEVICES`
	uint32_t display_enable;                  // Flag: stream the display, otherwise the frames only clear the PSP screen (`PSPDISP_COM_FLAG_IMAGE_CLEAR_SCREEN`)
	uint32_t display_autoselect;              // Flag: stream the display device the mouse is on, switching when it moves to another one
	uint32_t preset_index;                    // Preset (a saved set of these settings), in `PSPdispComSettingsReceived.presets`
	uint32_t preset_count;                    // Number of presets, at most `PSPDISP_COM_MAX_PRESETS`
	uint32_t preset_do_set;                   // Flag: the PSP asks the desktop to apply the preset `preset_index`
} PSPdispComSettings;

static_assert(sizeof(PSPdispComSettings) == 60, "The settings must be 15 values of 4 bytes");

/**
 * @brief Header of a frame, sent by the desktop to the PSP before its data.
 *
 * The data follows it in this order, each part only with its flag in `flags`:
 *   1. The image, `image_size` bytes (`PSPDISP_COM_FLAG_CONTAINS_IMAGE_DATA`), in the format of the `PSPDISP_COM_FLAG_IMAGE_IS_*` flags.
 *   2. The audio chunk, 4480 or 5376 bytes (`PSPDISP_COM_FLAG_CONTAINS_AUDIO_DATA`), as told by the `PSPDISP_COM_FLAG_AUDIO_CHUNK_*` flags.
 *   3. The settings, `settings_size` bytes (`PSPDISP_COM_FLAG_CONTAINS_SETTINGS_DATA`): a `PSPdispComSettings`, then the display device names, the control file names and the preset names, `PSPDISP_COM_NAME_SIZE` bytes each and as many as their `*_count` fields.
 */
typedef struct {
	uint32_t magic;         // Always `PSPDISP_COM_HEADER_MAGIC`
	uint32_t flags;         // Combination of `PSPdispComFlags`
	uint32_t image_size;    // Size in bytes of the image
	uint32_t settings_size; // Size in bytes of the settings, with their names
} PSPdispComFrameHeader;

static_assert(sizeof(PSPdispComFrameHeader) == 16, "A frame header must be 16 bytes");

/**
 * @brief Header of a response, sent by the PSP to the desktop after each frame, with the state of its controls.
 *
 * It's sent alone, or as part of a `PSPdispComSettingsResponse`.
 */
typedef struct {
	uint32_t magic;   // Always `PSPDISP_COM_HEADER_MAGIC`
	uint32_t flags;   // Combination of `PSPdispComFlags`: `PSPDISP_COM_FLAG_CONTAINS_SETTINGS_DATA` and `PSPDISP_COM_FLAG_FORCE_UPDATE`
	uint32_t buttons; // Pressed buttons, a combination of `PSPdispButton` (none while the PSP menu is open), or only the character typed on its on-screen keyboard in the highest 8 bits while it's open (255 for none)
	uint8_t analog_x; // Analog stick position from the left (0) to the right (255), `SceCtrlData.Lx` (127 while the PSP menu is open)
	uint8_t analog_y; // Analog stick position from the top (0) to the bottom (255), `SceCtrlData.Ly` (127 while the PSP menu is open)
} PSPdispComResponseHeader;

static_assert(sizeof(PSPdispComResponseHeader) == 14, "A response header must be 14 bytes");

/**
 * @brief Response with the settings of the PSP, instead of a plain `PSPdispComResponseHeader`.
 *
 * In USB mode it's sent after every frame, in WLAN mode only when the settings changed on the PSP.
 * The settings changed when `PSPDISP_COM_FLAG_CONTAINS_SETTINGS_DATA` is in the flags of the response.
 */
typedef struct {
	PSPdispComResponseHeader response; // The response header
	PSPdispComSettings settings;       // The settings, as on the PSP
} PSPdispComSettingsResponse;

static_assert(sizeof(PSPdispComSettingsResponse) == 14 + 60, "A settings response must be a response header and the settings");

/**
 * @brief Settings received by the PSP, with the names of their entries.
 *
 * It's how the PSP keeps them, not how they are sent: in a frame, each names array only holds as many names as its `*_count` field (see `PSPdispComFrameHeader`).
 */
typedef struct {
	PSPdispComSettings settings;                                          // The settings
	char devices[PSPDISP_COM_MAX_DISPLAY_DEVICES][PSPDISP_COM_NAME_SIZE]; // Names of the display devices, `settings.display_device_count` of them
	char controls[PSPDISP_COM_MAX_CONTROLS][PSPDISP_COM_NAME_SIZE];       // Names of the control files, `settings.control_count` of them
	char presets[PSPDISP_COM_MAX_PRESETS][PSPDISP_COM_NAME_SIZE];         // Names of the presets, `settings.preset_count` of them
} PSPdispComSettingsReceived;

static_assert(sizeof(PSPdispComSettingsReceived) == 60 + (10 + 20 + 20) * 32, "The received settings must be the settings and all their names");

#pragma scalar_storage_order default
#pragma pack(pop)

/**
 * @brief Get the model number of a PSP, as printed on its label (e.g. "PSP-3004"), to name it to the user.
 *
 * The model number is the series of the generation (e.g. PSP-3000 for the 03g, 04g, 07g and 09g boards) with the region in its last digits.
 *
 * Names follow that pattern even for combinations that were never sold (e.g. "PSP-E1001", the PSP-E1000 was only sold in Europe).
 *
 * Development units get the model number of the first kit of their kind (e.g. "DTP-T1000" for a DevKit), whatever the generation.
 *
 * Never released generations get "?" as their series (e.g. "PSP-?001"), and unknown regions get the generation instead (e.g. "PSP-01g?").
 *
 * @param model The PSP generation.
 * @param region The PSP sales region.
 * @return The model number, a static string of up to 13 characters (it fits an EDID text descriptor).
 */
static inline const char *pspdisp_psp_name(PSPdispModel model, PSPdispRegion region) {
	switch (region) {
		case PSPDISP_REGION_JAPAN:
			switch (model) {
				case PSPDISP_MODEL_01G: return "PSP-1000";
				case PSPDISP_MODEL_02G: return "PSP-2000";
				case PSPDISP_MODEL_03G:
				case PSPDISP_MODEL_04G:
				case PSPDISP_MODEL_07G:
				case PSPDISP_MODEL_09G: return "PSP-3000";
				case PSPDISP_MODEL_05G: return "PSP-N1000";
				case PSPDISP_MODEL_11G: return "PSP-E1000";
				default: return "PSP-?000"; // Never released
			}
		case PSPDISP_REGION_NORTH_AMERICA:
			switch (model) {
				case PSPDISP_MODEL_01G: return "PSP-1001";
				case PSPDISP_MODEL_02G: return "PSP-2001";
				case PSPDISP_MODEL_03G:
				case PSPDISP_MODEL_04G:
				case PSPDISP_MODEL_07G:
				case PSPDISP_MODEL_09G: return "PSP-3001";
				case PSPDISP_MODEL_05G: return "PSP-N1001";
				case PSPDISP_MODEL_11G: return "PSP-E1001";
				default: return "PSP-?001"; // Never released
			}
		case PSPDISP_REGION_AUSTRALIA:
			switch (model) {
				case PSPDISP_MODEL_01G: return "PSP-1002";
				case PSPDISP_MODEL_02G: return "PSP-2002";
				case PSPDISP_MODEL_03G:
				case PSPDISP_MODEL_04G:
				case PSPDISP_MODEL_07G:
				case PSPDISP_MODEL_09G: return "PSP-3002";
				case PSPDISP_MODEL_05G: return "PSP-N1002";
				case PSPDISP_MODEL_11G: return "PSP-E1002";
				default: return "PSP-?002"; // Never released
			}
		case PSPDISP_REGION_UK:
			switch (model) {
				case PSPDISP_MODEL_01G: return "PSP-1003";
				case PSPDISP_MODEL_02G: return "PSP-2003";
				case PSPDISP_MODEL_03G:
				case PSPDISP_MODEL_04G:
				case PSPDISP_MODEL_07G:
				case PSPDISP_MODEL_09G: return "PSP-3003";
				case PSPDISP_MODEL_05G: return "PSP-N1003";
				case PSPDISP_MODEL_11G: return "PSP-E1003";
				default: return "PSP-?003"; // Never released
			}
		case PSPDISP_REGION_EUROPE:
			switch (model) {
				case PSPDISP_MODEL_01G: return "PSP-1004";
				case PSPDISP_MODEL_02G: return "PSP-2004";
				case PSPDISP_MODEL_03G:
				case PSPDISP_MODEL_04G:
				case PSPDISP_MODEL_07G:
				case PSPDISP_MODEL_09G: return "PSP-3004";
				case PSPDISP_MODEL_05G: return "PSP-N1004";
				case PSPDISP_MODEL_11G: return "PSP-E1004";
				default: return "PSP-?004"; // Never released
			}
		case PSPDISP_REGION_KOREA:
			switch (model) {
				case PSPDISP_MODEL_01G: return "PSP-1005";
				case PSPDISP_MODEL_02G: return "PSP-2005";
				case PSPDISP_MODEL_03G:
				case PSPDISP_MODEL_04G:
				case PSPDISP_MODEL_07G:
				case PSPDISP_MODEL_09G: return "PSP-3005";
				case PSPDISP_MODEL_05G: return "PSP-N1005";
				case PSPDISP_MODEL_11G: return "PSP-E1005";
				default: return "PSP-?005"; // Never released
			}
		case PSPDISP_REGION_HONG_KONG:
			switch (model) {
				case PSPDISP_MODEL_01G: return "PSP-1006";
				case PSPDISP_MODEL_02G: return "PSP-2006";
				case PSPDISP_MODEL_03G:
				case PSPDISP_MODEL_04G:
				case PSPDISP_MODEL_07G:
				case PSPDISP_MODEL_09G: return "PSP-3006";
				case PSPDISP_MODEL_05G: return "PSP-N1006";
				case PSPDISP_MODEL_11G: return "PSP-E1006";
				default: return "PSP-?006"; // Never released
			}
		case PSPDISP_REGION_TAIWAN:
			switch (model) {
				case PSPDISP_MODEL_01G: return "PSP-1007";
				case PSPDISP_MODEL_02G: return "PSP-2007";
				case PSPDISP_MODEL_03G:
				case PSPDISP_MODEL_04G:
				case PSPDISP_MODEL_07G:
				case PSPDISP_MODEL_09G: return "PSP-3007";
				case PSPDISP_MODEL_05G: return "PSP-N1007";
				case PSPDISP_MODEL_11G: return "PSP-E1007";
				default: return "PSP-?007"; // Never released
			}
		case PSPDISP_REGION_RUSSIA:
			switch (model) {
				case PSPDISP_MODEL_01G: return "PSP-1008";
				case PSPDISP_MODEL_02G: return "PSP-2008";
				case PSPDISP_MODEL_03G:
				case PSPDISP_MODEL_04G:
				case PSPDISP_MODEL_07G:
				case PSPDISP_MODEL_09G: return "PSP-3008";
				case PSPDISP_MODEL_05G: return "PSP-N1008";
				case PSPDISP_MODEL_11G: return "PSP-E1008";
				default: return "PSP-?008"; // Never released
			}
		case PSPDISP_REGION_CHINA:
			switch (model) {
				case PSPDISP_MODEL_01G: return "PSP-1009";
				case PSPDISP_MODEL_02G: return "PSP-2009";
				case PSPDISP_MODEL_03G:
				case PSPDISP_MODEL_04G:
				case PSPDISP_MODEL_07G:
				case PSPDISP_MODEL_09G: return "PSP-3009";
				case PSPDISP_MODEL_05G: return "PSP-N1009";
				case PSPDISP_MODEL_11G: return "PSP-E1009";
				default: return "PSP-?009"; // Never released
			}
		case PSPDISP_REGION_MEXICO:
			switch (model) {
				case PSPDISP_MODEL_01G: return "PSP-1010";
				case PSPDISP_MODEL_02G: return "PSP-2010";
				case PSPDISP_MODEL_03G:
				case PSPDISP_MODEL_04G:
				case PSPDISP_MODEL_07G:
				case PSPDISP_MODEL_09G: return "PSP-3010";
				case PSPDISP_MODEL_05G: return "PSP-N1010";
				case PSPDISP_MODEL_11G: return "PSP-E1010";
				default: return "PSP-?010"; // Never released
			}
		case PSPDISP_REGION_TEST_UNIT:  return "PSP-0000";
		case PSPDISP_REGION_DEVKIT:     return "DTP-T1000";
		case PSPDISP_REGION_TESTKIT:    return "DTP-H1500";
		default: break;
	}

	// Unknown region
	switch (model) {
		case PSPDISP_MODEL_01G: return "PSP-01g?";
		case PSPDISP_MODEL_02G: return "PSP-02g?";
		case PSPDISP_MODEL_03G: return "PSP-03g?";
		case PSPDISP_MODEL_04G: return "PSP-04g?";
		case PSPDISP_MODEL_05G: return "PSP-05g?";
		case PSPDISP_MODEL_06G: return "PSP-06g?";
		case PSPDISP_MODEL_07G: return "PSP-07g?";
		case PSPDISP_MODEL_08G: return "PSP-08g?";
		case PSPDISP_MODEL_09G: return "PSP-09g?";
		case PSPDISP_MODEL_10G: return "PSP-10g?";
		case PSPDISP_MODEL_11G: return "PSP-11g?";
	}
	return "PSP-?"; // Unknown model
}
