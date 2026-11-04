# This file uses the VHID_* prefix, as it's included by the Makefile of the desktop (on Windows only), which gives it BIN_DIR and FORCE,
# WINSIGN_DIR and WINSIGN_PFX_PASSWORD, the certificate that signs the driver and its catalog (see ../../../winsign.mk), and WINDRIVER_VERSION, the version of the package (see ../../../windriver.mk)
#
# The virtual HID gamepad driver package (Windows, see pspdisp-vhid.inf): the driver (pspdisp-vhid.dll), its INF, and its catalog,
# the driver and the catalog signed with the self-signed certificate of the desktop.
# The driver is built with MSVC (the Build Tools of Visual Studio, found with vswhere) against the WDK, as the WDK headers and libraries are made for it.

# The folder of this file, absolute (and not ending with /), as the paths in an included Makefile are relative to where make runs
VHID_SRC_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# The folder of the package
VHID_DIR := $(BIN_DIR)/vhid

# The folder of the Windows Kits (the SDK and the WDK), and the version of their newest SDK
VHID_KIT != cygpath -u 'C:/Program Files (x86)/Windows Kits/10'
VHID_SDK_VERSION != ls "$(VHID_KIT)/Include" 2>/dev/null | grep '^10\.' | sort -V | tail -n 1

# The folder of the tools of the WDK that make and sign the catalog (Inf2Cat and signtool)
VHID_WDK_BIN := $(VHID_KIT)/bin/$(VHID_SDK_VERSION)

# The folder of the newest MSVC of the newest Visual Studio (or its Build Tools) with the C++ tools, empty if there's none
VHID_MSVC != vs="$$("$$(cygpath -u 'C:/Program Files (x86)/Microsoft Visual Studio/Installer')/vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>/dev/null | tr -d '\r')" \
	&& test -n "$$vs" && ls -d "$$(cygpath -u "$$vs")"/VC/Tools/MSVC/* 2>/dev/null | sort -V | tail -n 1

# The version of UMDF the driver is built for, the oldest one it needs (UMDF 2.15 is Windows 10 1607), kept in sync with pspdisp-vhid.inf
VHID_UMDF_VERSION := 2.15

# The folders of the headers and the libraries for cl.exe and link.exe, as their INCLUDE and LIB (separated by : here, given to them as Windows paths separated by ;)
# (the km folder of the WDK last, only for hidport.h, the IOCTLs of the HID minidrivers)
VHID_INCLUDE := $(VHID_MSVC)/include:$(VHID_KIT)/Include/$(VHID_SDK_VERSION)/ucrt:$(VHID_KIT)/Include/$(VHID_SDK_VERSION)/um:$(VHID_KIT)/Include/$(VHID_SDK_VERSION)/shared:$(VHID_KIT)/Include/wdf/umdf/$(VHID_UMDF_VERSION):$(VHID_KIT)/Include/$(VHID_SDK_VERSION)/km
VHID_LIB := $(VHID_MSVC)/lib/x64:$(VHID_KIT)/Lib/$(VHID_SDK_VERSION)/ucrt/x64:$(VHID_KIT)/Lib/$(VHID_SDK_VERSION)/um/x64:$(VHID_KIT)/Lib/wdf/umdf/x64/$(VHID_UMDF_VERSION)

# Flags for cl.exe: C (/TC, the WDF and HID headers are C), a DLL (/LD), with the C runtime linked in (/MT, so it doesn't need the Visual C++ Redistributable)
VHID_CFLAGS := /nologo /TC /std:c17 /W3 /O2 /MT /LD \
	/D UNICODE \
	/D _UNICODE \
	/D UMDF_VERSION_MAJOR=$(word 1,$(subst ., ,$(VHID_UMDF_VERSION))) \
	/D UMDF_VERSION_MINOR=$(word 2,$(subst ., ,$(VHID_UMDF_VERSION)))
VHID_LDFLAGS := /SUBSYSTEM:WINDOWS
VHID_LDLIBS := WdfDriverStubUm.lib OneCoreUAP.lib ntdll.lib

ALL += $(VHID_DIR)/pspdisp-vhid.dll $(VHID_DIR)/pspdisp-vhid.inf $(VHID_DIR)/pspdisp-vhid.cat

# The folder of the package, created before a file is made in it (each file has it as an order-only prerequisite)
$(VHID_DIR):
	mkdir -p $@

# The driver, compiled and linked by cl.exe, then signed by signtool with the certificate of the desktop (pspdisp-winsign.pfx), before the catalog takes its hash
# (MSYS2 would turn the /options of these Windows programs into paths, so it's off for them and the paths are given as Windows ones; the .obj, .lib and .exp of the link aren't part of the package)
$(VHID_DIR)/pspdisp-vhid.dll: $(VHID_SRC_DIR)/driver.c $(VHID_SRC_DIR)/protocol.h $(WINSIGN_DIR)/pspdisp-winsign.pfx | $(VHID_DIR)
	@test -n "$(VHID_MSVC)" || { echo "MSVC wasn't found (with vswhere), install the Build Tools of Visual Studio with the C++ tools (the VCTools workload) to build the virtual HID gamepad driver" >&2; exit 1; }
	@test -d "$(VHID_WDK_BIN)" || { echo "The Windows Driver Kit (WDK) wasn't found in C:\\Program Files (x86)\\Windows Kits\\10, install it to build the virtual HID gamepad driver" >&2; exit 1; }
	INCLUDE="$$(cygpath -wp '$(VHID_INCLUDE)')" LIB="$$(cygpath -wp '$(VHID_LIB)')" MSYS2_ARG_CONV_EXCL='*' "$(VHID_MSVC)/bin/Hostx64/x64/cl.exe" \
		$(VHID_CFLAGS) \
		/Fo"$$(cygpath -w $(@:.dll=.obj))" \
		/Fe"$$(cygpath -w $@)" \
		"$$(cygpath -w $<)" \
		/link $(VHID_LDFLAGS) $(VHID_LDLIBS)
	rm -f $(@:.dll=.obj) $(@:.dll=.lib) $(@:.dll=.exp)
	MSYS2_ARG_CONV_EXCL='*' "$(VHID_WDK_BIN)/x64/signtool.exe" sign /f "$$(cygpath -w $(WINSIGN_DIR)/pspdisp-winsign.pfx)" /p "$(WINSIGN_PFX_PASSWORD)" /fd sha256 "$$(cygpath -w $@)"

# The INF, with the date and the version of the build, made again on each run (FORCE)
$(VHID_DIR)/pspdisp-vhid.inf: $(VHID_SRC_DIR)/pspdisp-vhid.inf FORCE | $(VHID_DIR)
	sed -E "s|^(DriverVer *= *)[0-9/]+,[0-9.]+|\1$$(date +%m/%d/%Y),$(WINDRIVER_VERSION)|" $< > $@

# The catalog of the INF and the driver, made by Inf2Cat for the systems of /os (the driver is only built for x64), and signed by signtool with the certificate of the desktop (pspdisp-winsign.pfx)
$(VHID_DIR)/pspdisp-vhid.cat: $(VHID_DIR)/pspdisp-vhid.inf $(VHID_DIR)/pspdisp-vhid.dll $(WINSIGN_DIR)/pspdisp-winsign.pfx | $(VHID_DIR)
	@test -d "$(VHID_WDK_BIN)" || { echo "The Windows Driver Kit (WDK) wasn't found in C:\\Program Files (x86)\\Windows Kits\\10\\bin, install it for Inf2Cat and signtool" >&2; exit 1; }
	MSYS2_ARG_CONV_EXCL='*' "$(VHID_WDK_BIN)/x86/Inf2Cat.exe" /driver:"$$(cygpath -w $(@D))" /os:10_X64
	MSYS2_ARG_CONV_EXCL='*' "$(VHID_WDK_BIN)/x64/signtool.exe" sign /f "$$(cygpath -w $(WINSIGN_DIR)/pspdisp-winsign.pfx)" /p "$(WINSIGN_PFX_PASSWORD)" /fd sha256 "$$(cygpath -w $@)"
