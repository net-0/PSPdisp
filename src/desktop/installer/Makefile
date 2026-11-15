# This file uses the INSTALLER_* prefix to avoid conflicts with other Makefile variables when it's included
#
# The installers of the desktop, one per format, each in its folder, included by platform:
# - Windows: Inno Setup (see inno/inno.mk)

# The folder of this file, absolute (and not ending with /), as the paths in an included Makefile are relative to where make runs
INSTALLER_SRC_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

ifeq ($(PLATFORM),windows)
include $(INSTALLER_SRC_DIR)/inno/inno.mk
endif
