CFLAGS = -std=gnu23 -O3 -Wall -Wextra -Wno-pragma-once-outside-header
LDFLAGS = -Wl,--no-undefined
LDLIBS = $(shell pkg-config --libs glib-2.0) -levdi

DESKTOP_CC = gcc
PSP_CC = psp-gcc

# Where `make install` puts the programs, and what lets the daemon run as the user (see the install target)
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
LIBEXECDIR ?= $(PREFIX)/lib/pspdisp
UDEVRULESDIR ?= /etc/udev/rules.d
DATADIR ?= $(PREFIX)/share
ICONSDIR ?= $(DATADIR)/icons/hicolor
APPLICATIONSDIR ?= $(DATADIR)/applications
# polkit only reads its actions there
POLKITACTIONSDIR ?= /usr/share/polkit-1/actions

# The helper that loads the EVDI module if needed and creates an EVDI device, as root through pkexec, when the daemon can't open a device
DESKTOP_EVDI_ADD_HELPER = $(LIBEXECDIR)/pspdisp-evdi-add

# Version of the desktop app and the daemon (the old desktop app ended at 0.6.1)
# TODO: we should use git tags here and have a default
DESKTOP_VERSION = 0.7.0-dev

# The version is given to the programs that include settings.c
DESKTOP_VERSION_CPPFLAGS = -DPSPDISP_VERSION='"$(DESKTOP_VERSION)"'

# The keyboard layout of the desktop, for the virtual keyboard to type the characters of the PSP (see controller/input/keymap.c), with every program that includes controller/input.c
DESKTOP_KEYMAP_PACKAGES = xkbcommon xkbcommon-x11 xcb wayland-client

# The display connector is compiled in by connector.c (EVDI on Linux), so its library is linked with the program
DESKTOP_DC_CPPFLAGS = -DPSPDISP_EVDI_ADD_HELPER='"$(DESKTOP_EVDI_ADD_HELPER)"'
DESKTOP_DC_LDLIBS = -levdi

DESKTOP_TD_CPPFLAGS = $(shell pkg-config --cflags glib-2.0 gio-2.0 gdk-pixbuf-2.0) $(DESKTOP_DC_CPPFLAGS)
DESKTOP_TD_LDLIBS = $(shell pkg-config --libs glib-2.0 gio-2.0 gdk-pixbuf-2.0) $(DESKTOP_DC_LDLIBS)

# Test of the display without the PSP: saves a frame each second on ./build/display_pictures/
build-test-display:
	@mkdir -p ./build/
	@$(DESKTOP_CC) $(DESKTOP_TD_CPPFLAGS) $(CFLAGS) $(LDFLAGS) ./src/desktop/test_display.c -o ./build/test_display $(DESKTOP_TD_LDLIBS)

# Runs from ./build/, so the pictures are saved there
run-test-display: build-test-display
	@cd ./build/ && ./test_display

DESKTOP_TA_CPPFLAGS = $(shell pkg-config --cflags glib-2.0 libpipewire-0.3)
DESKTOP_TA_LDLIBS = $(shell pkg-config --libs glib-2.0 libpipewire-0.3)

# Test of the audio and its connector without the PSP: records the sound for the PSP into ./build/test_audio.wav, until Ctrl+C
build-test-audio:
	@mkdir -p ./build/
	@$(DESKTOP_CC) $(DESKTOP_TA_CPPFLAGS) $(CFLAGS) $(LDFLAGS) ./src/desktop/test_audio.c -o ./build/test_audio $(DESKTOP_TA_LDLIBS)

# Runs from ./build/, so the recording is saved there
run-test-audio: build-test-audio
	@cd ./build/ && ./test_audio

DESKTOP_TC_CPPFLAGS = $(shell pkg-config --cflags glib-2.0 $(DESKTOP_KEYMAP_PACKAGES)) $(DESKTOP_VERSION_CPPFLAGS)
DESKTOP_TC_LDLIBS = $(shell pkg-config --libs glib-2.0 $(DESKTOP_KEYMAP_PACKAGES)) -lm

# Test of the virtual controller without the PSP: presses each PSP button in turn, then turns the analog stick, until Ctrl+C
build-test-controller:
	@mkdir -p ./build/
	@$(DESKTOP_CC) $(DESKTOP_TC_CPPFLAGS) $(CFLAGS) $(LDFLAGS) ./src/desktop/test_controller.c -o ./build/test_controller $(DESKTOP_TC_LDLIBS)

# Needs to write to /dev/uinput (given by the udev rule of `make install`, or the input group), watch it with a gamepad tester (e.g. evtest)
run-test-controller: build-test-controller
	@./build/test_controller

DESKTOP_DAEMON_CPPFLAGS = $(shell pkg-config --cflags glib-2.0 gio-2.0 gusb libpipewire-0.3 libturbojpeg $(DESKTOP_KEYMAP_PACKAGES)) $(DESKTOP_DC_CPPFLAGS) $(DESKTOP_VERSION_CPPFLAGS)
DESKTOP_DAEMON_LDLIBS = $(shell pkg-config --libs glib-2.0 gio-2.0 gusb libpipewire-0.3 libturbojpeg $(DESKTOP_KEYMAP_PACKAGES)) $(DESKTOP_DC_LDLIBS) -lm

# The daemon: streams the desktop (a virtual display and the sound) to a PSP in USB or WLAN mode, and its controls to a virtual gamepad, keyboard or mouse
build-daemon:
	@mkdir -p ./build/
	@$(DESKTOP_CC) $(DESKTOP_DAEMON_CPPFLAGS) $(CFLAGS) $(LDFLAGS) ./src/desktop/daemon/main.c -o ./build/pspdisp-daemon $(DESKTOP_DAEMON_LDLIBS)

# Runs until Ctrl+C, as the user, with the settings of the user (~/.config/pspdisp), see the README for what it needs (`make install`, and the EVDI device)
run-daemon: build-daemon
	@./build/pspdisp-daemon

DESKTOP_UI_CPPFLAGS = $(shell pkg-config --cflags gtk4 $(DESKTOP_KEYMAP_PACKAGES)) $(DESKTOP_VERSION_CPPFLAGS)
DESKTOP_UI_LDLIBS = $(shell pkg-config --libs gtk4 $(DESKTOP_KEYMAP_PACKAGES))

# The desktop app: the settings of PSPdisp, shared with the daemon (~/.config/pspdisp), and what the daemon is doing
build-ui:
	@mkdir -p ./build/
	@$(DESKTOP_CC) $(DESKTOP_UI_CPPFLAGS) $(CFLAGS) $(LDFLAGS) ./src/desktop/ui/main.c -o ./build/pspdisp $(DESKTOP_UI_LDLIBS)
	@install -Dm644 ./src/desktop/ui/com.pspdisp.desktop.svg ./build/share/icons/hicolor/scalable/apps/com.pspdisp.desktop.svg

# Runs as the user, it can start the daemon from its "Streaming" page (the one of ./build/, which is first in the PATH), with its icon from ./build/share
run-ui: build-ui
	@PATH="$(CURDIR)/build:$$PATH" XDG_DATA_DIRS="$(CURDIR)/build/share:$${XDG_DATA_DIRS:-/usr/local/share:/usr/share}" ./build/pspdisp

# Both programs
build-desktop: build-daemon build-ui

# Installs both programs, the icon and the desktop entry of the desktop app, and what lets the daemon run as the user:
# - The udev rule that gives the PSP (in USB mode) and /dev/uinput to the user at the computer.
# - The helper that loads the EVDI module if needed and creates an EVDI device, and its polkit action, so polkit asks for the password when the daemon can't open an EVDI device.
# Build first as the user (make build-desktop), with the same PREFIX or LIBEXECDIR, as the daemon has the path of the helper.
install:
	@install -Dm755 ./build/pspdisp-daemon $(DESTDIR)$(BINDIR)/pspdisp-daemon
	@install -Dm755 ./build/pspdisp $(DESTDIR)$(BINDIR)/pspdisp
	@install -Dm755 ./src/desktop/display/connector/evdi/pspdisp-evdi-add $(DESTDIR)$(DESKTOP_EVDI_ADD_HELPER)
	@install -dm755 $(DESTDIR)$(POLKITACTIONSDIR)
	@sed 's|@PSPDISP_EVDI_ADD_HELPER@|$(DESKTOP_EVDI_ADD_HELPER)|' ./src/desktop/display/connector/evdi/com.pspdisp.evdi-add.policy.in > $(DESTDIR)$(POLKITACTIONSDIR)/com.pspdisp.evdi-add.policy
	@chmod 644 $(DESTDIR)$(POLKITACTIONSDIR)/com.pspdisp.evdi-add.policy
	@install -Dm644 ./src/desktop/daemon/60-pspdisp.rules $(DESTDIR)$(UDEVRULESDIR)/60-pspdisp.rules
	@install -Dm644 ./src/desktop/ui/com.pspdisp.desktop.svg $(DESTDIR)$(ICONSDIR)/scalable/apps/com.pspdisp.desktop.svg
	@install -Dm644 ./src/desktop/ui/com.pspdisp.desktop.desktop $(DESTDIR)$(APPLICATIONSDIR)/com.pspdisp.desktop.desktop
	@if [ -z "$(DESTDIR)" ]; then udevadm control --reload-rules && udevadm trigger --name-match=uinput; fi
	@if [ -z "$(DESTDIR)" ]; then gtk-update-icon-cache -q -t $(ICONSDIR) 2>/dev/null; update-desktop-database -q $(APPLICATIONSDIR) 2>/dev/null; true; fi

uninstall:
	@rm -f $(DESTDIR)$(BINDIR)/pspdisp-daemon $(DESTDIR)$(BINDIR)/pspdisp $(DESTDIR)$(DESKTOP_EVDI_ADD_HELPER)
	@rm -f $(DESTDIR)$(POLKITACTIONSDIR)/com.pspdisp.evdi-add.policy $(DESTDIR)$(UDEVRULESDIR)/60-pspdisp.rules
	@rm -f $(DESTDIR)$(ICONSDIR)/scalable/apps/com.pspdisp.desktop.svg $(DESTDIR)$(APPLICATIONSDIR)/com.pspdisp.desktop.desktop
	@rmdir --ignore-fail-on-non-empty $(DESTDIR)$(LIBEXECDIR) 2>/dev/null || true
	@if [ -z "$(DESTDIR)" ]; then udevadm control --reload-rules; fi
	@if [ -z "$(DESTDIR)" ]; then gtk-update-icon-cache -q -t $(ICONSDIR) 2>/dev/null; update-desktop-database -q $(APPLICATIONSDIR) 2>/dev/null; true; fi

hello:
	echo "Hello World"