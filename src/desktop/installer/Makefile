# This file uses the INSTALLER_* prefix to avoid conflicts with other Makefile variables when it's included
#
# The installers of the desktop, one per format, each in its folder, included by platform:
# - Windows: Inno Setup (see inno/inno.mk)
# - Linux, on Debian and its derivatives (e.g. Ubuntu), the ones with /etc/debian_version: a .deb (see deb/deb.mk), as it depends on their packages
# - Linux, on Fedora, the one with /etc/fedora-release: an RPM (see rpm/rpm.mk), as it depends on its packages

# The folder of this file, absolute (and not ending with /), as the paths in an included Makefile are relative to where make runs
INSTALLER_SRC_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

ifeq ($(PLATFORM),windows)
include $(INSTALLER_SRC_DIR)/inno/inno.mk
else ifneq ($(wildcard /etc/debian_version),)
include $(INSTALLER_SRC_DIR)/deb/deb.mk
else ifneq ($(wildcard /etc/fedora-release),)
include $(INSTALLER_SRC_DIR)/rpm/rpm.mk
endif
