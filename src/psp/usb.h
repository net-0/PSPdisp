/*
  ***************************************************
  PSPdisp (c) 2008 - 2015 Jochen Schleu

  usb.h

  This software is licensed under the BSD license.
  See license.txt for details.
  ***************************************************
*/

#ifndef USB_H
#define USB_H


#include <pspsdk.h>
#include <pspkernel.h>
#include <pspusb.h>
#include <intraFont.h>
#include "usb/usb.h"
#include "common.h"
#include "utils.h"
#include "config.h"
#include "graphic.h"


void usbEnterLoop();
int usbInit();
void usbTerm();
void usbDeactivate();
void usbDrawWaitingForDataMessage(bool enteringPowerSaveMode, bool clearScreen, bool forceDrawing);


#endif