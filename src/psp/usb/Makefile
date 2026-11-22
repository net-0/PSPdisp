# This file uses the USB_* prefix, as it's included by the Makefile of the PSP app (../Makefile), which gives it PSP_BIN_DIR, PSP_GAME_DIR and the flags of the kernel modules
#
# The USB driver of the app (usb.prx), the USB device the desktop talks to, loaded by the app from its folder

# The folder of this file, absolute (and not ending with /), as the paths in an included Makefile are relative to where make runs
USB_SRC_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# The firmware the headers of PSPSDK target (its default, 6.00), and the USB bus driver it calls
USB_CPPFLAGS := $(PSP_CPPFLAGS) -D_PSP_FW_VERSION=600
USB_LDLIBS   := -lpspusbbus_driver $(PSP_MODULE_LDLIBS)

# The stubs of the functions the app calls in the module, which it links (named after their library in exports.exp)
USB_STUBS := $(PSP_BIN_DIR)/PSPdispUSB.S

ALL += $(PSP_GAME_DIR)/usb.prx

# The object of the module, compiled apart from the link (see the one of the app)
$(PSP_BIN_DIR)/usb.o: $(USB_SRC_DIR)/usb.c | $(PSP_BIN_DIR) $(DEP_DIR)
	psp-gcc $(USB_CPPFLAGS) -MMD -MP -MF $(DEP_DIR)/$(@F).d -MT $@ $(PSP_CFLAGS) -c $< -o $@

# The exports of the module: module_start() for the system, and the functions the app calls (through its stubs)
$(PSP_BIN_DIR)/usb-exports.c: $(USB_SRC_DIR)/exports.exp | $(PSP_BIN_DIR)
	psp-build-exports -b $< > $@

# The stubs, which psp-build-exports writes in the folder it runs from
$(USB_STUBS): $(USB_SRC_DIR)/exports.exp | $(PSP_BIN_DIR)
	cd $(PSP_BIN_DIR) && psp-build-exports -s $<

# The ELF of the module, its imports fixed
$(PSP_BIN_DIR)/usb.elf: $(PSP_BIN_DIR)/usb.o $(PSP_BIN_DIR)/usb-exports.c
	psp-gcc $(USB_CPPFLAGS) $(PSP_CFLAGS) $(PSP_MODULE_LDFLAGS) $^ -o $@ $(USB_LDLIBS)
	psp-fixup-imports $@

# The module, in the folder of the app
$(PSP_GAME_DIR)/usb.prx: $(PSP_BIN_DIR)/usb.elf | $(PSP_GAME_DIR)
	psp-prxgen $< $@

# Include the dependencies to build the object, the .d file is written when it's compiled
-include $(DEP_DIR)/usb.o.d
