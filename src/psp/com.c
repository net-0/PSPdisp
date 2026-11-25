/*
  ***************************************************
  PSPdisp (c) 2008 - 2015 Jochen Schleu

  com.c - all data transmission functions

  This software is licensed under the BSD license.
  See license.txt for details.
  ***************************************************
*/

#include "com.h"

#include "compress.h"

#include "menu.h"
extern menuEntry pcSettingsDisplay[3];
extern menuEntry pcSettingsMisc[4];


#define CHECK_WLAN_RESULT { if ((g_comMode == COM_MODE_WLAN) && !g_netConnected) return; }


PSPdispComSettingsResponse l_response __attribute__((aligned(64)));


/*
  readData
  ---------------------------------------------------
  Read data from the active communication mode.
  ---------------------------------------------------
  Returns the number of bytes read, < 0 on error.
*/
int readData(void* buffer, int length)
{
  if (g_comMode == COM_MODE_USB)
  {
    int result = pspdisp_usb_read(buffer, length) ? length : -1;

    // Pause a bit on error
    if (result < 0)
      sceKernelDelayThread(1000 * 200);

    return result;
  }
  else // if (g_comMode == COM_MODE_WLAN)
  {
    if (!g_netConnected)
      return -1;

    SceUID alarmId = sceKernelSetAlarm(NET_SOCKET_TIMEOUT, (SceKernelAlarmHandler)netSocketTimeoutAlarm, NULL);
    int result = sceNetInetRecv(g_netSocket, buffer, length, 0);
    sceKernelCancelAlarm(alarmId);

    if (result <= 0)
      g_netConnected = false;

    return result;
  }
}



/*
  readDataLarge
  ---------------------------------------------------
  Read data from the active communication mode
  until all requested data is received.
  ---------------------------------------------------
  Returns true on success.
*/
bool readDataLarge(void* buffer, int length)
{
  if (length == 0)
    return true;

  int bufferPosition = 0;
  int bytesToRead = 0;
  int len;
  unsigned char* charBuffer = buffer;

  while (bufferPosition < length)
  {
    bytesToRead = (length - bufferPosition);
    if (bytesToRead > 0)
    {
      len = readData(&charBuffer[bufferPosition], bytesToRead);

      if (len < 0)
      {
        // Reading error
        return false;
      }

      bufferPosition += len;
    }
  }

  return true;
}




/*
  writeData
  ---------------------------------------------------
  Write data to the selected output.
  ---------------------------------------------------
  Returns bytes written or an error code.
*/
int writeData(void* buffer, int length)
{
  if (g_comMode == COM_MODE_USB)
  {
    return pspdisp_usb_write(buffer, length) ? length : -1;
  }
  else // if (g_comMode == COM_MODE_WLAN)
  {
    if (!g_netConnected)
      return -1;

    SceUID alarmId = sceKernelSetAlarm(NET_SOCKET_TIMEOUT, (SceKernelAlarmHandler)netSocketTimeoutAlarm, NULL);
    int result = sceNetInetSend(g_netSocket, buffer, length, 0);
    sceKernelCancelAlarm(alarmId);

    if (result < 0)
      g_netConnected = false;

    return result;
  }
}





/*
  waitForDataSize
  ---------------------------------------------------
  Waits till the PC sends the size of the next
  Jpeg frame.
  ---------------------------------------------------
  Returns true if information was received.
*/
bool readFrameInformation(PSPdispComFrameHeader* frameHeader)
{
  memset(frameHeader, 0, sizeof(PSPdispComFrameHeader));
  int len = readData(frameHeader, sizeof(PSPdispComFrameHeader));

  if (len == sizeof(PSPdispComFrameHeader))
  {
    if (frameHeader->magic == PSPDISP_COM_HEADER_MAGIC)
    {
      return true;
    }
  }

  sceKernelDelayThread(200 * 1000);
  return false;
}




/*
  readFrame
  ---------------------------------------------------
  Reads from the input till the whole frame
  is transmitted.
  ---------------------------------------------------
  Returns true on success.
*/
bool readFrame(PSPdispComFrameHeader* frameHeader, unsigned char* soundData, PSPdispComSettingsReceived* settingsData)
{
  // TODO: check the frame size against `PSPDISP_COM_MAX_FRAME_DATA_SIZE` and the name counts against `PSPDISP_COM_MAX_*`, as bigger values from the desktop overflow the buffers
  const u32 audioFrameSize = ((frameHeader->flags & PSPDISP_COM_FLAG_AUDIO_CHUNK_2240) ? PSPDISP_COM_AUDIO_CHUNK_2240_SIZE : PSPDISP_COM_AUDIO_CHUNK_2688_SIZE);
  const u32 audioDataSize = ((frameHeader->flags & PSPDISP_COM_FLAG_CONTAINS_AUDIO_DATA) ? audioFrameSize : 0);

  if (!readDataLarge(g_comImageReceiveBuffer, frameHeader->image_size + audioDataSize + frameHeader->settings_size))
    return false;

  if (audioDataSize > 0)
  {
    memcpy(soundData, &g_comImageReceiveBuffer[frameHeader->image_size], audioDataSize);
  }

  if (frameHeader->flags & PSPDISP_COM_FLAG_CONTAINS_SETTINGS_DATA)
  {
    u32 bufferPosition = frameHeader->image_size + audioDataSize;

    // Copy the settings structure
    memcpy(&settingsData->settings, &g_comImageReceiveBuffer[bufferPosition], sizeof(PSPdispComSettings));
    bufferPosition += sizeof(PSPdispComSettings);

    // Fill the device, control and preset names, the ones that weren't sent are empty
    const u32 devicesSize = settingsData->settings.display_device_count * PSPDISP_COM_NAME_SIZE;
    const u32 controlsSize = settingsData->settings.control_count * PSPDISP_COM_NAME_SIZE;
    const u32 presetsSize = settingsData->settings.preset_count * PSPDISP_COM_NAME_SIZE;

    memset(settingsData->devices, 0, sizeof(settingsData->devices));
    memset(settingsData->controls, 0, sizeof(settingsData->controls));
    memset(settingsData->presets, 0, sizeof(settingsData->presets));

    memcpy(settingsData->devices, &g_comImageReceiveBuffer[bufferPosition], devicesSize);
    bufferPosition += devicesSize;
    memcpy(settingsData->controls, &g_comImageReceiveBuffer[bufferPosition], controlsSize);
    bufferPosition += controlsSize;
    memcpy(settingsData->presets, &g_comImageReceiveBuffer[bufferPosition], presetsSize);

    pcSettingsDisplay[1].maximum = settingsData->settings.display_device_count - 1;
    pcSettingsMisc[2].maximum = settingsData->settings.control_count - 1;
    pcSettingsMisc[3].maximum = settingsData->settings.preset_count - 1;
  }

  return true;
}





/*
  sendResponse
  ---------------------------------------------------
  Send the control data and setting changes to the PC.
  ---------------------------------------------------
*/
bool sendResponse()
{
  // Read controls
  SceCtrlData controlData;

  sceCtrlPeekBufferPositive(&controlData, 1);

  if (g_menuActive)
  {
    controlData.Buttons = 0;
    controlData.Lx = 127;
    controlData.Ly = 127;
  }

  oskCheckDanzeffState(&controlData);

  // Omit buttons that are still pressed after exiting the menu
  if (g_menuButtonsOnExit > 0)
  {
    if (g_menuButtonsOnExit == controlData.Buttons)
      controlData.Buttons &= ~g_menuButtonsOnExit;
    else
      g_menuButtonsOnExit = 0;
  }

  bool result = false;

  l_response.response.magic = PSPDISP_COM_HEADER_MAGIC;
  l_response.response.flags = (g_comUpdateSettings ? PSPDISP_COM_FLAG_CONTAINS_SETTINGS_DATA : 0);
  if (g_comForceScreenUpdate)
    l_response.response.flags = l_response.response.flags | PSPDISP_COM_FLAG_FORCE_UPDATE;
  l_response.response.buttons = controlData.Buttons;
  l_response.response.analog_x = controlData.Lx;
  l_response.response.analog_y = controlData.Ly;

  sceKernelDcacheWritebackInvalidateAll();

  if (g_comUpdateSettings || (g_comMode == COM_MODE_USB))
  {
    l_response.settings = configPCSettings.settings;

    result = (writeData(&l_response, sizeof(PSPdispComSettingsResponse)) == sizeof(PSPdispComSettingsResponse));

    if (g_comUpdateSettings)
    {
      g_comForceScreenUpdate = false;

      if (result)
        g_comUpdateSettings = false;
    }
  }
  else
  {
    result = (writeData(&l_response, sizeof(PSPdispComResponseHeader)) == sizeof(PSPdispComResponseHeader));
  }

  return result;
}





/*
  comLoop
  ---------------------------------------------------
  Main loop for sending and receiving.
  ---------------------------------------------------
*/
void comLoop()
{
  PSPdispComFrameHeader frameHeader;
  memset(&frameHeader, 0, sizeof(PSPdispComFrameHeader));

  g_comRunning = true;
  g_comAudioWritePosition = 0;

  while (g_comRunning)
  {
    // Wait for the PC to send the size of the next compressed frame.
    if (!readFrameInformation(&frameHeader))
    {
      // Break if connection lost in Wlan mode
      CHECK_WLAN_RESULT
    }
    else
    {
      // Frame information received

      // Disable power save mode
      if (g_comMode == COM_MODE_USB)
        powerEnablePowerSaveMode(false);

      // Got data size, read in data.
      if (g_comAudioWritePosition >= g_audioCurrentBufferSize)
        g_comAudioWritePosition = 0;

      readFrame(&frameHeader, &(g_comAudioReceiveBuffer[g_comAudioWritePosition]), &configPCSettings);
      CHECK_WLAN_RESULT

      g_comAudioWritePosition += g_audioCurrentFrameSize;

      audioCheckIfResetIsNeeded(&frameHeader);

	    // Start the thread again
//      sceKernelWaitThreadEnd(g_compressThreadId, NULL);
      if ((frameHeader.flags & PSPDISP_COM_FLAG_CONTAINS_IMAGE_DATA) || (frameHeader.flags & PSPDISP_COM_FLAG_IMAGE_CLEAR_SCREEN))
        sceKernelStartThread(g_compressThreadId, sizeof(PSPdispComFrameHeader), &frameHeader);

      // Read and transmit the PSP controls
      sendResponse();
      CHECK_WLAN_RESULT
    }

    sceDisplayWaitVblankStart();
  }
}
