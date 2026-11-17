# This file uses the DEB_* prefix, as it's included by the Makefile of the desktop (on Debian and its derivatives only), which gives it BIN_DIR, VERSION and the files it packages:
# the programs, with the files of the app (UI_SRC_DIR), of the daemon (DAEMON_SRC_DIR) and of the EVDI module (in DESKTOP_SRC_DIR)
#
# The .deb of PSPdisp for Debian and its derivatives (e.g. Ubuntu), made by dpkg-deb from a folder staged here, as it's installed:
# - usr/bin: the programs
# - usr/share/applications, usr/share/icons: the desktop entry of the app and its icon
# - usr/lib/udev/rules.d: the udev rule, which gives the PSP, /dev/uinput and /dev/uhid to the user at the computer
# - usr/lib/modprobe.d, usr/lib/modules-load.d: the EVDI module loaded at boot with a device, so the daemon only asks for the password to create one when none is free
# - etc/xdg/autostart: the autostart entry of the daemon, which starts it when a user signs in
# - usr/share/doc/pspdisp/copyright: the license
# - DEBIAN: postinst, which applies the udev rule at once, conffiles, which keeps the changes to the autostart entry on upgrade (as Debian wants for the files of /etc), and the control file, written when the .deb is made (see below)
# `make` makes it in build/deb, if dpkg-shlibdeps is installed (otherwise it's skipped, with a note).

# The folder of this file, absolute (and not ending with /), as the paths in an included Makefile are relative to where make runs
DEB_SRC_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# The folder of the files of the EVDI kernel module (its options, and its loading at boot), next to its display connector
DEB_EVDI_SRC_DIR := $(DESKTOP_SRC_DIR)/display/connector/evdi

# The folder of the .deb, and of the folder it's made from
DEB_DIR := $(BIN_DIR)/deb
DEB_STAGE_DIR := $(DEB_DIR)/stage

# The file touched once the folder is staged, the target of the folder: next to it, not in it, as dpkg-deb packages all the folder has
DEB_STAGE := $(DEB_STAGE_DIR).staged

# The version of the package, from the one of `git describe`, in the order of Debian: v0.7.0 becomes 0.7.0, and v0.7.0-3-gf74cc8f becomes 0.7.0+3.gf74cc8f (after 0.7.0, before 0.7.1)
DEB_VERSION != echo '$(VERSION)' | sed -e 's/^v//' -e 's/-/+/' -e 's/-/./'

# The architecture of the package, the one of the system (e.g. amd64)
DEB_ARCH != dpkg --print-architecture

# The .deb, named as Debian names them
DEB_PACKAGE := $(DEB_DIR)/pspdisp_$(DEB_VERSION)_$(DEB_ARCH).deb

# The .deb only with dpkg-shlibdeps (of dpkg-dev, which build-essential installs), so the programs are still made without it
ifneq ($(shell command -v dpkg-shlibdeps),)
ALL += $(DEB_PACKAGE)
else
$(info dpkg-shlibdeps wasn't found, so the .deb isn't made: install it to make it (e.g. sudo apt install dpkg-dev))
endif

# The folder of the .deb, created before a file is made in it (each file has it as an order-only prerequisite)
$(DEB_DIR):
	mkdir -p $@

# The folder the .deb is made from, staged again from scratch when what it has changes, as it's installed (see the top of this file), but the control file
# A folder's date doesn't tell when its files changed, so its target is a file touched once it's all staged
$(DEB_STAGE): $(BIN_DIR)/pspdisp-ui $(BIN_DIR)/pspdisp-daemon $(UI_SRC_DIR)/com.pspdisp.ui.desktop $(UI_SRC_DIR)/com.pspdisp.ui.svg \
		$(DAEMON_SRC_DIR)/com.pspdisp.daemon.desktop $(DAEMON_SRC_DIR)/60-pspdisp.rules $(DEB_EVDI_SRC_DIR)/modprobe.conf $(DEB_EVDI_SRC_DIR)/modules-load.conf \
		$(DEB_SRC_DIR)/postinst $(DEB_SRC_DIR)/deb.mk | $(DEB_DIR)
	rm -rf $(DEB_STAGE_DIR)
	install -Dm755 -s -t $(DEB_STAGE_DIR)/usr/bin $(BIN_DIR)/pspdisp-ui $(BIN_DIR)/pspdisp-daemon
	install -Dm644 -t $(DEB_STAGE_DIR)/usr/share/applications $(UI_SRC_DIR)/com.pspdisp.ui.desktop
	install -Dm644 -t $(DEB_STAGE_DIR)/usr/share/icons/hicolor/scalable/apps $(UI_SRC_DIR)/com.pspdisp.ui.svg
	install -Dm644 -t $(DEB_STAGE_DIR)/usr/lib/udev/rules.d $(DAEMON_SRC_DIR)/60-pspdisp.rules
	install -Dm644 $(DEB_EVDI_SRC_DIR)/modprobe.conf $(DEB_STAGE_DIR)/usr/lib/modprobe.d/00-pspdisp.conf
	install -Dm644 $(DEB_EVDI_SRC_DIR)/modules-load.conf $(DEB_STAGE_DIR)/usr/lib/modules-load.d/pspdisp.conf
	install -Dm644 -t $(DEB_STAGE_DIR)/etc/xdg/autostart $(DAEMON_SRC_DIR)/com.pspdisp.daemon.desktop
	install -Dm644 $(DESKTOP_SRC_DIR)/../../LICENSE $(DEB_STAGE_DIR)/usr/share/doc/pspdisp/copyright
	install -Dm755 -t $(DEB_STAGE_DIR)/DEBIAN $(DEB_SRC_DIR)/postinst
	echo /etc/xdg/autostart/com.pspdisp.daemon.desktop > $(DEB_STAGE_DIR)/DEBIAN/conffiles
	touch $@

# The .deb, made by dpkg-deb from the staged folder, with its control file: control with the version, and what's known once it's staged, the size of the files
# and the packages of the libraries the programs link (dpkg-shlibdeps reads debian/control from the folder it runs in, which it needs to exist, so it runs in DEB_DIR with an empty one)
# The control file is written here, not when it's staged, as the version changes with each commit and the staged files don't (a new version is a new .deb, by its name)
# The files belong to root in the .deb (--root-owner-group), not to the user who makes it
$(DEB_PACKAGE): $(DEB_STAGE) $(DEB_SRC_DIR)/control | $(DEB_DIR)
	mkdir -p $(DEB_DIR)/debian
	touch $(DEB_DIR)/debian/control
	size=$$(du -sk --exclude=DEBIAN $(DEB_STAGE_DIR) | cut -f1) \
	&& shlibs=$$(cd $(DEB_DIR) && dpkg-shlibdeps -O $(DEB_STAGE_DIR)/usr/bin/pspdisp-ui $(DEB_STAGE_DIR)/usr/bin/pspdisp-daemon) \
	&& sed -e '/^#/d' \
		-e 's#@VERSION@#$(DEB_VERSION)#' \
		-e 's#@ARCH@#$(DEB_ARCH)#' \
		-e "s#@INSTALLED_SIZE@#$$size#" \
		-e "s#@SHLIBS@#$${shlibs#shlibs:Depends=}#" \
		$(DEB_SRC_DIR)/control > $(DEB_STAGE_DIR)/DEBIAN/control
	dpkg-deb --root-owner-group --build $(DEB_STAGE_DIR) $@
