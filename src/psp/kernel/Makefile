# This file uses the KERNEL_* prefix, as it's included by the Makefile of the PSP app (../Makefile), which gives it PSP_BIN_DIR, PSP_GAME_DIR and the flags of the kernel modules
#
# The kernel module of the app (kernel.prx), for what it can only do in kernel mode (e.g. the brightness of the screen, the LED of the WLAN), loaded by the app from its folder

# The folder of this file, absolute (and not ending with /), as the paths in an included Makefile are relative to where make runs
KERNEL_SRC_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# The firmware the headers of PSPSDK target (its default, 6.00), and the display and controller drivers it calls
KERNEL_CPPFLAGS := $(PSP_CPPFLAGS) -D_PSP_FW_VERSION=600
KERNEL_LDLIBS   := -lpspdisplay_driver -lpspctrl_driver $(PSP_MODULE_LDLIBS)

ALL += $(PSP_GAME_DIR)/kernel.prx

# The object of the module, compiled apart from the link (see the one of the app)
$(PSP_BIN_DIR)/kernel.o: $(KERNEL_SRC_DIR)/main.c | $(PSP_BIN_DIR) $(DEP_DIR)
	psp-gcc $(KERNEL_CPPFLAGS) -MMD -MP -MF $(DEP_DIR)/$(@F).d -MT $@ $(PSP_CFLAGS) -c $< -o $@

# The exports of the module: module_start() for the system, and the functions the app calls (through its stubs, kernel.S)
$(PSP_BIN_DIR)/kernel-exports.c: $(KERNEL_SRC_DIR)/exports.exp | $(PSP_BIN_DIR)
	psp-build-exports -b $< > $@

# The ELF of the module, with the stub of the syscon driver it calls, its imports fixed
$(PSP_BIN_DIR)/kernel.elf: $(PSP_BIN_DIR)/kernel.o $(PSP_BIN_DIR)/kernel-exports.c $(KERNEL_SRC_DIR)/syscon.S
	psp-gcc $(KERNEL_CPPFLAGS) $(PSP_CFLAGS) $(PSP_MODULE_LDFLAGS) $^ -o $@ $(KERNEL_LDLIBS)
	psp-fixup-imports $@

# The module, in the folder of the app
$(PSP_GAME_DIR)/kernel.prx: $(PSP_BIN_DIR)/kernel.elf | $(PSP_GAME_DIR)
	psp-prxgen $< $@

# Include the dependencies to build the object, the .d file is written when it's compiled
-include $(DEP_DIR)/kernel.o.d
