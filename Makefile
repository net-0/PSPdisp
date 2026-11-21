VERSION != git describe --tags

# The folder of this file, absolute (and not ending with /), as the sub-makes run from other folders (-C)
DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

BIN_DIR := $(DIR)/build
DEP_DIR := $(BIN_DIR)/dep

.PHONY: all desktop psp FORCE

# Note: "make" will run only the first entry, which must be all files
all: desktop psp

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

# The PSP app, in build/psp (with the PSP toolchain)
psp: $(BIN_DIR) $(DEP_DIR) FORCE
	$(MAKE) -k -C $(DIR)/src/psp \
		VERSION=$(VERSION) \
		BIN_DIR=$(BIN_DIR) \
		DEP_DIR=$(DEP_DIR)
