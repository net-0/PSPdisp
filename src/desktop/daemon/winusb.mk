# This file uses the DAEMON_* prefix, as it's included by the Makefile of the daemon (on Windows only), which gives it DAEMON_SRC_DIR, BIN_DIR, VERSION and FORCE,
# and WINSIGN_DIR and WINSIGN_PFX_PASSWORD, the certificate that signs the catalog (see ../winsign.mk)
#
# The WinUSB driver package of the PSP (Windows, see pspdisp-winusb.inf): the INF, and its catalog, signed with the self-signed certificate of the desktop.
# Only needed until the PSP app says itself that it uses WinUSB (Microsoft OS descriptors), then remove this file and its include in the Makefile of the daemon.

# The folder of the package
WINUSB_DIR := $(BIN_DIR)/winusb

# The folder of the tools of the WDK that make and sign the catalog (Inf2Cat and signtool), from its newest version
DAEMON_WDK_BIN != ls -d "$$(cygpath -u 'C:/Program Files (x86)/Windows Kits/10/bin')"/10.* 2>/dev/null | sort -V | tail -n 1

# The version of the package, which Windows compares to pick the newest one: DriverVer takes four numbers (v0.6.2-89-gfbedb26 gives 0.6.2.89, v0.6.2 gives 0.6.2.0)
DAEMON_WINUSB_VERSION_WORDS := $(subst -, ,$(subst ., ,$(patsubst v%,%,$(VERSION))))
DAEMON_WINUSB_VERSION := $(or $(word 1,$(DAEMON_WINUSB_VERSION_WORDS)),0).$(or $(word 2,$(DAEMON_WINUSB_VERSION_WORDS)),0).$(or $(word 3,$(DAEMON_WINUSB_VERSION_WORDS)),0).$(or $(word 4,$(DAEMON_WINUSB_VERSION_WORDS)),0)

ALL += $(WINUSB_DIR)/pspdisp-winusb.inf $(WINUSB_DIR)/pspdisp-winusb.cat

# The folder of the package, created before a file is made in it (each file has it as an order-only prerequisite)
$(WINUSB_DIR):
	mkdir -p $@

# The INF, with the date and the version of the build, made again on each run (FORCE)
$(WINUSB_DIR)/pspdisp-winusb.inf: $(DAEMON_SRC_DIR)/pspdisp-winusb.inf FORCE | $(WINUSB_DIR)
	sed -E "s|^(DriverVer *= *)[0-9/]+,[0-9.]+|\1$$(date +%m/%d/%Y),$(DAEMON_WINUSB_VERSION)|" $< > $@

# The catalog of the INF, made by Inf2Cat for the systems of /os, and signed by signtool with the certificate of the desktop (pspdisp-winsign.pfx)
# (MSYS2 would turn the /options of these Windows programs into paths, so it's off for them and the paths are given as Windows ones)
$(WINUSB_DIR)/pspdisp-winusb.cat: $(WINUSB_DIR)/pspdisp-winusb.inf $(WINSIGN_DIR)/pspdisp-winsign.pfx | $(WINUSB_DIR)
	@test -d "$(DAEMON_WDK_BIN)" || { echo "The Windows Driver Kit (WDK) wasn't found in C:\\Program Files (x86)\\Windows Kits\\10\\bin, install it for Inf2Cat and signtool" >&2; exit 1; }
	MSYS2_ARG_CONV_EXCL='*' "$(DAEMON_WDK_BIN)/x86/Inf2Cat.exe" /driver:"$$(cygpath -w $(@D))" /os:10_X86,10_X64
	MSYS2_ARG_CONV_EXCL='*' "$(DAEMON_WDK_BIN)/x64/signtool.exe" sign /f "$$(cygpath -w $(WINSIGN_DIR)/pspdisp-winsign.pfx)" /p "$(WINSIGN_PFX_PASSWORD)" /fd sha256 "$$(cygpath -w $@)"
