VERSION != git describe --tags

# The folder of this file, absolute (and not ending with /), as the sub-makes run from other folders (-C)
DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

BIN_DIR := $(DIR)/build
DEP_DIR := $(BIN_DIR)/dep

# The extension of the programs: .exe when the compiler builds for Windows (MinGW, e.g. the UCRT64 shell of MSYS2)
EXE := $(if $(findstring mingw,$(shell gcc -dumpmachine)),.exe)

.PHONY: all desktop

# Note: "make" will run only the first entry, which must be all files
all: desktop

# Always out of date, so the targets that depend on it are rebuilt on each run
FORCE:

# The folders of the build, created before a program is built in them (each program has them as order-only prerequisites)
$(BIN_DIR) $(DEP_DIR):
	mkdir -p $@

desktop: $(BIN_DIR) $(DEP_DIR) FORCE
	$(MAKE) -k -C $(DIR)/src/desktop \
		VERSION=$(VERSION) \
		BIN_DIR=$(BIN_DIR) \
		DEP_DIR=$(DEP_DIR)

