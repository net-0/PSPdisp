# This file uses the RPM_* prefix, as it's included by the Makefile of the desktop (on Fedora only), which gives it BIN_DIR, VERSION and the files it packages:
# the programs, with the files of the app (UI_SRC_DIR), of the daemon (DAEMON_SRC_DIR) and of the EVDI module (in DESKTOP_SRC_DIR)
#
# The RPM of PSPdisp for Fedora, made by rpmbuild from a folder staged here, as it's installed:
# - usr/bin: the programs
# - usr/share/applications, usr/share/icons: the desktop entry of the app and its icon
# - usr/lib/udev/rules.d: the udev rule, which gives the PSP, /dev/uinput and /dev/uhid to the user at the computer
# - usr/lib/modprobe.d, usr/lib/modules-load.d: the EVDI module loaded at boot with a device, so the daemon only asks for the password to create one when none is free
# - usr/share/licenses/pspdisp: the license
# The spec (pspdisp.spec) has the rest: what the package is, what it depends on, and the script run once it's installed.
# `make` makes it in build/rpm, if rpmbuild is installed (otherwise it's skipped, with a note).

# The folder of this file, absolute (and not ending with /), as the paths in an included Makefile are relative to where make runs
RPM_SRC_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# The folder of the files of the EVDI kernel module (its options, and its loading at boot), next to its display connector
RPM_EVDI_SRC_DIR := $(DESKTOP_SRC_DIR)/display/connector/evdi

# The folder of the RPM, of the folder it's made from, and of the work of rpmbuild
RPM_DIR := $(BIN_DIR)/rpm
RPM_STAGE_DIR := $(RPM_DIR)/stage
RPM_BUILD_DIR := $(RPM_DIR)/rpmbuild

# The file touched once the folder is staged, the target of the folder: next to it, not in it, as the spec packages all the folder has
RPM_STAGE := $(RPM_STAGE_DIR).staged

# The version of the package, from the one of `git describe`, in the order of RPM: v0.7.0 becomes 0.7.0, and v0.7.0-3-gf74cc8f becomes 0.7.0^3.gf74cc8f (after 0.7.0, before 0.7.1)
RPM_VERSION != echo '$(VERSION)' | sed -e 's/^v//' -e 's/-/^/' -e 's/-/./'

# The release of the package, the times this version was packaged: once, as each commit is a new version
RPM_RELEASE := 1

# The architecture of the package, the one of the system (e.g. x86_64)
RPM_ARCH != rpm --eval '%{_arch}'

# The RPM, named as rpmbuild names them
RPM_PACKAGE := $(RPM_DIR)/pspdisp-$(RPM_VERSION)-$(RPM_RELEASE).$(RPM_ARCH).rpm

# The RPM only with rpmbuild (of rpm-build), so the programs are still made without it
ifneq ($(shell command -v rpmbuild),)
ALL += $(RPM_PACKAGE)
else
$(info rpmbuild wasn't found, so the RPM isn't made: install it to make it (e.g. sudo dnf install rpm-build))
endif

# The folder of the RPM, created before a file is made in it (each file has it as an order-only prerequisite)
$(RPM_DIR):
	mkdir -p $@

# The folder the RPM is made from, staged again from scratch when what it has changes, as it's installed (see the top of this file)
# A folder's date doesn't tell when its files changed, so its target is a file touched once it's all staged
$(RPM_STAGE): $(BIN_DIR)/pspdisp-ui $(BIN_DIR)/pspdisp-daemon $(UI_SRC_DIR)/com.pspdisp.ui.desktop $(UI_SRC_DIR)/com.pspdisp.ui.svg \
		$(DAEMON_SRC_DIR)/60-pspdisp.rules $(RPM_EVDI_SRC_DIR)/modprobe.conf $(RPM_EVDI_SRC_DIR)/modules-load.conf \
		$(RPM_SRC_DIR)/rpm.mk | $(RPM_DIR)
	rm -rf $(RPM_STAGE_DIR)
	install -Dm755 -s -t $(RPM_STAGE_DIR)/usr/bin $(BIN_DIR)/pspdisp-ui $(BIN_DIR)/pspdisp-daemon
	install -Dm644 -t $(RPM_STAGE_DIR)/usr/share/applications $(UI_SRC_DIR)/com.pspdisp.ui.desktop
	install -Dm644 -t $(RPM_STAGE_DIR)/usr/share/icons/hicolor/scalable/apps $(UI_SRC_DIR)/com.pspdisp.ui.svg
	install -Dm644 -t $(RPM_STAGE_DIR)/usr/lib/udev/rules.d $(DAEMON_SRC_DIR)/60-pspdisp.rules
	install -Dm644 $(RPM_EVDI_SRC_DIR)/modprobe.conf $(RPM_STAGE_DIR)/usr/lib/modprobe.d/00-pspdisp.conf
	install -Dm644 $(RPM_EVDI_SRC_DIR)/modules-load.conf $(RPM_STAGE_DIR)/usr/lib/modules-load.d/pspdisp.conf
	install -Dm644 $(DESKTOP_SRC_DIR)/../../LICENSE $(RPM_STAGE_DIR)/usr/share/licenses/pspdisp/LICENSE
	touch $@

# The RPM, made by rpmbuild from the spec, which copies the staged folder as it's installed, with the version and the release given here
# rpmbuild works in RPM_BUILD_DIR (its _topdir), and writes the RPM in RPM_DIR, named without the folder of its architecture it adds by default
# The files belong to root in the RPM, not to the user who makes it, as rpmbuild does by default
$(RPM_PACKAGE): $(RPM_STAGE) $(RPM_SRC_DIR)/pspdisp.spec | $(RPM_DIR)
	rpmbuild -bb --quiet \
		--define '_topdir $(RPM_BUILD_DIR)' \
		--define '_rpmdir $(RPM_DIR)' \
		--define '_build_name_fmt %%{NAME}-%%{VERSION}-%%{RELEASE}.%%{ARCH}.rpm' \
		--define 'pspdisp_version $(RPM_VERSION)' \
		--define 'pspdisp_release $(RPM_RELEASE)' \
		--define 'pspdisp_stage_dir $(RPM_STAGE_DIR)' \
		$(RPM_SRC_DIR)/pspdisp.spec
