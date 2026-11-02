# This file uses the WINICON_* prefix, as it's included by the Makefile of the desktop (on Windows only), which gives it BIN_DIR
#
# The icon of the desktop programs (Windows), shown by Explorer and the taskbar: the icon of the app (ui/com.pspdisp.ui.svg) as a .ico,
# in a resource object (pspdisp.rc) that the programs link, WINICON_OBJ (in their prerequisites and their link).
# The .ico is made by ImageMagick, with the sizes Windows shows (the bigger ones for Explorer and the high DPI displays).

# The folder of this file, absolute (and not ending with /), as the paths in an included Makefile are relative to where make runs
WINICON_SRC_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# The folder of the icon and its resource object
WINICON_DIR := $(BIN_DIR)/winicon

# The resource object the programs link
WINICON_OBJ := $(WINICON_DIR)/pspdisp.res.o

# The folder of the icon, created before a file is made in it (each file has it as an order-only prerequisite)
$(WINICON_DIR):
	mkdir -p $@

# The icon, from the one of the app (bigger than 256 pixels, so each size is scaled down, with a transparent background)
$(WINICON_DIR)/com.pspdisp.ui.ico: $(WINICON_SRC_DIR)/ui/com.pspdisp.ui.svg | $(WINICON_DIR)
	@command -v magick >/dev/null || { echo "ImageMagick (magick) wasn't found, install it to make the icon of the programs (pacman -S mingw-w64-ucrt-x86_64-imagemagick)" >&2; exit 1; }
	magick -background none $< -define icon:auto-resize=256,128,64,48,32,24,16 $@

# The resource object, with the icon found in its folder
$(WINICON_OBJ): $(WINICON_SRC_DIR)/pspdisp.rc $(WINICON_DIR)/com.pspdisp.ui.ico | $(WINICON_DIR)
	windres --include-dir=$(WINICON_DIR) -O coff $< -o $@
