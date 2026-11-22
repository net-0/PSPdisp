# This file uses the PSP_* prefix to avoid conflicts with other Makefile variables
#
# The PSP app, built by the PSP toolchain (https://github.com/pspdev/pspdev), the way PSPSDK's build.mak does, in BIN_DIR/psp:
# - the app (EBOOT.PBP), and the kernel modules it loads from its folder (kernel.prx and usbhostfs.prx), in PSP/GAME/PSPdisp, as they're copied on the memory stick
# - the .zip of that PSP folder, to extract at the root of the memory stick, if zip is installed (otherwise it's skipped, with a note)
#
# Each program (the app and each kernel module) is compiled from its main.c, which includes its other .c files, then linked into an ELF, which psp-prxgen converts into a PRX

# The folder of this file, absolute (and not ending with /), as the paths in an included Makefile are relative to where make runs
PSP_SRC_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

ifeq ($(strip $(BIN_DIR)),)
$(error BIN_DIR is not set, give the folder of the binaries (e.g. make BIN_DIR=build), or run make from the root of the repository)
endif
ifeq ($(strip $(DEP_DIR)),)
$(error DEP_DIR is not set, give the folder of the .d files (e.g. make DEP_DIR=build/deps), or run make from the root of the repository)
endif
ifeq ($(strip $(VERSION)),)
$(error VERSION is not set, give the version of the app (e.g. make VERSION=0.7.0), or run make from the root of the repository)
endif

# The folders of the toolchain and of PSPSDK (its headers, libraries and link scripts)
PSPDEV := $(shell psp-config --pspdev-path 2>/dev/null)
PSPSDK := $(shell psp-config --pspsdk-path 2>/dev/null)
ifeq ($(strip $(PSPSDK)),)
$(error psp-config wasn't found, install the PSP toolchain (https://github.com/pspdev/pspdev) and add its bin folder to the PATH)
endif

# The folder of the files the app is made from, and the one of the app as it's copied on the memory stick
PSP_BIN_DIR := $(BIN_DIR)/psp
PSP_GAME_DIR := $(PSP_BIN_DIR)/PSP/GAME/PSPdisp

# Flags for psp-gcc, for all the programs:
# - -G0, as a PRX can't reach its data through the gp register, it's relocated
# - the headers of the libraries of the toolchain (e.g. png.h), which psp-gcc finds by itself, given for clangd, which reads the compile commands (see .clangd)
PSP_CFLAGS   := -std=gnu23 -O2 -G0 -Wall
PSP_CPPFLAGS := -I$(PSPDEV)/psp/include -I$(PSPSDK)/include
# A PRX (linkfile.prx, keeping the relocations), with its sections aligned to 128 bytes instead of pages, so it's smaller
PSP_LDFLAGS  := -L$(PSPSDK)/lib -Wl,-q,-T$(PSPSDK)/lib/linkfile.prx -Wl,-zmax-page-size=128

# Flags for the kernel modules: no start files, as the system calls their module_start(), and no C library
# The libraries all of them use: the bounds of their exports and imports (modinfo), the helpers of PSPSDK (e.g. pspSdkSetK1()) and the functions of the kernel, each module adds the drivers it calls
PSP_MODULE_LDFLAGS := $(PSP_LDFLAGS) -nostartfiles -nostdlib
PSP_MODULE_LDLIBS  := -lpspmodinfo -lpspsdk -lpspkernel

.DEFAULT_GOAL := all

ALL :=

# The folders of the build, created before a file is made in them (each file has its folder as an order-only prerequisite)
$(PSP_BIN_DIR) $(PSP_GAME_DIR):
	mkdir -p $@

# The kernel modules, before the app, as it links their stubs (their *_SRC_DIR)
include $(PSP_SRC_DIR)/kernel/Makefile
include $(PSP_SRC_DIR)/usbhostfs/Makefile

# Flags for the app:
# - _PSP_FW_VERSION, the firmware the headers of PSPSDK target, 3.71 as the old app
# - the version of the app, which its main menu and the menu of the PSP show
PSP_APP_CPPFLAGS := $(PSP_CPPFLAGS) -D_PSP_FW_VERSION=371 -DPSPDISP_VERSION='"$(VERSION)"'
# A PRX started as a user program (prxspecs gives its start files)
PSP_APP_LDFLAGS  := $(PSP_LDFLAGS) -specs=$(PSPSDK)/lib/prxspecs
# The libraries
PSP_APP_LDLIBS   := -lpspaudio -lpng -lz -lintrafont -lpspusb -lpspgum -lpspgu -lm -lpsppower -lpspjpeg -lpsputility -lpspkubridge -lpsprtc -ljpeg \
	-lpspdebug -lpspdisplay -lpspge -lpspctrl -lpspnet -lpspnet_apctl

# The stubs of the functions the app calls in the kernel modules, which the system links once the app loads them
PSP_APP_STUBS := $(KERNEL_SRC_DIR)/kernel.S $(USBHOSTFS_SRC_DIR)/USBHostFS.S

ALL += $(PSP_GAME_DIR)/EBOOT.PBP

# The object of the app, main.c compiled with the other .c files it includes
# Compiled apart from the link, so its compile command has main.c as its only input: clangd drops -std from a command with other inputs (e.g. the stubs)
$(PSP_BIN_DIR)/pspdisp.o: $(PSP_SRC_DIR)/main.c | $(PSP_BIN_DIR) $(DEP_DIR)
	psp-gcc $(PSP_APP_CPPFLAGS) -MMD -MP -MF $(DEP_DIR)/$(@F).d -MT $@ $(PSP_CFLAGS) -c $< -o $@

# The ELF of the app, with the stubs of the kernel modules and the exports the system looks for in a module (prxexports.o), its imports fixed
$(PSP_BIN_DIR)/pspdisp.elf: $(PSP_BIN_DIR)/pspdisp.o $(PSP_APP_STUBS) $(PSPSDK)/lib/prxexports.o
	psp-gcc $(PSP_APP_CPPFLAGS) $(PSP_CFLAGS) $(PSP_APP_LDFLAGS) $^ -o $@ $(PSP_APP_LDLIBS)
	psp-fixup-imports $@

# The PRX of the app, packed in EBOOT.PBP
$(PSP_BIN_DIR)/pspdisp.prx: $(PSP_BIN_DIR)/pspdisp.elf
	psp-prxgen $< $@

# The title of the app in the menu of the PSP, with the memory of the first PSP (MEMSIZE=0, as the old app), not the bigger one of the later models
$(PSP_BIN_DIR)/PARAM.SFO: | $(PSP_BIN_DIR)
	mksfoex -d MEMSIZE=0 'PSPdisp $(VERSION)' $@

# The app, as the PSP finds it on the memory stick, with its icon (the NULLs are the files it doesn't have: a second icon, the backgrounds, a sound, and the data of a PS1 game)
$(PSP_GAME_DIR)/EBOOT.PBP: $(PSP_BIN_DIR)/PARAM.SFO $(PSP_SRC_DIR)/ICON0.PNG $(PSP_BIN_DIR)/pspdisp.prx | $(PSP_GAME_DIR)
	pack-pbp $@ $(PSP_BIN_DIR)/PARAM.SFO $(PSP_SRC_DIR)/ICON0.PNG NULL NULL NULL NULL $(PSP_BIN_DIR)/pspdisp.prx NULL

# The images of the on-screen keyboard (Danzeff), which it loads from the graphics folder next to the app
PSP_GRAPHICS := $(patsubst $(PSP_SRC_DIR)/%,$(PSP_GAME_DIR)/%,$(wildcard $(PSP_SRC_DIR)/graphics/*.png))

ALL += $(PSP_GRAPHICS)

$(PSP_GAME_DIR)/graphics/%.png: $(PSP_SRC_DIR)/graphics/%.png
	install -Dm644 $< $@

# Include the dependencies to build the object of the app, the .d file is written when it's compiled
-include $(DEP_DIR)/pspdisp.o.d

# The .zip of the PSP folder, named as the Windows Setup, only with zip
PSP_ZIP := $(PSP_BIN_DIR)/PSPdisp-$(VERSION)-psp.zip
ifneq ($(shell command -v zip),)
ALL += $(PSP_ZIP)
else
$(info zip wasn't found, so the .zip of the PSP app isn't made: install it to make it (e.g. sudo apt install zip))
endif

# Made again from scratch, as zip only adds to an archive that exists
$(PSP_ZIP): $(PSP_GAME_DIR)/EBOOT.PBP $(PSP_GAME_DIR)/kernel.prx $(PSP_GAME_DIR)/usbhostfs.prx $(PSP_GRAPHICS)
	rm -f $@
	cd $(PSP_BIN_DIR) && zip -r -X $@ PSP

all: $(ALL)
