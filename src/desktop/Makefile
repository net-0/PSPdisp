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

# The platform the compiler builds for, which picks the libraries of the programs: Windows with MinGW (e.g. x86_64-w64-mingw32), Linux otherwise
PLATFORM := $(if $(findstring mingw,$(shell $(CC) -dumpmachine)),windows,linux)

# The extension of the programs (.exe on Windows)
EXE := $(if $(filter windows,$(PLATFORM)),.exe)

# The command that fixes the .d file of a binary, run after it's compiled (`$(DEP_FIX) <file>.d`):
# with the make of MSYS2 (whose paths are /c/...), MSYS2 gives the paths to the Windows compiler as C:/..., which it writes in the .d files,
# but that make can't read them back (the colon of the drive ends the target), so the binaries weren't rebuilt when a file they include changed
ifneq ($(and $(filter windows,$(PLATFORM)),$(filter /%,$(DESKTOP_DIR))),)
DEP_FIX := sed -i -E 's\#(^| )([A-Za-z]):/\#\1/\L\2\E/\#g'
else
DEP_FIX := :
endif

# Flags for the C compiler, the ones of the user last, so they win (e.g. `CFLAGS=-O0 make`), with the version for the programs that include settings.c
CFLAGS   := -std=gnu23 -Wall -Wextra -Wno-pragma-once-outside-header $(CFLAGS)
CPPFLAGS := $(CPPFLAGS) -DPSPDISP_VERSION='"$(VERSION)"'
LDFLAGS  := $(LDFLAGS)
LDLIBS   := $(LDLIBS)

.DEFAULT_GOAL := all

# Always out of date, so the targets that depend on it are rebuilt on each run
FORCE:

ALL :=

# The programs
include $(DESKTOP_DIR)/daemon/Makefile
include $(DESKTOP_DIR)/ui/Makefile

all: $(ALL)
