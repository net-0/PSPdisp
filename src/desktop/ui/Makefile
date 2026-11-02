# This file uses the UI_* prefix to avoid conflicts with other Makefile variables when it's included

# The folder of this file, absolute (and not ending with /), as the paths in an included Makefile are relative to where make runs
UI_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# CC has a default of make (cc), so check that it was given (on the command line, in the environment or in a Makefile)
ifneq ($(filter default undefined,$(origin CC)),)
$(error CC is not set, give the C compiler (e.g. make CC=gcc), or run make from the root of the repository)
endif
ifeq ($(strip $(CC)),)
$(error CC is empty, give the C compiler (e.g. make CC=gcc))
endif

ifeq ($(strip $(BIN_DIR)),)
$(error BIN_DIR is not set, give the folder of the binaries (e.g. make BIN_DIR=build), or run make from the root of the repository)
endif
ifeq ($(strip $(DEP_DIR)),)
$(error DEP_DIR is not set, give the folder of the .d files (e.g. make DEP_DIR=build/deps), or run make from the root of the repository)
endif

# The libraries: GTK, then by platform:
# - Linux: the keyboard layout of the desktop (see controller/input/keymap.c)
# - Windows: the addresses of the "WLAN" page (IP Helper, see page/wlan.c)
UI_PACKAGES := gtk4
ifeq ($(PLATFORM),windows)
UI_PLATFORM_LDLIBS := -liphlpapi
else
UI_PACKAGES += xkbcommon xkbcommon-x11 xcb wayland-client
UI_PLATFORM_LDLIBS :=
endif

# Flags for the C compiler
UI_CFLAGS   := $(CFLAGS)
UI_CPPFLAGS := $(CPPFLAGS) $(shell pkg-config --cflags $(UI_PACKAGES))
UI_LDFLAGS  := $(LDFLAGS)
UI_LDLIBS   := $(LDLIBS) $(shell pkg-config --libs $(UI_PACKAGES)) $(UI_PLATFORM_LDLIBS)

ALL += $(BIN_DIR)/pspdisp-ui$(EXE) $(BIN_DIR)/com.pspdisp.ui.svg $(BIN_DIR)/com.pspdisp.ui.desktop

# The binary of the app
# The .d file is fixed even when the build fails, as it's written before the link (e.g. on Windows, the link fails while the app runs, its .exe is locked),
# otherwise make wouldn't read it back and wouldn't rebuild the app anymore; the status of the build is kept
# On Windows, it links the icon of the programs (WINICON_OBJ, see ../winicon.mk), empty elsewhere
$(BIN_DIR)/pspdisp-ui$(EXE): $(UI_DIR)/main.c $(WINICON_OBJ) | $(BIN_DIR) $(DEP_DIR)
	$(CC) $(UI_CPPFLAGS) -MMD -MP -MF $(DEP_DIR)/$(@F).d -MT $@ $(UI_CFLAGS) $(UI_LDFLAGS) $< $(WINICON_OBJ) -o $@ $(UI_LDLIBS); \
	status=$$?; \
	if [ -f $(DEP_DIR)/$(@F).d ]; then $(DEP_FIX) $(DEP_DIR)/$(@F).d; fi; \
	exit $$status

# The icon of the app
$(BIN_DIR)/com.pspdisp.ui.svg: $(UI_DIR)/com.pspdisp.ui.svg
	install -Dm644 $< $@

# The desktop entry of the app
$(BIN_DIR)/com.pspdisp.ui.desktop: $(UI_DIR)/com.pspdisp.ui.desktop
	install -Dm644 $< $@

# Include the dependencies to build the binary, the .d file is written when the binary is compiled
-include $(DEP_DIR)/pspdisp-ui$(EXE).d
