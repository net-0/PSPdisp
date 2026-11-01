# This file uses the IDDCX_* prefix, as it's included by the Makefile of the desktop (on Windows only), which gives it BIN_DIR, VERSION and FORCE,
# and WINSIGN_DIR and WINSIGN_PFX_PASSWORD, the certificate that signs the driver and its catalog (see ../../../winsign.mk)
#
# The IddCx driver package of the virtual display (Windows, see pspdisp-iddcx.inf and BUILDING.md): the driver (pspdisp-iddcx.dll), its INF, and its catalog,
# the driver and the catalog signed with the self-signed certificate of the desktop.
# The driver is built with MSVC (the Build Tools of Visual Studio, found with vswhere) against the WDK, as the WDK headers and libraries are made for it.

# The folder of this file, absolute (and not ending with /), as the paths in an included Makefile are relative to where make runs
IDDCX_SRC_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# The folder of the package
IDDCX_DIR := $(BIN_DIR)/iddcx

# The folder of the Windows Kits (the SDK and the WDK), and the version of their newest SDK
IDDCX_KIT != cygpath -u 'C:/Program Files (x86)/Windows Kits/10'
IDDCX_SDK_VERSION != ls "$(IDDCX_KIT)/Include" 2>/dev/null | grep '^10\.' | sort -V | tail -n 1

# The folder of the tools of the WDK that make and sign the catalog (Inf2Cat and signtool)
IDDCX_WDK_BIN := $(IDDCX_KIT)/bin/$(IDDCX_SDK_VERSION)

# The folder of the newest MSVC of the newest Visual Studio (or its Build Tools) with the C++ tools, empty if there's none
IDDCX_MSVC != vs="$$("$$(cygpath -u 'C:/Program Files (x86)/Microsoft Visual Studio/Installer')/vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>/dev/null | tr -d '\r')" \
	&& test -n "$$vs" && ls -d "$$(cygpath -u "$$vs")"/VC/Tools/MSVC/* 2>/dev/null | sort -V | tail -n 1

# The versions of UMDF and IddCx the driver is built for, the oldest ones it needs (UMDF 2.15 is Windows 10 1607, IddCx 1.2 is Windows 10 1709), kept in sync with pspdisp-iddcx.inf
IDDCX_UMDF_VERSION := 2.15
IDDCX_CX_VERSION := 1.2

# The folders of the headers and the libraries for cl.exe and link.exe, as their INCLUDE and LIB (separated by : here, given to them as Windows paths separated by ;)
IDDCX_INCLUDE := $(IDDCX_MSVC)/include:$(IDDCX_KIT)/Include/$(IDDCX_SDK_VERSION)/ucrt:$(IDDCX_KIT)/Include/$(IDDCX_SDK_VERSION)/um:$(IDDCX_KIT)/Include/$(IDDCX_SDK_VERSION)/shared:$(IDDCX_KIT)/Include/wdf/umdf/$(IDDCX_UMDF_VERSION):$(IDDCX_KIT)/Include/$(IDDCX_SDK_VERSION)/um/iddcx/$(IDDCX_CX_VERSION)
IDDCX_LIB := $(IDDCX_MSVC)/lib/x64:$(IDDCX_KIT)/Lib/$(IDDCX_SDK_VERSION)/ucrt/x64:$(IDDCX_KIT)/Lib/$(IDDCX_SDK_VERSION)/um/x64:$(IDDCX_KIT)/Lib/wdf/umdf/x64/$(IDDCX_UMDF_VERSION):$(IDDCX_KIT)/Lib/$(IDDCX_SDK_VERSION)/um/x64/iddcx/$(IDDCX_CX_VERSION)

# Flags for cl.exe: C++ (the IddCx headers are C++ only, see driver.cpp), a DLL (/LD), with the C runtime linked in (/MT, so it doesn't need the Visual C++ Redistributable)
IDDCX_CXXFLAGS := /nologo /std:c++17 /W3 /O2 /MT /LD \
	/D UNICODE \
	/D _UNICODE \
	/D UMDF_VERSION_MAJOR=$(word 1,$(subst ., ,$(IDDCX_UMDF_VERSION))) \
	/D UMDF_VERSION_MINOR=$(word 2,$(subst ., ,$(IDDCX_UMDF_VERSION))) \
	/D IDDCX_VERSION_MAJOR=$(word 1,$(subst ., ,$(IDDCX_CX_VERSION))) \
	/D IDDCX_VERSION_MINOR=$(word 2,$(subst ., ,$(IDDCX_CX_VERSION)))
IDDCX_LDFLAGS := /SUBSYSTEM:WINDOWS
IDDCX_LDLIBS := WdfDriverStubUm.lib IddCxStub.lib OneCoreUAP.lib ntdll.lib d3d11.lib dxgi.lib dxguid.lib avrt.lib

# The version of the package, which Windows compares to pick the newest one: DriverVer takes four numbers (v0.6.2-89-gfbedb26 gives 0.6.2.89, v0.6.2 gives 0.6.2.0)
IDDCX_VERSION_WORDS := $(subst -, ,$(subst ., ,$(patsubst v%,%,$(VERSION))))
IDDCX_PACKAGE_VERSION := $(or $(word 1,$(IDDCX_VERSION_WORDS)),0).$(or $(word 2,$(IDDCX_VERSION_WORDS)),0).$(or $(word 3,$(IDDCX_VERSION_WORDS)),0).$(or $(word 4,$(IDDCX_VERSION_WORDS)),0)

ALL += $(IDDCX_DIR)/pspdisp-iddcx.dll $(IDDCX_DIR)/pspdisp-iddcx.inf $(IDDCX_DIR)/pspdisp-iddcx.cat

# The folder of the package, created before a file is made in it (each file has it as an order-only prerequisite)
$(IDDCX_DIR):
	mkdir -p $@

# The driver, compiled and linked by cl.exe, then signed by signtool with the certificate of the desktop (pspdisp-winsign.pfx), before the catalog takes its hash
# (MSYS2 would turn the /options of these Windows programs into paths, so it's off for them and the paths are given as Windows ones; the .obj, .lib and .exp of the link aren't part of the package)
$(IDDCX_DIR)/pspdisp-iddcx.dll: $(IDDCX_SRC_DIR)/driver.cpp $(IDDCX_SRC_DIR)/protocol.h $(WINSIGN_DIR)/pspdisp-winsign.pfx | $(IDDCX_DIR)
	@test -n "$(IDDCX_MSVC)" || { echo "MSVC wasn't found (with vswhere), install the Build Tools of Visual Studio with the C++ tools (the VCTools workload) to build the IddCx driver" >&2; exit 1; }
	@test -d "$(IDDCX_WDK_BIN)" || { echo "The Windows Driver Kit (WDK) wasn't found in C:\\Program Files (x86)\\Windows Kits\\10, install it to build the IddCx driver" >&2; exit 1; }
	INCLUDE="$$(cygpath -wp '$(IDDCX_INCLUDE)')" LIB="$$(cygpath -wp '$(IDDCX_LIB)')" MSYS2_ARG_CONV_EXCL='*' "$(IDDCX_MSVC)/bin/Hostx64/x64/cl.exe" \
		$(IDDCX_CXXFLAGS) \
		/Fo"$$(cygpath -w $(@:.dll=.obj))" \
		/Fe"$$(cygpath -w $@)" \
		"$$(cygpath -w $<)" \
		/link $(IDDCX_LDFLAGS) $(IDDCX_LDLIBS)
	rm -f $(@:.dll=.obj) $(@:.dll=.lib) $(@:.dll=.exp)
	MSYS2_ARG_CONV_EXCL='*' "$(IDDCX_WDK_BIN)/x64/signtool.exe" sign /f "$$(cygpath -w $(WINSIGN_DIR)/pspdisp-winsign.pfx)" /p "$(WINSIGN_PFX_PASSWORD)" /fd sha256 "$$(cygpath -w $@)"

# The INF, with the date and the version of the build, made again on each run (FORCE)
$(IDDCX_DIR)/pspdisp-iddcx.inf: $(IDDCX_SRC_DIR)/pspdisp-iddcx.inf FORCE | $(IDDCX_DIR)
	sed -E "s|^(DriverVer *= *)[0-9/]+,[0-9.]+|\1$$(date +%m/%d/%Y),$(IDDCX_PACKAGE_VERSION)|" $< > $@

# The catalog of the INF and the driver, made by Inf2Cat for the systems of /os (the driver is only built for x64), and signed by signtool with the certificate of the desktop (pspdisp-winsign.pfx)
$(IDDCX_DIR)/pspdisp-iddcx.cat: $(IDDCX_DIR)/pspdisp-iddcx.inf $(IDDCX_DIR)/pspdisp-iddcx.dll $(WINSIGN_DIR)/pspdisp-winsign.pfx | $(IDDCX_DIR)
	@test -d "$(IDDCX_WDK_BIN)" || { echo "The Windows Driver Kit (WDK) wasn't found in C:\\Program Files (x86)\\Windows Kits\\10\\bin, install it for Inf2Cat and signtool" >&2; exit 1; }
	MSYS2_ARG_CONV_EXCL='*' "$(IDDCX_WDK_BIN)/x86/Inf2Cat.exe" /driver:"$$(cygpath -w $(@D))" /os:10_X64
	MSYS2_ARG_CONV_EXCL='*' "$(IDDCX_WDK_BIN)/x64/signtool.exe" sign /f "$$(cygpath -w $(WINSIGN_DIR)/pspdisp-winsign.pfx)" /p "$(WINSIGN_PFX_PASSWORD)" /fd sha256 "$$(cygpath -w $@)"
