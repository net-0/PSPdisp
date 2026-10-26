# This file uses the DESKTOP_* prefix to avoid conflicts with other Makefile variables when it's included

# The folder of this file, absolute (and not ending with /), as the paths in an included Makefile are relative to where make runs
DESKTOP_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

ifeq ($(strip $(BIN_DIR)),)
$(error BIN_DIR is not set, give the folder of the binaries (e.g. make BIN_DIR=build), or run make from the root of the repository)
endif
ifeq ($(strip $(DEP_DIR)),)
$(error DEP_DIR is not set, give the folder of the .d files (e.g. make DEP_DIR=build/deps), or run make from the root of the repository)
endif
ifeq ($(strip $(VERSION)),)
$(error VERSION is not set, give the version of the programs (e.g. make VERSION=0.7.0), or run make from the root of the repository)
endif

# C compiler
CC := gcc

# Flags for the C compiler, the ones of the user last, so they win (e.g. `CFLAGS=-O0 make`), with the version for the programs that include settings.c
CFLAGS   := -std=gnu23 -Wall -Wextra -Wno-pragma-once-outside-header $(CFLAGS)
CPPFLAGS := $(CPPFLAGS) -DPSPDISP_VERSION='"$(VERSION)"'
LDFLAGS  := $(LDFLAGS)
LDLIBS   := $(LDLIBS)

# The programs
include $(DESKTOP_DIR)/daemon/Makefile
include $(DESKTOP_DIR)/ui/Makefile

# # The tests without a PSP, with the variables of the programs (see ui/Makefile)

# # Test of the display: saves a frame each second in build/display_pictures/
# DESKTOP_TEST_DISPLAY_CC       := $(DESKTOP_CC)
# DESKTOP_TEST_DISPLAY_TARGET   := test_display
# DESKTOP_TEST_DISPLAY_BIN_DIR  := $(DESKTOP_BIN_DIR)
# DESKTOP_TEST_DISPLAY_DEP_DIR  := $(DESKTOP_DEP_DIR)
# DESKTOP_TEST_DISPLAY_BIN      := $(DESKTOP_TEST_DISPLAY_BIN_DIR)/$(DESKTOP_TEST_DISPLAY_TARGET)
# DESKTOP_TEST_DISPLAY_DEP      := $(DESKTOP_TEST_DISPLAY_DEP_DIR)/$(DESKTOP_TEST_DISPLAY_TARGET).d
# DESKTOP_TEST_DISPLAY_PACKAGES := glib-2.0 gio-2.0 gdk-pixbuf-2.0
# DESKTOP_TEST_DISPLAY_CFLAGS   := $(DESKTOP_CFLAGS)
# DESKTOP_TEST_DISPLAY_CPPFLAGS := $(DESKTOP_CPPFLAGS) $(shell pkg-config --cflags $(DESKTOP_TEST_DISPLAY_PACKAGES)) $(DESKTOP_DC_CPPFLAGS)
# DESKTOP_TEST_DISPLAY_LDFLAGS  := $(DESKTOP_LDFLAGS)
# DESKTOP_TEST_DISPLAY_LDLIBS   := $(DESKTOP_LDLIBS) $(shell pkg-config --libs $(DESKTOP_TEST_DISPLAY_PACKAGES)) $(DESKTOP_DC_LDLIBS)

# $(DESKTOP_TEST_DISPLAY_BIN): $(DESKTOP_DIR)/test_display.c | $(DESKTOP_TEST_DISPLAY_BIN_DIR) $(DESKTOP_TEST_DISPLAY_DEP_DIR)
# 	$(DESKTOP_TEST_DISPLAY_CC) $(DESKTOP_TEST_DISPLAY_CPPFLAGS) -MMD -MP -MF $(DESKTOP_TEST_DISPLAY_DEP) -MT $@ $(DESKTOP_TEST_DISPLAY_CFLAGS) $(DESKTOP_TEST_DISPLAY_LDFLAGS) $< -o $@ $(DESKTOP_TEST_DISPLAY_LDLIBS)

# -include $(DESKTOP_TEST_DISPLAY_DEP)

# # Test of the audio and its connector: records the sound for the PSP into build/test_audio.wav, until Ctrl+C
# DESKTOP_TEST_AUDIO_CC       := $(DESKTOP_CC)
# DESKTOP_TEST_AUDIO_TARGET   := test_audio
# DESKTOP_TEST_AUDIO_BIN_DIR  := $(DESKTOP_BIN_DIR)
# DESKTOP_TEST_AUDIO_DEP_DIR  := $(DESKTOP_DEP_DIR)
# DESKTOP_TEST_AUDIO_BIN      := $(DESKTOP_TEST_AUDIO_BIN_DIR)/$(DESKTOP_TEST_AUDIO_TARGET)
# DESKTOP_TEST_AUDIO_DEP      := $(DESKTOP_TEST_AUDIO_DEP_DIR)/$(DESKTOP_TEST_AUDIO_TARGET).d
# DESKTOP_TEST_AUDIO_PACKAGES := glib-2.0 libpipewire-0.3
# DESKTOP_TEST_AUDIO_CFLAGS   := $(DESKTOP_CFLAGS)
# DESKTOP_TEST_AUDIO_CPPFLAGS := $(DESKTOP_CPPFLAGS) $(shell pkg-config --cflags $(DESKTOP_TEST_AUDIO_PACKAGES))
# DESKTOP_TEST_AUDIO_LDFLAGS  := $(DESKTOP_LDFLAGS)
# DESKTOP_TEST_AUDIO_LDLIBS   := $(DESKTOP_LDLIBS) $(shell pkg-config --libs $(DESKTOP_TEST_AUDIO_PACKAGES))

# $(DESKTOP_TEST_AUDIO_BIN): $(DESKTOP_DIR)/test_audio.c | $(DESKTOP_TEST_AUDIO_BIN_DIR) $(DESKTOP_TEST_AUDIO_DEP_DIR)
# 	$(DESKTOP_TEST_AUDIO_CC) $(DESKTOP_TEST_AUDIO_CPPFLAGS) -MMD -MP -MF $(DESKTOP_TEST_AUDIO_DEP) -MT $@ $(DESKTOP_TEST_AUDIO_CFLAGS) $(DESKTOP_TEST_AUDIO_LDFLAGS) $< -o $@ $(DESKTOP_TEST_AUDIO_LDLIBS)

# -include $(DESKTOP_TEST_AUDIO_DEP)

# # Test of the virtual controller: presses each PSP button in turn, then turns the analog stick, until Ctrl+C
# DESKTOP_TEST_CONTROLLER_CC       := $(DESKTOP_CC)
# DESKTOP_TEST_CONTROLLER_TARGET   := test_controller
# DESKTOP_TEST_CONTROLLER_BIN_DIR  := $(DESKTOP_BIN_DIR)
# DESKTOP_TEST_CONTROLLER_DEP_DIR  := $(DESKTOP_DEP_DIR)
# DESKTOP_TEST_CONTROLLER_BIN      := $(DESKTOP_TEST_CONTROLLER_BIN_DIR)/$(DESKTOP_TEST_CONTROLLER_TARGET)
# DESKTOP_TEST_CONTROLLER_DEP      := $(DESKTOP_TEST_CONTROLLER_DEP_DIR)/$(DESKTOP_TEST_CONTROLLER_TARGET).d
# DESKTOP_TEST_CONTROLLER_PACKAGES := glib-2.0 $(DESKTOP_KEYMAP_PACKAGES)
# DESKTOP_TEST_CONTROLLER_CFLAGS   := $(DESKTOP_CFLAGS)
# DESKTOP_TEST_CONTROLLER_CPPFLAGS := $(DESKTOP_CPPFLAGS) $(shell pkg-config --cflags $(DESKTOP_TEST_CONTROLLER_PACKAGES)) $(DESKTOP_VERSION_CPPFLAGS)
# DESKTOP_TEST_CONTROLLER_LDFLAGS  := $(DESKTOP_LDFLAGS)
# DESKTOP_TEST_CONTROLLER_LDLIBS   := $(DESKTOP_LDLIBS) $(shell pkg-config --libs $(DESKTOP_TEST_CONTROLLER_PACKAGES)) -lm

# $(DESKTOP_TEST_CONTROLLER_BIN): $(DESKTOP_DIR)/test_controller.c | $(DESKTOP_TEST_CONTROLLER_BIN_DIR) $(DESKTOP_TEST_CONTROLLER_DEP_DIR)
# 	$(DESKTOP_TEST_CONTROLLER_CC) $(DESKTOP_TEST_CONTROLLER_CPPFLAGS) -MMD -MP -MF $(DESKTOP_TEST_CONTROLLER_DEP) -MT $@ $(DESKTOP_TEST_CONTROLLER_CFLAGS) $(DESKTOP_TEST_CONTROLLER_LDFLAGS) $< -o $@ $(DESKTOP_TEST_CONTROLLER_LDLIBS)

# -include $(DESKTOP_TEST_CONTROLLER_DEP)
