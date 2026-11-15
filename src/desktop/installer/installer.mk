# This file uses the INSTALLER_* prefix, as it's included by the Makefile of the desktop (on Windows only), which gives it BIN_DIR, VERSION, FORCE, CFLAGS and the files it packages:
# the programs, the driver packages (IDDCX_DIR, VHID_DIR, WINUSB_DIR, and WINDRIVER_VERSION), the certificate (WINSIGN_DIR) and the icon (WINICON_DIR)
#
# The installer of PSPdisp for Windows (pspdisp.iss), made by Inno Setup from a folder staged here, as it's installed (see the top of pspdisp.iss):
# the programs with the DLLs of MSYS2 they need, the data of GTK, the driver packages with pspdisp-driver.exe that installs them (driver.c), and the certificate.
# `make` makes it in build/installer, if Inno Setup is installed (otherwise it's skipped, with a note).

# The folder of this file, absolute (and not ending with /), as the paths in an included Makefile are relative to where make runs
INSTALLER_SRC_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# The folder of the installer, and of the folder it's made from
INSTALLER_DIR := $(BIN_DIR)/installer
INSTALLER_STAGE_DIR := $(INSTALLER_DIR)/stage

# The file touched once the folder is staged, the target of the folder (the installer doesn't package it)
INSTALLER_STAGE := $(INSTALLER_STAGE_DIR)/.staged

# The folder of MSYS2 the programs are built with (e.g. /ucrt64), with the DLLs (bin) and the data (lib, share) of GTK
INSTALLER_PREFIX != cygpath -u "$$(pkg-config --variable=prefix gtk4)"

# The compiler of Inno Setup: in the PATH, or where its installer puts it (for every user, or for one), the newest one; empty if there's none
INSTALLER_ISCC != command -v ISCC.exe 2>/dev/null \
	|| ls -d "$$(cygpath -u 'C:/Program Files (x86)')"/Inno\ Setup\ */ISCC.exe "$$(cygpath -u 'C:/Program Files')"/Inno\ Setup\ */ISCC.exe "$$(cygpath -u "$$LOCALAPPDATA")"/Programs/Inno\ Setup\ */ISCC.exe 2>/dev/null | sort -V | tail -n 1

# The data of GTK it needs: its GSettings schemas (not the ones of its demo), the icon themes it falls back to, and the SVG loader of gdk-pixbuf (the icon of the app is an SVG)
INSTALLER_SCHEMAS := $(wildcard $(INSTALLER_PREFIX)/share/glib-2.0/schemas/org.gtk.gtk4.*.gschema.xml)
INSTALLER_PIXBUF_DIR := lib/gdk-pixbuf-2.0/2.10.0
INSTALLER_PIXBUF_LOADER := pixbufloader_svg.dll

# The programs of GLib that the app runs: the helpers of g_spawn_*(), to start the daemon, and gdbus.exe, the session bus GLib starts on Windows,
# which GtkApplication needs to know that a second start is the same app (it shows the window already there)
INSTALLER_GLIB_PROGRAMS := $(wildcard $(INSTALLER_PREFIX)/bin/gspawn-win64-helper*$(EXE)) $(INSTALLER_PREFIX)/bin/gdbus$(EXE)

# The installer, named with the version (as `git describe` gives it)
INSTALLER_SETUP := $(INSTALLER_DIR)/PSPdisp-$(VERSION)-setup$(EXE)

ALL += $(INSTALLER_DIR)/pspdisp-driver$(EXE)

# The installer only with Inno Setup, so the programs and the drivers are still made without it (e.g. in the CI)
ifneq ($(INSTALLER_ISCC),)
ALL += $(INSTALLER_SETUP)
else
$(info Inno Setup (ISCC.exe) wasn't found, so the installer isn't made: install it to make it (e.g. winget install JRSoftware.InnoSetup))
endif

# The folder of the installer, created before a file is made in it (each file has it as an order-only prerequisite)
$(INSTALLER_DIR):
	mkdir -p $@

# The program that installs and removes the driver packages (see driver.c), a console program run hidden by the installer, also usable as `devcon` (as an administrator)
$(INSTALLER_DIR)/pspdisp-driver$(EXE): $(INSTALLER_SRC_DIR)/driver.c | $(INSTALLER_DIR)
	$(CC) $(CFLAGS) -municode $< -o $@ -lsetupapi -lnewdev

# The folder the installer is made from, staged again from scratch when what it has changes, as it's installed:
# - bin: the programs, with the programs of GLib, and the DLLs of MSYS2 they all and the SVG loader need (as `ldd` lists them)
# - lib, share: the data of GTK, found from the folder above bin (its schemas compiled, its icon themes with the icon of the app, and the SVG loader with its entry of loaders.cache, whose paths are relative)
# - drivers: the driver packages, and pspdisp-driver.exe
# - the certificate, the license and the icon of the installer
# A folder's date doesn't tell when its files changed, so its target is a file touched once it's all staged (not when MSYS2 updates its DLLs, `touch` a program then)
$(INSTALLER_STAGE): $(BIN_DIR)/pspdisp-ui$(EXE) $(BIN_DIR)/pspdisp-daemon$(EXE) $(INSTALLER_DIR)/pspdisp-driver$(EXE) \
		$(IDDCX_DIR)/pspdisp-iddcx.cat $(VHID_DIR)/pspdisp-vhid.cat $(WINUSB_DIR)/pspdisp-winusb.cat $(WINSIGN_DIR)/pspdisp-winsign.cer $(WINICON_DIR)/com.pspdisp.ui.ico \
		$(INSTALLER_SRC_DIR)/installer.mk | $(INSTALLER_DIR)
	rm -rf $(@D)
	mkdir -p $(@D)/bin $(@D)/drivers $(@D)/share/glib-2.0/schemas $(@D)/share/icons/hicolor/scalable/apps $(@D)/$(INSTALLER_PIXBUF_DIR)/loaders
	cp $(BIN_DIR)/pspdisp-ui$(EXE) $(BIN_DIR)/pspdisp-daemon$(EXE) $(INSTALLER_GLIB_PROGRAMS) $(@D)/bin/
	cp $(INSTALLER_PREFIX)/$(INSTALLER_PIXBUF_DIR)/loaders/$(INSTALLER_PIXBUF_LOADER) $(@D)/$(INSTALLER_PIXBUF_DIR)/loaders/
	awk -v RS= -v ORS='\n\n' '/$(INSTALLER_PIXBUF_LOADER)/' $(INSTALLER_PREFIX)/$(INSTALLER_PIXBUF_DIR)/loaders.cache > $(@D)/$(INSTALLER_PIXBUF_DIR)/loaders.cache
	for file in $(@D)/bin/*$(EXE) $(@D)/$(INSTALLER_PIXBUF_DIR)/loaders/*.dll; do ldd "$$file"; done \
		| awk '{ print $$3 }' | grep -i '^$(INSTALLER_PREFIX)/bin/' | sort -u | xargs cp -t $(@D)/bin/
	cp $(INSTALLER_SCHEMAS) $(@D)/share/glib-2.0/schemas/
	glib-compile-schemas $(@D)/share/glib-2.0/schemas
	cp -r $(INSTALLER_PREFIX)/share/icons/Adwaita $(@D)/share/icons/
	cp $(INSTALLER_PREFIX)/share/icons/hicolor/index.theme $(@D)/share/icons/hicolor/
	cp $(UI_SRC_DIR)/com.pspdisp.ui.svg $(@D)/share/icons/hicolor/scalable/apps/
	gtk4-update-icon-cache -q -f -t $(@D)/share/icons/hicolor
	cp -r $(IDDCX_DIR) $(VHID_DIR) $(WINUSB_DIR) $(@D)/drivers/
	cp $(INSTALLER_DIR)/pspdisp-driver$(EXE) $(@D)/drivers/
	cp $(WINSIGN_DIR)/pspdisp-winsign.cer $(WINICON_DIR)/com.pspdisp.ui.ico $(DESKTOP_SRC_DIR)/../../LICENSE $(@D)/
	touch $@

# The installer, made by Inno Setup from the staged folder (the version of Windows, of 4 numbers, is the one of the driver packages)
# (MSYS2 would turn the /options of ISCC into paths, so it's off for it and the paths are given as Windows ones)
$(INSTALLER_SETUP): $(INSTALLER_STAGE) $(INSTALLER_SRC_DIR)/pspdisp.iss | $(INSTALLER_DIR)
	MSYS2_ARG_CONV_EXCL='*' "$(INSTALLER_ISCC)" /Qp \
		/DAppVersion=$(VERSION) \
		/DFileVersion=$(WINDRIVER_VERSION) \
		/DStageDir="$$(cygpath -w $(INSTALLER_STAGE_DIR))" \
		/O"$$(cygpath -w $(INSTALLER_DIR))" \
		"$$(cygpath -w $(INSTALLER_SRC_DIR)/pspdisp.iss)"
