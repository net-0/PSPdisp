/*
  ***************************************************
  PSPdisp (c) 2008 - 2015 Jochen Schleu

  compress.h

  This software is licensed under the BSD license.
  See license.txt for details.
  ***************************************************
*/

#ifndef COMPRESS_H
#define COMPRESS_H

#include <pspsdk.h>
#include <psptypes.h>
#include <pspjpeg.h>
#include <jpeglib.h>
#include "common.h"
#include "com.h"


unsigned char* g_pixelBuffer;
SceUID g_compressThreadId;

void compressInit();
void compressTerm();
void compressClearBuffers();
void compressConvertBmp(comFrameHeader* frameHeader);
void compressTermJpegDecoding();
int compressInitJpegDecoding();
void compressDecompressJpeg(comFrameHeader* frameHeader);
void compressDecodingAndDrawingThread(SceSize args, void *argp);
void compressSwitchTargetBuffer();

#endif
