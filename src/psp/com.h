/*
  ***************************************************
  PSPdisp (c) 2008 - 2015 Jochen Schleu

  com.h

  This software is licensed under the BSD license.
  See license.txt for details.
  ***************************************************
*/

#ifndef COM_H
#define COM_H

#include <pspsdk.h>
#include <pspthreadman.h>
#include "common.h"
#include "wlan.h"
#include "usb/usb.h"
#include "audio.h"
#include "../shared.h"


#define COM_AUDIO_BUFFER_SIZE (10 * PSPDISP_COM_AUDIO_CHUNK_2688_SIZE) // 10 audio chunks of the biggest size, the most `g_audioCurrentBufferSize` gets

#define COM_MODE_NONE -1
#define COM_MODE_USB 0
#define COM_MODE_WLAN 1


int g_comMode;
unsigned char g_comImageReceiveBuffer[PSPDISP_COM_MAX_FRAME_DATA_SIZE];
unsigned char g_comAudioReceiveBuffer[COM_AUDIO_BUFFER_SIZE];

int g_comAudioWritePosition;

bool g_comRunning;

bool g_comUpdateSettings;

bool g_comForceScreenUpdate;


bool readFrameInformation(PSPdispComFrameHeader* frameHeader);
void comLoop();
int readData(void* buffer, int length);
bool readDataLarge(void* buffer, int length);
bool readFrame(PSPdispComFrameHeader* frameHeader, unsigned char* soundData, PSPdispComSettingsReceived* settingsData);
bool sendResponse();


#endif
