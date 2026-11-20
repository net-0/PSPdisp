# This file uses the USBHOSTFS_* prefix, as it's included by the Makefile of the PSP app (../Makefile), which gives it PSP_BIN_DIR, PSP_GAME_DIR and the flags of the kernel modules
#
# The USB driver of the app (usbhostfs.prx, from PSPLink), the USB device the desktop talks to, loaded by the app from its folder

# The folder of this file, absolute (and not ending with /), as the paths in an included Makefile are relative to where make runs
USBHOSTFS_SRC_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# The firmware the headers of PSPSDK target (its default, 6.00), its own printf() (not the one of the C library, which it doesn't link), and the USB bus driver it calls
USBHOSTFS_CPPFLAGS := $(PSP_CPPFLAGS) -D_PSP_FW_VERSION=600
USBHOSTFS_CFLAGS   := $(PSP_CFLAGS) -fno-builtin-printf
USBHOSTFS_LDLIBS   := -lpspusbbus_driver $(PSP_MODULE_LDLIBS)

ALL += $(PSP_GAME_DIR)/usbhostfs.prx

# The object of the module, main.c compiled with host_driver.c it includes, apart from the link (see the one of the app)
$(PSP_BIN_DIR)/usbhostfs.o: $(USBHOSTFS_SRC_DIR)/main.c | $(PSP_BIN_DIR) $(DEP_DIR)
	psp-gcc $(USBHOSTFS_CPPFLAGS) -MMD -MP -MF $(DEP_DIR)/$(@F).d -MT $@ $(USBHOSTFS_CFLAGS) -c $< -o $@

# The exports of the module: module_start() for the system, and the functions the app calls (through its stubs, USBHostFS.S)
$(PSP_BIN_DIR)/usbhostfs-exports.c: $(USBHOSTFS_SRC_DIR)/exports.exp | $(PSP_BIN_DIR)
	psp-build-exports -b $< > $@

# The ELF of the module, with the function that sets the k1 register (kmode.S), its imports fixed
$(PSP_BIN_DIR)/usbhostfs.elf: $(PSP_BIN_DIR)/usbhostfs.o $(PSP_BIN_DIR)/usbhostfs-exports.c $(USBHOSTFS_SRC_DIR)/kmode.S
	psp-gcc $(USBHOSTFS_CPPFLAGS) $(USBHOSTFS_CFLAGS) $(PSP_MODULE_LDFLAGS) $^ -o $@ $(USBHOSTFS_LDLIBS)
	psp-fixup-imports $@

# The module, in the folder of the app
$(PSP_GAME_DIR)/usbhostfs.prx: $(PSP_BIN_DIR)/usbhostfs.elf | $(PSP_GAME_DIR)
	psp-prxgen $< $@

# Include the dependencies to build the object, the .d file is written when it's compiled
-include $(DEP_DIR)/usbhostfs.o.d
