VERSION != git describe --tags

# The folder of this file, absolute (and not ending with /), as the sub-makes run from other folders (-C)
DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

BIN_DIR = $(DIR)/build
DEP_DIR = $(BIN_DIR)/dep

# The extension of the programs: .exe when the compiler builds for Windows (MinGW, e.g. the UCRT64 shell of MSYS2)
EXE := $(if $(findstring mingw,$(shell gcc -dumpmachine)),.exe)

# Note: "make" will run only the first entry, which must be all files
all: $(BIN_DIR)/pspdisp-daemon$(EXE) $(BIN_DIR)/pspdisp-ui$(EXE) $(BIN_DIR)/com.pspdisp.ui.svg $(BIN_DIR)/com.pspdisp.ui.desktop

# The folders of the build, created before a program is built in them (each program has them as order-only prerequisites)
$(BIN_DIR) $(DEP_DIR):
	mkdir -p $@

$(BIN_DIR)/pspdisp-daemon$(EXE) $(BIN_DIR)/pspdisp-ui$(EXE) $(BIN_DIR)/com.pspdisp.ui.svg $(BIN_DIR)/com.pspdisp.ui.desktop $(BIN_DIR)/test_display$(EXE) $(BIN_DIR)/test_audio$(EXE) $(BIN_DIR)/test_controller$(EXE): $(BIN_DIR) $(DEP_DIR)
	$(MAKE) -C $(DIR)/src/desktop $@ VERSION=$(VERSION) BIN_DIR=$(BIN_DIR) DEP_DIR=$(DEP_DIR) EXE=$(EXE)
