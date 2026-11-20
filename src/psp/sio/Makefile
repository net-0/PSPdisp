# This file uses the SIO_* prefix, as it's included by the Makefile of the PSP app (../Makefile), which gives it PSP_BIN_DIR and the flags of the kernel modules
#
# The kernel module of the debug output on the serial port of the PSP (sio.prx), only loaded when the app is built for it (DEBUG_SIO, see ../debug.h)
# So it's left out of the folder of the app: copy it there to use it

# The folder of this file, absolute (and not ending with /), as the paths in an included Makefile are relative to where make runs
SIO_SRC_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# The firmware the headers of PSPSDK target (its default, 6.00), the serial port functions of PSPSDK (pspdebug), and the driver of the headphone remote they call, whose port is the serial port
SIO_CPPFLAGS := $(PSP_CPPFLAGS) -D_PSP_FW_VERSION=600
SIO_LDLIBS   := -lpspdebug -lpsphprm_driver $(PSP_MODULE_LDLIBS)

ALL += $(PSP_BIN_DIR)/sio.prx

# The object of the module, compiled apart from the link (see the one of the app)
$(PSP_BIN_DIR)/sio.o: $(SIO_SRC_DIR)/main.c | $(PSP_BIN_DIR) $(DEP_DIR)
	psp-gcc $(SIO_CPPFLAGS) -MMD -MP -MF $(DEP_DIR)/$(@F).d -MT $@ $(PSP_CFLAGS) -c $< -o $@

# The exports of the module: module_start() for the system, and the functions the app calls (through its stubs, sio.S)
$(PSP_BIN_DIR)/sio-exports.c: $(SIO_SRC_DIR)/exports.exp | $(PSP_BIN_DIR)
	psp-build-exports -b $< > $@

# The ELF of the module, its imports fixed
$(PSP_BIN_DIR)/sio.elf: $(PSP_BIN_DIR)/sio.o $(PSP_BIN_DIR)/sio-exports.c
	psp-gcc $(SIO_CPPFLAGS) $(PSP_CFLAGS) $(PSP_MODULE_LDFLAGS) $^ -o $@ $(SIO_LDLIBS)
	psp-fixup-imports $@

# The module, next to the files the app is made from
$(PSP_BIN_DIR)/sio.prx: $(PSP_BIN_DIR)/sio.elf
	psp-prxgen $< $@

# Include the dependencies to build the object, the .d file is written when it's compiled
-include $(DEP_DIR)/sio.o.d
