VERSION != git describe --tags

# The folder of this file, absolute (and not ending with /), as the sub-makes run from other folders (-C)
DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

BIN_DIR = $(DIR)/build
DEP_DIR = $(BIN_DIR)/dep

.PHONY: all

# Note: "make" will run only the first entry, which must be all files
all: $(BIN_DIR)/pspdisp-daemon $(BIN_DIR)/pspdisp-ui $(BIN_DIR)/com.pspdisp.ui.svg $(BIN_DIR)/com.pspdisp.ui.desktop

# The folders of the build, created before a program is built in them (each program has them as order-only prerequisites)
$(BIN_DIR) $(DEP_DIR):
	mkdir -p $@

$(BIN_DIR)/pspdisp-daemon $(BIN_DIR)/pspdisp-ui $(BIN_DIR)/com.pspdisp.ui.svg $(BIN_DIR)/com.pspdisp.ui.desktop: $(BIN_DIR) $(DEP_DIR)
	$(MAKE) -C $(DIR)/src/desktop $@ VERSION=$(VERSION) BIN_DIR=$(BIN_DIR) DEP_DIR=$(DEP_DIR)

# # TODO: make the build more reliable
# # - A debug build (-O0 -g3), with the sanitizers (-fsanitize=address,undefined)
# # - Stop with a clear error when a pkg-config package is missing (make goes on now, and gcc fails on a missing header)
# # - A stamp of the flags, so a change of the flags, of PREFIX or of the version rebuilds the programs (they're only rebuilt when a file they include changes now)
# # - `install` refuses a missing or stale build (`$(MAKE) -q build-desktop`), instead of installing it as it is or building it as root
# # - PSP_CC isn't used: build psp/source from here, or remove it
# # - Keep tools/gen-compile-commands.sh in sync, it reads DESKTOP_CFLAGS and the CPPFLAGS of the daemon, the UI and the display test by name
# # - Build the Windows sources (MinGW), or move the desktop to Meson for it

# .PHONY: all build-desktop build-daemon build-ui build-test-display build-test-audio build-test-controller run-daemon run-ui run-test-display run-test-audio run-test-controller install uninstall

# # The tests without a PSP (see src/desktop/Makefile)
# build-test-display: $(DESKTOP_TEST_DISPLAY_BIN)
# build-test-audio: $(DESKTOP_TEST_AUDIO_BIN)
# build-test-controller: $(DESKTOP_TEST_CONTROLLER_BIN)

# # Runs from build/, so the pictures are saved there
# run-test-display: build-test-display
# 	@cd $(DESKTOP_TEST_DISPLAY_BIN_DIR) && ./$(DESKTOP_TEST_DISPLAY_TARGET)

# # Runs from build/, so the recording is saved there
# run-test-audio: build-test-audio
# 	@cd $(DESKTOP_TEST_AUDIO_BIN_DIR) && ./$(DESKTOP_TEST_AUDIO_TARGET)

# # Needs to write to /dev/uinput (given by the udev rule of `make install`, or the input group), watch it with a gamepad tester (e.g. evtest)
# run-test-controller: build-test-controller
# 	@./$(DESKTOP_TEST_CONTROLLER_BIN)

# # Installs both programs, the icon and the desktop entry of the desktop app, and what lets the daemon run as the user:
# # - The udev rule that gives the PSP (in USB mode) and /dev/uinput to the user at the computer.
# # Build first as the user (make build-desktop).
# install:
# 	@install -Dm755 $(DESKTOP_DAEMON_BIN) $(DESTDIR)$(BINDIR)/$(DESKTOP_DAEMON_TARGET)
# 	@install -Dm755 $(DESKTOP_UI_BIN) $(DESTDIR)$(BINDIR)/$(DESKTOP_UI_TARGET)
# 	@install -Dm644 ./src/desktop/daemon/60-pspdisp.rules $(DESTDIR)$(UDEVRULESDIR)/60-pspdisp.rules
# 	@install -Dm644 ./src/desktop/ui/com.pspdisp.ui.svg $(DESTDIR)$(ICONSDIR)/scalable/apps/com.pspdisp.ui.svg
# 	@install -Dm644 ./src/desktop/ui/com.pspdisp.ui.desktop $(DESTDIR)$(APPLICATIONSDIR)/com.pspdisp.ui.desktop
# 	@if [ -z "$(DESTDIR)" ]; then udevadm control --reload-rules && udevadm trigger --name-match=uinput; fi
# 	@if [ -z "$(DESTDIR)" ]; then gtk-update-icon-cache -q -t $(ICONSDIR) 2>/dev/null; update-desktop-database -q $(APPLICATIONSDIR) 2>/dev/null; true; fi

# uninstall:
# 	@rm -f $(DESTDIR)$(BINDIR)/$(DESKTOP_DAEMON_TARGET) $(DESTDIR)$(BINDIR)/$(DESKTOP_UI_TARGET)
# 	@rm -f $(DESTDIR)$(UDEVRULESDIR)/60-pspdisp.rules
# 	@rm -f $(DESTDIR)$(ICONSDIR)/scalable/apps/com.pspdisp.ui.svg $(DESTDIR)$(APPLICATIONSDIR)/com.pspdisp.ui.desktop
# 	@if [ -z "$(DESTDIR)" ]; then udevadm control --reload-rules; fi
# 	@if [ -z "$(DESTDIR)" ]; then gtk-update-icon-cache -q -t $(ICONSDIR) 2>/dev/null; update-desktop-database -q $(APPLICATIONSDIR) 2>/dev/null; true; fi
